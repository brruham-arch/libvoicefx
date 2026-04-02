/**
 * voicefx.c - AML Mod Voice Changer
 * Using RTLD_DEFAULT for Dobby (already in game memory)
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

static void writeLog(const char* msg) {
    FILE* f = fopen("/sdcard/voicefx_log.txt", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

typedef unsigned int DWORD;
typedef unsigned int HRECORD;
typedef unsigned int HDSP;
typedef void (*DSPPROC)(HDSP, DWORD, void*, DWORD, void*);

// Function pointers
static void*   (*pDobbySymbolResolver)(const char*, const char*) = NULL;
static int     (*pDobbyHook)(void*, void*, void**) = NULL;
static HDSP    (*pBASS_ChannelSetDSP)(HRECORD, DSPPROC, void*, int) = NULL;
static HRECORD (*orig_BASS_RecordStart)(DWORD, DWORD, DWORD, void*, void*) = NULL;

static struct {
    float  pitch;
    int    enabled;
    short  ring[8192];
    int    wpos;
    float  spos;
    float  ovl[256];
} g_vfx = {1.0f, 0, {0}, 0, 0, {0}};

static inline float hann(int i, int n) {
    return 0.5f * (1.0f - cosf(6.283185307f * i / (n - 1)));
}
static inline short clamp16(float v) {
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : (short)v);
}

static void dspCallback(HDSP dsp, DWORD chan, void* buf, DWORD len, void* u) {
    if (!g_vfx.enabled || g_vfx.pitch == 1.0f) return;
    short* s = (short*)buf;
    int n = len / 2;

    for (int i = 0; i < n; i++)
        g_vfx.ring[(g_vfx.wpos + i) & 8191] = s[i];
    g_vfx.wpos = (g_vfx.wpos + n) & 8191;

    if (g_vfx.spos < 0) g_vfx.spos = g_vfx.wpos;

    float pos = g_vfx.spos;
    for (int i = 0; i < n; i++) {
        int   ip = (int)pos;
        float fr = pos - ip;
        float v  = g_vfx.ring[ip & 8191] * (1 - fr) + g_vfx.ring[(ip+1) & 8191] * fr;
        if (i < 256) v = v * hann(i, 512) + g_vfx.ovl[i] * (1 - hann(i, 512));
        s[i] = clamp16(v);
        pos += g_vfx.pitch;
    }
    g_vfx.spos = pos;
}

static HRECORD hook_BASS_RecordStart(DWORD freq, DWORD chans, DWORD flags, void* proc, void* user) {
    HRECORD h = orig_BASS_RecordStart(freq, chans, flags, proc, user);
    writeLog("[VFX] BASS_RecordStart HOOKED");
    pBASS_ChannelSetDSP(h, dspCallback, NULL, 1);
    return h;
}

void vc_set_pitch(float f) {
    if (f < 0.25f) f = 0.25f;
    if (f > 4.0f)  f = 4.0f;
    g_vfx.pitch = f;
}
void vc_enable(void)  { g_vfx.enabled = 1; }
void vc_disable(void) { g_vfx.enabled = 0; }
int  vc_is_enabled(void) { return g_vfx.enabled; }
float vc_get_pitch(void) { return g_vfx.pitch; }

// ============================================================
// INIT
// ============================================================
void Init_Mod(void) {
    remove("/sdcard/voicefx_log.txt");
    writeLog("=========================");
    writeLog("   LIB VOICEFX START    ");
    writeLog("=========================");

    // === CARI DOBBY LANGSUNG DARI MEMORI GAME ===
    pDobbySymbolResolver = (void*(*)(const char*,const char*))dlsym(RTLD_DEFAULT, "DobbySymbolResolver");
    pDobbyHook           = (int(*)(void*,void*,void**))dlsym(RTLD_DEFAULT, "DobbyHook");

    if (!pDobbySymbolResolver || !pDobbyHook) {
        writeLog("[X] Dobby functions NOT FOUND in memory!");
        return;
    }
    writeLog("[✓] Dobby FOUND in game memory");

    // === CARI BASS ===
    void* bobj = dlopen("libBASS.so", RTLD_NOW | RTLD_GLOBAL);
    if (!bobj) {
        writeLog("[X] libBASS.so NOT FOUND");
        return;
    }
    writeLog("[✓] libBASS.so OK");

    pBASS_ChannelSetDSP = (HDSP(*)(HRECORD,DSPPROC,void*,int))dlsym(bobj, "BASS_ChannelSetDSP");

    void* addr = pDobbySymbolResolver("libBASS.so", "BASS_RecordStart");
    if (!addr) {
        writeLog("[X] BASS_RecordStart NOT FOUND");
        return;
    }
    writeLog("[✓] Symbol BASS_RecordStart FOUND");

    pDobbyHook(addr, (void*)hook_BASS_RecordStart, (void**)&orig_BASS_RecordStart);
    writeLog("[✓] HOOK SUCCESS! READY");
}

// ============================================================
// ENTRY POINTS
// ============================================================
void OnModLoad(void) { Init_Mod(); }

__attribute__((constructor)) void init_ctor() { Init_Mod(); }

#include <jni.h>
jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    Init_Mod();
    return JNI_VERSION_1_6;
}
