/**
 * voicefx.c - Voice FX Engine for MoNetLoader/SAMP Android
 * Algorithm: TD-PSOLA (Time-Domain Pitch Synchronous Overlap Add)
 * Target: ARM Android (armv7 / arm64)
 * Author: libvoicefx project
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// ============================================================
// CONFIG
// ============================================================
#define MAX_FRAME_SIZE   4096
#define OVERLAP_SIZE     256
#define RING_SIZE        8192   // harus power of 2

// ============================================================
// STATE
// ============================================================
typedef struct {
    float   pitch_factor;       // 0.5 = turun 1 oktaf, 2.0 = naik 1 oktaf
    int     enabled;
    int     sample_rate;
    int     channels;

    // Ring buffer untuk history audio (hindari putus di batas buffer)
    int16_t ring[RING_SIZE];
    int     ring_write;         // posisi tulis saat ini
    int     ring_read;          // posisi baca synthesis

    // Output buffer overlap-add
    float   overlap[OVERLAP_SIZE];

    // Synthesis position (float untuk sub-sample accuracy)
    float   synth_pos;

} VoiceFX;

static VoiceFX g_vfx = {0};

// ============================================================
// HELPERS
// ============================================================

// Hann window untuk smooth overlap-add
static float hann(int i, int n) {
    return 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (n - 1)));
}

// Baca dari ring buffer (dengan wrap)
static inline int16_t ring_get(int pos) {
    return g_vfx.ring[pos & (RING_SIZE - 1)];
}

// Tulis ke ring buffer
static inline void ring_put(int pos, int16_t val) {
    g_vfx.ring[pos & (RING_SIZE - 1)] = val;
}

// Clamp float ke int16
static inline int16_t clamp16(float v) {
    if (v >  32767.0f) return  32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)v;
}

// ============================================================
// PUBLIC API — dipanggil dari Lua via FFI
// ============================================================

/**
 * vc_init: inisialisasi engine
 * @sample_rate: biasanya 8000 atau 16000 (SAMP voice)
 * @channels: 1 = mono
 */
void vc_init(int sample_rate, int channels) {
    memset(&g_vfx, 0, sizeof(VoiceFX));
    g_vfx.sample_rate   = sample_rate;
    g_vfx.pitch_factor  = 1.0f;
    g_vfx.channels      = channels;
    g_vfx.synth_pos     = 0.0f;
    g_vfx.ring_write    = 0;
    g_vfx.ring_read     = 0;
    g_vfx.enabled       = 0;
}

/**
 * vc_set_pitch: ubah pitch factor
 * @factor: 0.5 = 1 oktaf turun, 1.0 = normal, 2.0 = 1 oktaf naik
 */
void vc_set_pitch(float factor) {
    if (factor < 0.25f) factor = 0.25f;
    if (factor > 4.0f)  factor = 4.0f;
    g_vfx.pitch_factor = factor;
}

/**
 * vc_enable / vc_disable
 */
void vc_enable(void)  { g_vfx.enabled = 1; }
void vc_disable(void) { g_vfx.enabled = 0; }
int  vc_is_enabled(void) { return g_vfx.enabled; }
float vc_get_pitch(void) { return g_vfx.pitch_factor; }

/**
 * vc_process: proses buffer audio in-place
 * @buf: pointer ke buffer int16 (dari BASS DSP callback)
 * @n:   jumlah SAMPLE (bukan byte — sudah dibagi 2)
 *
 * Algoritma:
 *   1. Tulis input ke ring buffer
 *   2. Baca dari ring buffer dengan kecepatan pitch_factor
 *      menggunakan linear interpolation (sub-sample accuracy)
 *   3. Overlap-add dengan Hann window di batas frame
 *   4. Tulis hasil ke buf (in-place)
 */
void vc_process(int16_t* buf, int n) {
    if (!g_vfx.enabled || g_vfx.pitch_factor == 1.0f) return;
    if (n <= 0 || n > MAX_FRAME_SIZE) return;

    VoiceFX* v = &g_vfx;

    // Step 1: Tulis input ke ring buffer
    int base_write = v->ring_write;
    for (int i = 0; i < n; i++) {
        ring_put(base_write + i, buf[i]);
    }
    v->ring_write = base_write + n;

    // Pastikan synth_pos tidak terlalu jauh di belakang
    // (hindari wrap-around berlebihan)
    int available = v->ring_write - (int)v->synth_pos;
    if (available > RING_SIZE - 256) {
        // Terlalu jauh tertinggal, skip ke posisi aman
        v->synth_pos = (float)(v->ring_write - n);
    }

    // Step 2: Baca dengan pitch_factor (resampling)
    float factor = v->pitch_factor;
    float pos    = v->synth_pos;

    for (int i = 0; i < n; i++) {
        // Linear interpolation antar sample
        int   p0   = (int)pos;
        float frac = pos - p0;

        int16_t s0 = ring_get(p0);
        int16_t s1 = ring_get(p0 + 1);

        float val = s0 * (1.0f - frac) + s1 * frac;

        // Overlap-add untuk frame boundary smoothing
        if (i < OVERLAP_SIZE) {
            float w = hann(i, OVERLAP_SIZE * 2);
            val = val * w + v->overlap[i] * (1.0f - w);
        }

        buf[i] = clamp16(val);
        pos += factor;
    }

    // Simpan overlap zone untuk frame berikutnya
    float save_pos = pos - OVERLAP_SIZE;
    for (int i = 0; i < OVERLAP_SIZE; i++) {
        int   p0   = (int)save_pos;
        float frac = save_pos - p0;
        int16_t s0 = ring_get(p0);
        int16_t s1 = ring_get(p0 + 1);
        v->overlap[i] = s0 * (1.0f - frac) + s1 * frac;
        save_pos += factor;
    }

    v->synth_pos = pos;
}

/**
 * vc_destroy: cleanup (opsional, untuk good practice)
 */
void vc_destroy(void) {
    memset(&g_vfx, 0, sizeof(VoiceFX));
}
