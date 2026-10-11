#pragma once
// Small, allocation-free DSP building blocks shared by voices, tracks and the master bus.
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace gm
{
constexpr int kNumTracks = 16;
constexpr int kMaxSteps = 128;  // 8 bars of 16ths
constexpr int kNumPatterns = 16;
constexpr int kVoicesPerTrack = 4;
constexpr int kNumAux = 16;
constexpr float kPi = 3.14159265358979f;

//==============================================================================
// Stateless hashing RNG: same key -> same number. The sequencer keys randomness on
// (seed, track, step index) so a scheduled step evaluates identically in every block.
inline uint32_t hash32 (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline uint32_t hashMix (uint32_t a, uint32_t b) noexcept { return hash32 (a ^ (b * 0x9e3779b9U + 0x7f4a7c15U)); }
inline uint32_t hashMix (uint32_t a, uint32_t b, uint32_t c) noexcept { return hashMix (hashMix (a, b), c); }
inline float hash01 (uint32_t k) noexcept { return (float) (hash32 (k) >> 8) * (1.0f / 16777216.0f); }
inline float hashBi (uint32_t k) noexcept { return hash01 (k) * 2.0f - 1.0f; }

// Free-running RNG for things that should differ every time (MIDI-in hits, dice).
struct Rng
{
    uint32_t s = 0x12345678;
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() noexcept { return uni() * 2.0f - 1.0f; }
};

//==============================================================================
inline float dbToGain (float db) noexcept { return db <= -60.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float semitonesToRatio (float st) noexcept { return std::exp2 (st * (1.0f / 12.0f)); }
inline float clampf (float x, float lo, float hi) noexcept { return std::min (hi, std::max (lo, x)); }
// Combine a per-track and a global amount so neither can exceed 1 and both always count.
inline float unionAmt (float a, float b) noexcept { return 1.0f - (1.0f - a) * (1.0f - b); }

struct Smoother
{
    float value = 0, target = 0, coef = 0.999f;
    void setTime (float seconds, double sr) noexcept { coef = std::exp (-1.0f / (float) (std::max (1.0e-4, seconds * sr))); }
    void reset (float v) noexcept { value = target = v; }
    float next() noexcept { value = target + (value - target) * coef; return value; }
};

//==============================================================================
// Topology-preserving-transform state variable filter (Zavalishin / Simper).
// Cheap to re-tune per control block, stable under fast modulation.
struct SVF
{
    enum Mode { Off = 0, LP, BP, HP };
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float ic1 = 0, ic2 = 0;

    void set (float cutoffHz, float reso01, double sr) noexcept
    {
        cutoffHz = clampf (cutoffHz, 16.0f, (float) sr * 0.47f);
        g = std::tan (kPi * cutoffHz / (float) sr);
        k = 2.0f - 1.96f * clampf (reso01, 0.0f, 1.0f); // k=2 → Q 0.5, k→0.04 → near self-osc
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() noexcept { ic1 = ic2 = 0; }

    inline float process (float x, int mode) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        switch (mode)
        {
            case LP: return v2;
            case BP: return v1 * k; // unity-ish peak gain
            case HP: return x - k * v1 - v2;
            default: return x;
        }
    }
};

//==============================================================================
// Saturation with first-order antiderivative anti-aliasing (ADAA): instead of f(x) we output
// (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]), which strongly suppresses foldover from hard drive.
struct Saturator
{
    enum Type { Soft = 0, Hard, Fold, Tube, Rectify };
    float x1 = 0;

    static inline float logcosh (float x) noexcept
    {
        const float ax = std::fabs (x);
        return ax > 15.0f ? ax - 0.6931472f : std::log (std::cosh (x));
    }
    static inline float f (float x, int t) noexcept
    {
        switch (t)
        {
            case Hard:    return clampf (x, -1.0f, 1.0f);
            case Fold:    return std::sin (x);
            case Tube:    return std::tanh (x + 0.35f) - 0.3364f; // tanh(0.35)
            case Rectify: return std::tanh (std::fabs (x) * 1.4f) * 0.9f + std::tanh (x) * 0.3f;
            default:      return std::tanh (x);
        }
    }
    static inline float F (float x, int t) noexcept // antiderivative of f
    {
        switch (t)
        {
            case Hard:
            {
                const float ax = std::fabs (x);
                return ax <= 1.0f ? 0.5f * x * x : ax - 0.5f;
            }
            case Fold:    return -std::cos (x);
            case Tube:    return logcosh (x + 0.35f) - 0.3364f * x;
            case Rectify:
            {
                const float ax = std::fabs (x);
                return logcosh (ax * 1.4f) * (0.9f / 1.4f) * (x < 0 ? -1.0f : 1.0f) + 0.3f * logcosh (x);
            }
            default:      return logcosh (x);
        }
    }
    inline float process (float x, int t) noexcept
    {
        const float d = x - x1;
        float y;
        if (std::fabs (d) < 1.0e-4f)
            y = f (0.5f * (x + x1), t);
        else
            y = (F (x, t) - F (x1, t)) / d;
        x1 = x;
        return y;
    }
    void reset() noexcept { x1 = 0; }
};

//==============================================================================
// Bit-depth + sample-rate reduction ("Dirt"). bits >= 16 and factor <= 1 means bypass.
struct Crusher
{
    float phase = 0, holdL = 0, holdR = 0;
    inline void process (float& l, float& r, float bits, float downFactor) noexcept
    {
        if (downFactor > 1.0f)
        {
            phase += 1.0f;
            if (phase >= downFactor) { phase -= downFactor; holdL = l; holdR = r; }
            l = holdL; r = holdR;
        }
        if (bits < 15.9f)
        {
            const float q = std::exp2 (bits - 1.0f);
            l = std::round (l * q) / q;
            r = std::round (r * q) / q;
        }
    }
};

//==============================================================================
// 4-point cubic Hermite read from a channel; returns 0 outside [0, len).
inline float readHermite (const float* d, int len, double pos) noexcept
{
    const int i = (int) std::floor (pos);
    if (i < -2 || i > len) return 0.0f;
    const float t = (float) (pos - i);
    auto at = [d, len] (int j) { return (j >= 0 && j < len) ? d[j] : 0.0f; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

//==============================================================================
template <typename T, int N>
struct FixedVec
{
    T items[N];
    int size = 0;
    void clear() noexcept { size = 0; }
    bool push (const T& t) noexcept { if (size >= N) return false; items[size++] = t; return true; }
    T* begin() noexcept { return items; }
    T* end() noexcept { return items + size; }
    T& operator[] (int i) noexcept { return items[i]; }
    const T& operator[] (int i) const noexcept { return items[i]; }
};
} // namespace gm
