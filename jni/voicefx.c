/**
 * voicefx.c - AML Voice FX Mod untuk SA-MP Android
 * Entry point: __attribute__((constructor)) + JNI_OnLoad
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdio.h>

#define LOG_TAG "libvoicefx"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Ubah path ke folder yang pasti bisa write
#define LOGFILE "/sdcard/Android/data/com.sampmobilerp.game/files/voicefx_log.txt"
static void logfile(const char* msg) {
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

#define MAX_SAMPLES  4096
#define OVERLAP_SIZE 256
#define RING_SIZE    8192

typedef unsigned int DWORD;
typedef unsigned int HRECORD;
typedef unsigned int HDSP;
typedef void (*DSPPROC)(HDSP, DWORD, void*, DWORD, void*);

typedef struct {
    float  pitch_factor;
    int    enabled;
    int    sample_rate;
    int    channels;
    short  ring[RING_SIZE];
    int    ring_write;
    float  synth_pos;
    float  overlap[OVERLAP_SIZE];
} VoiceFX;

static VoiceFX g_vfx = {0};
static HRECORD g_recHandle = 0;
static HDSP    g_dspHandle = 0;

static HDSP    (*pBASSChannelSetDSP)(HRECORD, DSPPROC, void*, int) = NULL;
static int     (*pBASSChannelRemoveDSP)(HRECORD, HDSP) = NULL;
static HRECORD (*orig_BASSRecordStart)(DWORD, DWORD, DWORD, void*, void*) = NULL;
static void*   (*pDobbySymbolResolver)(const char*, const char*) = NULL;
static int     (*pDobbyHook)(void*, void*, void**) = NULL;

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

static void dspCallback(HDSP dsp, DWORD channel, void* buf, DWORD len, void* user) {
    if (!g_vfx.enabled || g_vfx.pitch_factor == 1.0f) return;
    short* s16 = (short*)buf;
    int n = (int)(len / 2);
    if (n <= 0 || n > MAX_SAMPLES) return;

    int base = g_vfx.ring_write;
    for (int i = 0; i < n; i++)
        g_vfx.ring[(base + i) & (RING_SIZE - 1)] = s16[i];
    g_vfx.ring_write = base + n;

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

    float sp = pos - OVERLAP_SIZE;
    for (int i = 0; i < OVERLAP_SIZE; i++) {
        int   p0   = (int)sp;
        float frac = sp - p0;
        g_vfx.overlap[i] = ring_get(p0) * (1.0f - frac) + ring_get(p0 + 1) * frac;
        sp += factor;
    }
    g_vfx.synth_pos = pos;
}

static HRECORD hook_BASSRecordStart(DWORD freq, DWORD chans, DWORD flags, void* proc, void* user) {
    HRECORD handle = orig_BASSRecordStart(freq, chans, flags, proc, user);
    g_recHandle = handle;

    memset(&g_vfx, 0, sizeof(VoiceFX));
    g_vfx.pitch_factor = 1.0f;
    g_vfx.sample_rate  = (int)freq;
    g_vfx.channels     = (int)chans;

    LOGI("BASS_RecordStart hooked! handle=%u freq=%u", handle, freq);
    logfile("[VFX] BASS_RecordStart hooked!");

    if (pBASSChannelSetDSP)
        g_dspHandle = pBASSChannelSetDSP(handle, dspCallback, NULL, 1);

    return handle;
}

// ============================================================
// PUBLIC API
// ============================================================
void vc_set_pitch(float factor) {
    if (factor < 0.25f) factor = 0.25f;
    if (factor > 4.0f)  factor = 4.0f;
    g_vfx.pitch_factor = factor;
    LOGI("pitch set to %.2f", factor);
}
void vc_enable(void)     { g_vfx.enabled = 1; LOGI("enabled"); }
void vc_disable(void)    { g_vfx.enabled = 0; LOGI("disabled"); }
int  vc_is_enabled(void) { return g_vfx.enabled; }
float vc_get_pitch(void) { return g_vfx.pitch_factor; }

// ============================================================
// AML ENTRY POINT
// ============================================================
void __attribute__((constructor)) OnModLoad(void) {
    remove(LOGFILE);
    logfile("[VFX] OnModLoad dipanggil!");
    LOGI("OnModLoad called");

    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) {
        logfile("[VFX] ERROR: libdobby.so tidak bisa dibuka");
        LOGE("dlopen libdobby.so failed: %s", dlerror());
        return;
    }
    logfile("[VFX] libdobby.so OK");

    pDobbySymbolResolver = (void*(*)(const char*, const char*))dlsym(hDobby, "DobbySymbolResolver");
    pDobbyHook           = (int(*)(void*, void*, void**))dlsym(hDobby, "DobbyHook");
    if (!pDobbySymbolResolver || !pDobbyHook) {
        logfile("[VFX] ERROR: Dobby symbol tidak ditemukan");
        return;
    }
    logfile("[VFX] Dobby symbols OK");

    void* hBASS = dlopen("libBASS.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hBASS) {
        logfile("[VFX] ERROR: libBASS.so tidak bisa dibuka");
        LOGE("dlopen libBASS.so failed: %s", dlerror());
        return;
    }
    logfile("[VFX] libBASS.so OK");

    pBASSChannelSetDSP    = (HDSP(*)(HRECORD, DSPPROC, void*, int))dlsym(hBASS, "BASS_ChannelSetDSP");
    pBASSChannelRemoveDSP = (int(*)(HRECORD, HDSP))dlsym(hBASS, "BASS_ChannelRemoveDSP");

    void* addr = pDobbySymbolResolver("libBASS.so", "BASS_RecordStart");
    if (!addr) {
        logfile("[VFX] ERROR: BASS_RecordStart tidak ditemukan");
        return;
    }
    logfile("[VFX] BASS_RecordStart addr ditemukan");

    int r = pDobbyHook(addr, (void*)hook_BASSRecordStart, (void**)&orig_BASSRecordStart);
    if (r != 0) {
        logfile("[VFX] ERROR: DobbyHook gagal");
        return;
    }
    logfile("[VFX] Hook BASS_RecordStart OK!");

    g_vfx.pitch_factor = 1.0f;
    g_vfx.enabled      = 0;

    logfile("[VFX] OnModLoad selesai - siap!");
    LOGI("OnModLoad done");
}

// Tambahan untuk jaminan load
#include <jni.h>
jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    OnModLoad();
    return JNI_VERSION_1_6;
}
