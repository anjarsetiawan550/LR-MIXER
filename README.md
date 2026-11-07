# LR PRO AUDIO DEVICE — VST3 + Standalone (ASIO, latensi 0 sampel)

Plugin JUCE 8 (C++17) dengan GUI neon violet dari asset kit. Format: **VST3** dan **Standalone** (Windows x64).

## Cara build (GitHub Actions, sama seperti proyek sebelumnya)
1. Upload seluruh isi folder ini ke repo GitHub (folder `.github/workflows/build.yml` harus ikut).
2. Buka tab **Actions** -> workflow *Build VST3 + Standalone* (otomatis jalan saat push, atau klik *Run workflow*).
3. Selesai -> unduh artifact **LR_PRO_AUDIO_DEVICE_windows**: berisi `VST3/LR PRO AUDIO DEVICE.vst3` dan `Standalone/LR PRO AUDIO DEVICE.exe`.
4. Pasang VST3: salin ke `C:\Program Files\Common Files\VST3\` lalu scan ulang di host (mis. Waves SuperRack).

Workflow mengunduh **Steinberg ASIO SDK** otomatis. Jika unduhan gagal, unduh manual SDK dari steinberg.net, lalu taruh sehingga ada
`ThirdParty/asiosdk/common/iasiodrv.h` di repo. Tanpa SDK, build tetap jalan tetapi tanpa ASIO (log CMake menampilkan peringatan).

## ASIO di Standalone
Buka aplikasi -> tombol **Options** (kiri atas jendela) -> *Audio device type* = **ASIO** -> pilih driver interface audio kamu -> atur **buffer size** serendah yang stabil (mis. 64, 32, atau 16 sampel).
Pengaturan tersimpan otomatis. Tanpa ASIO, tersedia WASAPI (mode exclusive juga diaktifkan).

## Latensi
- Latensi yang ditambahkan plugin: **0 sampel** (`getLatencySamples() = 0`). Semua pemrosesan memakai filter IIR (tanpa FIR/linear-phase), kompresor **tanpa lookahead**, tanpa oversampling, tanpa FFT di jalur audio.
- FFT hanya untuk tampilan (dihitung di thread GUI), tidak menambah latensi audio.
- Latensi total = buffer ASIO + latensi converter interface (diatur dari driver/Options).
- Parameter dihaluskan per 32 sampel (20 ms) agar tidak ada klik saat knob digeser; tidak memengaruhi latensi.
- Pemrosesan presisi ganda (double) di dalam biquad, perlindungan denormal aktif.

## Alur sinyal
`Input -> HPF -> EQ1..EQ4 -> LPF -> Kompresor -> Makeup -> Output` (BYPASS = crossfade 10 ms ke sinyal asli).

| Bagian | Detail |
|---|---|
| HPF / LPF | 20 Hz - 20 kHz; tipe **BW** Butterworth, **LR** Linkwitz-Riley, **BS** Bessel; slope 12/18/24 dB/oct. LR 18 tidak ada secara teori -> otomatis menjadi 24 dB. |
| EQ 1-4 | Bell (peaking) 20 Hz - 20 kHz, gain +-18 dB, Q 0.1 - 10, tombol ON per band |
| Kompresor | Threshold -60..0 dB, Ratio 1..20:1, Knee 0..24 dB (soft knee), Attack 0.1..200 ms, Release 5..2000 ms, Makeup 0..24 dB; detektor peak stereo-link |
| Tampilan | FFT realtime 8192 titik (20 Hz - 20 kHz, 0..-72 dB), kurva respons EQ, grafik kompresor realtime + titik level, meter GR, meter IN/OUT dengan peak hold |

## Kontrol GUI
- Knob: drag vertikal, scroll mouse, **klik ganda = reset ke default**. Kotak nilai: **klik ganda lalu ketik angka** (mis. `2.5k`, `-18`).
- Layar FFT: **seret titik 1-4** untuk mengubah frekuensi + gain, **scroll di atas titik** = Q, **klik ganda titik** = ON/OFF band.
- Jendela bisa di-resize (rasio tetap 5:3), tampilan ikut skala.
- Semua parameter bisa di-automasi dari host (ID: `comp_thresh`, `comp_ratio`, `comp_knee`, `comp_attack`, `comp_release`, `comp_makeup`, `bypass`, `hpf_on/freq/type/slope`, `lpf_on/freq/type/slope`, `eq1..eq4_on/freq/gain/q`).

## Struktur
- `Source/Dsp.h` — inti DSP murni C++ (sudah diuji numerik: -3 dB/-6 dB tepat di fc, slope, kurva kompresor)
- `Source/PluginProcessor.*` — parameter, loop audio, meter, ring buffer analyzer
- `Source/PluginEditor.*` — GUI (film-strip knob, sprite tombol, meter, FFT/EQ/kompresor)
- `Assets/` — asset GUI resolusi 2x (di-embed lewat BinaryData, ditampilkan skala otomatis)
- `tests/dsp_test.cpp` — uji DSP: `g++ -std=c++17 tests/dsp_test.cpp -o dsp_test && ./dsp_test`

## Catatan lisensi
- JUCE 8: tanpa lisensi berbayar, jendela menampilkan splash screen "Made with JUCE" dan proyek mengikuti GPLv3.
- ASIO SDK: lisensi Steinberg / GPLv3 — jangan mendistribusikan SDK-nya di repo publik tanpa memeriksa lisensinya; biner hasil build yang menyertakan ASIO ikut aturan lisensi tersebut.
