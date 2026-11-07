// Pure C++ DSP core (no JUCE dependency) - zero latency: IIR filters + feed-forward compressor.
#pragma once
#include <cmath>
#include <algorithm>
#include <complex>

namespace lr
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxSections = 2;

struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
struct BiquadState { double z1 = 0, z2 = 0; };

// Transposed direct form II, double precision
inline double run (const Coef& c, BiquadState& s, double x) noexcept
{
    const double y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

inline double clampFreq (double fs, double f) { return std::min (std::max (f, 5.0), 0.49 * fs); }

inline Coef lowpass2 (double fs, double f, double q)
{
    const double w = 2.0 * kPi * clampFreq (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
    Coef k; k.b0 = (1 - c) * 0.5 / a0; k.b1 = (1 - c) / a0; k.b2 = k.b0; k.a1 = -2 * c / a0; k.a2 = (1 - al) / a0; return k;
}
inline Coef highpass2 (double fs, double f, double q)
{
    const double w = 2.0 * kPi * clampFreq (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
    Coef k; k.b0 = (1 + c) * 0.5 / a0; k.b1 = -(1 + c) / a0; k.b2 = k.b0; k.a1 = -2 * c / a0; k.a2 = (1 - al) / a0; return k;
}
inline Coef lowpass1 (double fs, double f)
{
    const double K = std::tan (kPi * clampFreq (fs, f) / fs), n = 1.0 / (1.0 + K);
    Coef k; k.b0 = K * n; k.b1 = k.b0; k.a1 = (K - 1) * n; return k;
}
inline Coef highpass1 (double fs, double f)
{
    const double K = std::tan (kPi * clampFreq (fs, f) / fs), n = 1.0 / (1.0 + K);
    Coef k; k.b0 = n; k.b1 = -n; k.a1 = (K - 1) * n; return k;
}
inline Coef peaking (double fs, double f, double gainDb, double q)
{
    const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kPi * clampFreq (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al / A;
    Coef k; k.b0 = (1 + al * A) / a0; k.b1 = -2 * c / a0; k.b2 = (1 - al * A) / a0; k.a1 = k.b1; k.a2 = (1 - al / A) / a0; return k;
}

enum PassType { BW = 0, LR = 1, BS = 2 };   // Butterworth, Linkwitz-Riley, Bessel
// slope index: 0 = 12 dB/oct, 1 = 18 dB/oct, 2 = 24 dB/oct   (LR 18 is not a real LR order -> uses LR 24)

// Design a high-/low-pass cascade. Returns number of sections (<= kMaxSections).
inline int designPass (bool high, int type, int slope, double fs, double fc, Coef* out)
{
    auto sec2 = [&] (double mult, double q) { const double f0 = high ? fc / mult : fc * mult; return high ? highpass2 (fs, f0, q) : lowpass2 (fs, f0, q); };
    auto sec1 = [&] (double mult)            { const double f0 = high ? fc / mult : fc * mult; return high ? highpass1 (fs, f0) : lowpass1 (fs, f0); };
    int n = 0;
    if (type == LR)
    {
        if (slope == 0) { out[n++] = sec2 (1.0, 0.5); }
        else            { out[n++] = sec2 (1.0, 0.70710678118); out[n++] = sec2 (1.0, 0.70710678118); }
    }
    else if (type == BS)
    {   // -3 dB normalised Bessel pole sets
        if (slope == 0)      { out[n++] = sec2 (1.27200, 0.57735027); }
        else if (slope == 1) { out[n++] = sec1 (1.32270); out[n++] = sec2 (1.44765, 0.69105); }
        else                 { out[n++] = sec2 (1.60330, 0.80554); out[n++] = sec2 (1.43008, 0.52193); }
    }
    else
    {
        if (slope == 0)      { out[n++] = sec2 (1.0, 0.70710678118); }
        else if (slope == 1) { out[n++] = sec1 (1.0); out[n++] = sec2 (1.0, 1.0); }
        else                 { out[n++] = sec2 (1.0, 0.54119610); out[n++] = sec2 (1.0, 1.30656296); }
    }
    return n;
}

inline double magnitudeDb (const Coef& c, double fs, double f)
{
    const double w = 2.0 * kPi * f / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    const auto num = c.b0 + c.b1 * z1 + c.b2 * z2, den = 1.0 + c.a1 * z1 + c.a2 * z2;
    return 20.0 * std::log10 (std::max (1e-9, std::abs (num / den)));
}

// ---------- settings snapshot ----------
struct PassParams { bool on = false; float freq = 100; int type = 0, slope = 0; };
struct BandParams { bool on = true; float freq = 1000, gain = 0, q = 1; };
struct Settings
{
    PassParams hpf, lpf; BandParams eq[4];
    float thresh = -18, ratio = 2, knee = 6, attack = 10, release = 100, makeup = 0; bool bypass = false;
};

// Frequency-response model (UI): sums every active section.
struct ResponseModel
{
    Coef c[4 + 2 * kMaxSections]; int n = 0;
    void build (const Settings& s, double fs)
    {
        n = 0;
        if (s.hpf.on) n += designPass (true,  s.hpf.type, s.hpf.slope, fs, s.hpf.freq, c + n);
        for (int b = 0; b < 4; ++b) if (s.eq[b].on) c[n++] = peaking (fs, s.eq[b].freq, s.eq[b].gain, s.eq[b].q);
        if (s.lpf.on) n += designPass (false, s.lpf.type, s.lpf.slope, fs, s.lpf.freq, c + n);
    }
    double db (double fs, double f) const { double d = 0; for (int i = 0; i < n; ++i) d += magnitudeDb (c[i], fs, f); return d; }
};

// ---------- compressor (feed-forward, soft knee, stereo-linked peak detector, NO lookahead => 0 latency) ----------
struct Compressor
{
    double fs = 48000, gr = 0, atk = 0, rel = 0;
    void setup (double sampleRate, double attackMs, double releaseMs)
    {
        fs = sampleRate;
        atk = std::exp (-1.0 / (0.001 * std::max (0.01, attackMs) * fs));
        rel = std::exp (-1.0 / (0.001 * std::max (0.1, releaseMs) * fs));
    }
    // static curve: returns output level (dB) for input level (dB)
    static double outDb (double x, double T, double R, double K)
    {
        const double over = x - T;
        if (2.0 * over < -K) return x;
        if (K > 1e-6 && 2.0 * std::fabs (over) <= K) return x + (1.0 / R - 1.0) * (over + K * 0.5) * (over + K * 0.5) / (2.0 * K);
        return T + over / R;
    }
    // returns gain reduction in dB (>= 0), smoothed
    double process (double detectorLin, double T, double R, double K)
    {
        const double xdB = 20.0 * std::log10 (std::max (detectorLin, 1e-6));
        const double target = std::max (0.0, xdB - outDb (xdB, T, R, K));
        const double c = target > gr ? atk : rel;
        gr = c * gr + (1.0 - c) * target;
        if (gr < 1e-7) gr = 0;
        return gr;
    }
    void reset() { gr = 0; }
};
} // namespace lr
