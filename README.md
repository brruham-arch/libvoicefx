# libvoicefx

Voice changer engine untuk SA-MP Android (MoNetLoader 3.6.0 + AML).

## Algoritma
- **TD-PSOLA** dengan linear interpolation
- Ring buffer antar-frame (tidak putus di batas callback)
- Overlap-add dengan Hann window (tidak ada echo)
- In-place processing (zero extra allocation saat runtime)

## Struktur
```
jni/
  voicefx.c       ← Engine C (compile → libvoicefx.so)
  Android.mk
  Application.mk
lua/
  voicechanger.lua ← Controller Lua untuk MoNetLoader
```

## Build
GitHub Actions otomatis build saat push ke `main`.
Download `.so` dari tab **Releases** atau **Actions → Artifacts**.

### Manual build di Termux
Lihat bagian **Setup Termux** di bawah.

## Install
1. Download `libvoicefx.so` sesuai arch HP kamu:
   - ARM32 (kebanyakan HP lama): `armeabi-v7a/libvoicefx.so`
   - ARM64 (HP modern): `arm64-v8a/libvoicefx.so`
2. Taruh di `/sdcard/MoNetLoader/libvoicefx.so`
3. Taruh `voicechanger.lua` di folder scripts MoNetLoader

## Commands (in-game)
| Command | Fungsi |
|---|---|
| `/vfx` | Toggle voice changer ON/OFF |
| `/vfp 0.5` | Set pitch (0.3–3.0, default 1.0) |
| `/vfs` | Lihat status |

### Contoh pitch
| Value | Efek |
|---|---|
| `0.5` | Suara berat/dalam |
| `0.75` | Sedikit lebih rendah |
| `1.0` | Normal |
| `1.5` | Sedikit lebih tinggi |
| `2.0` | Suara tinggi/melengking |
