/**
 * voicefx.c - AML Voice FX Mod untuk SA-MP Android
 * Entry point: OnModLoad (format AML)
 * Build: arm64-v8a
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <dlfcn.h>

// ============================================================
// CONFIG
// ============================================================
#define MAX_SAMPLES  4096
#define OVERLAP_SIZE 256
#define RING_SIZE    8192

// ============================================================
// BASS types
// ============================================================
typedef unsigned int HRECORD;
typedef unsigned int HDSP;
typedef void (*DSPPROC)(HDSP, DWORD, void*, DWORD, void*);
#define DWORD unsigned int

// ============================================================
// STATE
// ============================================================
typedef struct {
    float   pitch_factor;
    int     enabled;
    int     sample_rate;
    int     channels;
    short   ring[RING_SIZE];
    int     ring_write;
    float   synth_pos;
    float   overlap[OVERLAP_SIZE];
} VoiceFX;

static VoiceFX g_vfx = {0};
static HRECORD g_recHandle = 0;
static HDSP    g_dspHandle = 0;

// BASS function pointers
static HDSP (*pBASSChannelSetDSP)(HRECORD, DSPPROC, void*, int) = NULL;
static int  (*pBASSChannelRemoveDSP)(HRECORD, HDSP) = NULL;
static HRECORD (*pBASSRecordStart)(DWORD, DWORD, DWORD, void*, void*) = NULL;
static HRECORD (*orig_BASSRecordStart)(DWORD, DWORD, DWORD, void*, void*) = NULL;

// Dobby
static void* (*pDobbySymbolResolver)(const char*, const char*) = NULL;
static int   (*pDobbyHook)(void*, void*, void**) = NULL;

// ============================================================
// AUDIO ENGINE
// ============================================================
static inline float hann(int i, int n) {
    return 0.5f * (1.0f - cosf(6.28318f * i / (n - 1)));
}

static inline short ring_get(int pos) {
    return g_vfx.ring[pos & (RING_SIZE - 1)];
}

static inline short clamp16(float v) {
    if (v >  32767.0f) return  32767;
    if (v < -32768.0f) return -32768;
    return (short)v;
}

// ============================================================
// DSP CALLBACK — dipanggil BASS tiap ada audio
// ============================================================
static void dspCallback(HDSP dsp, DWORD channel, void* buf, DWORD len, void* user) {
    if (!g_vfx.enabled || g_vfx.pitch_factor == 1.0f) return;

    short* s16 = (short*)buf;
    int n = (int)(len / 2);
    if (n <= 0 || n > MAX_SAMPLES) return;

    // Tulis ke ring buffer
    int base = g_vfx.ring_write;
    for (int i = 0; i < n; i++)
        g_vfx.ring[(base + i) & (RING_SIZE - 1)] = s16[i];
    g_vfx.ring_write = base + n;

    // Jaga synth_pos tidak tertinggal terlalu jauh
    if ((g_vfx.ring_write - (int)g_vfx.synth_pos) > RING_SIZE - 256)
        g_vfx.synth_pos = (float)(g_vfx.ring_write - n);

    float factor = g_vfx.pitch_factor;
    float pos    = g_vfx.synth_pos;

    for (int i = 0; i < n; i++) {
        int   p0   = (int)pos;
        float frac = pos - p0;
        float val  = ring_get(p0) * (1.0f - frac) + ring_get(p0 + 1) * frac;

        if (i < OVERLAP_SIZE) {
            float w = hann(i, OVERLAP_SIZE * 2);
            val = val * w + g_vfx.overlap[i] * (1.0f - w);
        }

        s16[i] = clamp16(val);
        pos += factor;
    }

    // Simpan overlap untuk frame berikutnya
    float sp = pos - OVERLAP_SIZE;
    for (int i = 0; i < OVERLAP_SIZE; i++) {
        int   p0   = (int)sp;
        float frac = sp - p0;
        g_vfx.overlap[i] = ring_get(p0) * (1.0f - frac) + ring_get(p0 + 1) * frac;
        sp += factor;
    }

    g_vfx.synth_pos = pos;
}

// ============================================================
// HOOK: BASS_RecordStart
// ============================================================
static HRECORD hook_BASSRecordStart(DWORD freq, DWORD chans, DWORD flags, void* proc, void* user) {
    HRECORD handle = orig_BASSRecordStart(freq, chans, flags, proc, user);
    g_recHandle = handle;
    g_vfx.sample_rate = freq;
    g_vfx.channels    = chans;

    // Reset engine dengan sample rate yang benar
    memset(&g_vfx, 0, sizeof(VoiceFX));
    g_vfx.pitch_factor = 1.0f;
    g_vfx.sample_rate  = freq;
    g_vfx.channels     = chans;

    // Pasang DSP
    if (pBASSChannelSetDSP) {
        g_dspHandle = pBASSChannelSetDSP(handle, dspCallback, NULL, 1);
    }

    return handle;
}

// ============================================================
// PUBLIC API — dipanggil dari Lua via dlsym
// ============================================================
void vc_set_pitch(float factor) {
    if (factor < 0.25f) factor = 0.25f;
    if (factor > 4.0f)  factor = 4.0f;
    g_vfx.pitch_factor = factor;
}

void vc_enable(void)  { g_vfx.enabled = 1; }
void vc_disable(void) { g_vfx.enabled = 0; }
int  vc_is_enabled(void)  { return g_vfx.enabled; }
float vc_get_pitch(void)  { return g_vfx.pitch_factor; }

// ============================================================
// AML ENTRY POINT
// ============================================================
void OnModLoad(void) {
    // Load Dobby
    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) return;

    pDobbySymbolResolver = dlsym(hDobby, "DobbySymbolResolver");
    pDobbyHook           = dlsym(hDobby, "DobbyHook");
    if (!pDobbySymbolResolver || !pDobbyHook) return;

    // Load BASS
    void* hBASS = dlopen("libBASS.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hBASS) return;

    pBASSChannelSetDSP    = dlsym(hBASS, "BASS_ChannelSetDSP");
    pBASSChannelRemoveDSP = dlsym(hBASS, "BASS_ChannelRemoveDSP");

    // Hook BASS_RecordStart
    void* addr = pDobbySymbolResolver("libBASS.so", "BASS_RecordStart");
    if (!addr) return;

    pDobbyHook(addr, (void*)hook_BASSRecordStart, (void**)&orig_BASSRecordStart);

    // Init engine default
    memset(&g_vfx, 0, sizeof(VoiceFX));
    g_vfx.pitch_factor = 1.0f;
    g_vfx.enabled      = 0;
}
