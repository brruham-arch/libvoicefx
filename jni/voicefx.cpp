#include <android/log.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdint.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VoiceFX", __VA_ARGS__)

// ============================================================
// STRUCT AML
// ============================================================
struct ModInfo {
    const char* id;
    const char* name;
    const char* version;
    const char* author;
    ModInfo(const char* i, const char* n, const char* v, const char* a)
        : id(i), name(n), version(v), author(a) {}
};

struct IAML {
    virtual void ShowToast(bool, const char*) = 0;
};

// ============================================================
// MOD INFO
// ============================================================
static ModInfo g_modinfo("com.burhan.voicefx", "VoiceFX", "1.0", "Burhan");

// --- WAJIB PAKAI extern "C" ---
extern "C" void* __GetModInfo() {
    return &g_modinfo;
}

// ============================================================
// FUNGSI WAJIB KEDUA
// ============================================================
extern "C" void OnModPreLoad() {
    // Log awal
    FILE* f = fopen("/sdcard/aml_log.txt", "w");
    if(f) {
        fprintf(f, "OnModPreLoad CALLED!\n");
        fclose(f);
    }
    LOGI("OnModPreLoad done");
}

// ============================================================
// TYPEDEFS & AUDIO
// ============================================================
typedef unsigned int DWORD;
typedef unsigned int HRECORD;
typedef void* HDSP;
typedef void (*DSPPROC)(HDSP, DWORD, void*, DWORD, void*);

static void*   (*pDobbySymbolResolver)(const char*, const char*) = nullptr;
static int     (*pDobbyHook)(void*, void*, void**) = nullptr;
static HDSP    (*pBASS_ChannelSetDSP)(HRECORD, DSPPROC, void*, int) = nullptr;
static HRECORD (*orig_BASS_RecordStart)(DWORD, DWORD, DWORD, void*, void*) = nullptr;

static struct {
    float  pitch;
    int    enabled;
    short  ring[8191];
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

static void dspCallback(HDSP, DWORD, void* buf, DWORD len, void*) {
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
    pBASS_ChannelSetDSP(h, dspCallback, nullptr, 1);
    return h;
}

// ============================================================
// API
// ============================================================
extern "C" {
    void vc_set_pitch(float f) { if(f<0.25f)f=0.25f; if(f>4.0f)f=4.0f; g_vfx.pitch=f; }
    void vc_enable(void)  { g_vfx.enabled=1; }
    void vc_disable(void) { g_vfx.enabled=0; }
}

// ============================================================
// ON MOD LOAD
// ============================================================
extern "C" void OnModLoad() {
    LOGI("=== OnModLoad START ===");

    void* (*GetInterface)(const char*) = (void*(*)(const char*))dlsym(RTLD_DEFAULT, "GetInterface");
    if(!GetInterface) return;

    IAML* aml = (IAML*)GetInterface("AMLInterface");
    if(!aml) return;

    aml->ShowToast(true, "VoiceFX Loaded!");

    // CARI DOBBY
    pDobbySymbolResolver = (void*(*)(const char*,const char*))dlsym(RTLD_DEFAULT, "DobbySymbolResolver");
    pDobbyHook           = (int(*)(void*,void*,void**))dlsym(RTLD_DEFAULT, "DobbyHook");

    if(!pDobbyHook) {
        aml->ShowToast(true, "Dobby not found");
        return;
    }

    void* hBASS = dlopen("libBASS.so", RTLD_NOW | RTLD_GLOBAL);
    if(!hBASS) {
        aml->ShowToast(true, "BASS not found");
        return;
    }

    pBASS_ChannelSetDSP = (HDSP(*)(HRECORD,DSPPROC,void*,int))dlsym(hBASS, "BASS_ChannelSetDSP");
    void* addr = pDobbySymbolResolver("libBASS.so", "BASS_RecordStart");

    if(!addr) {
        aml->ShowToast(true, "Func not found");
        return;
    }

    pDobbyHook(addr, (void*)hook_BASS_RecordStart, (void**)&orig_BASS_RecordStart);
    aml->ShowToast(true, "Hook Success!");
}
