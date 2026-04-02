/**
 * voicefx.c - AML Mod Voice Changer
 * ENTRY POINT: OnModLoad
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

// ============================================================
// DEBUG TOOLS - AKAN MUNCUL DI LAYAR
// ============================================================
static void showToast(const char* text) {
    FILE* p = popen("am broadcast -a android.intent.action.BOOT_COMPLETED --es text \""(text)"\"", "w");
    if (p) pclose(p);
}

static void printToChat(const char* msg) {
    LOGI("%s", msg);
    showToast(msg);
    
    FILE* f = fopen("/sdcard/voicefx_log.txt", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

// ============================================================
// TYPEDEFS
// ============================================================
typedef unsigned int DWORD;
typedef unsigned int HRECORD;
typedef unsigned int HDSP;
typedef void (*DSPPROC)(HDSP, DWORD, void*, DWORD, void*);

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

// ============================================================
// AUDIO
// ============================================================
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
    printToChat("[VFX] BASS_RecordStart HOOKED");
    pBASS_ChannelSetDSP(h, dspCallback, NULL, 1);
    return h;
}

// ============================================================
// API
// ============================================================
void vc_set_pitch(float f) {
    if (f < 0.25f) f = 0.25f;
    if (f > 4.0f)  f = 4.0f;
    g_vfx.pitch = f;
}
void vc_enable(void)  { g_vfx.enabled = 1; printToChat("[VFX] ENABLED"); }
void vc_disable(void) { g_vfx.enabled = 0; printToChat("[VFX] DISABLED"); }
int  vc_is_enabled(void) { return g_vfx.enabled; }
float vc_get_pitch(void) { return g_vfx.pitch; }

// ============================================================
// ENTRY POINT - WAJIB NAMA OnModLoad
// ============================================================
void OnModLoad(void) {
    remove("/sdcard/voicefx_log.txt");
    printToChat("");
    printToChat("=========================");
    printToChat("   LIB VOICEFX LOADED   ");
    printToChat("=========================");

    void* dobj = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!dobj) {
        printToChat("[X] libdobby.so NOT FOUND");
        return;
    }
    printToChat("[✓] libdobby.so OK");

    pDobbySymbolResolver = (void*(*)(const char*,const char*))dlsym(dobj, "DobbySymbolResolver");
    pDobbyHook           = (int(*)(void*,void*,void**))dlsym(dobj, "DobbyHook");

    void* bobj = dlopen("libBASS.so", RTLD_NOW | RTLD_GLOBAL);
    if (!bobj) {
        printToChat("[X] libBASS.so NOT FOUND");
        return;
    }
    printToChat("[✓] libBASS.so OK");

    pBASS_ChannelSetDSP = (HDSP(*)(HRECORD,DSPPROC,void*,int))dlsym(bobj, "BASS_ChannelSetDSP");

    void* addr = pDobbySymbolResolver("libBASS.so", "BASS_RecordStart");
    if (!addr) {
        printToChat("[X] BASS_RecordStart NOT FOUND");
        return;
    }
    printToChat("[✓] Symbol BASS_RecordStart FOUND");

    int ret = pDobbyHook(addr, (void*)hook_BASS_RecordStart, (void**)&orig_BASS_RecordStart);
    if (ret != 0) {
        printToChat("[X] HOOK FAILED");
        return;
    }
    printToChat("[✓] HOOK SUCCESS! READY");
    printToChat("=========================");
}
