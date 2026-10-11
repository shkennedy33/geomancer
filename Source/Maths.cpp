#include "Maths.h"

namespace gm
{
void Maths::reset() noexcept
{
    for (auto& g : gens) g = Gen{};
    eocLast[0] = eocLast[1] = false;
}

float Maths::shape (double u, float c) noexcept
{
    const float x = clampf ((float) u, 0.0f, 1.0f);
    if (c > 0.01f) return std::pow (x, 1.0f + 3.0f * c);              // exponential: slow then fast
    if (c < -0.01f) return 1.0f - std::pow (1.0f - x, 1.0f - 3.0f * c); // logarithmic: fast then slow
    return x;
}

float Maths::value (int src) const noexcept
{
    const float a = gens[0].scaled, b = gens[1].scaled;
    switch (src)
    {
        case msA: return a;
        case msB: return b;
        case msOr: return std::max (a, b);
        case msSum: return clampf (a + b, -1.0f, 1.0f);
        case msDiff: return clampf (a - b, -1.0f, 1.0f);
        default: return 0;
    }
}

void Maths::process (int numSamples, double sr, double ppq0, double bpm, double barLen,
                     const MathsChannelParams (&ch)[2], const FixedVec<int, 64> (&trackHits)[kNumTracks],
                     FixedVec<MathsEvent, 256>& events) noexcept
{
    const double pps = bpm / (60.0 * sr);
    const double stepSec = 0.25 * 60.0 / bpm; // one 1/16 step
    const double dt = kTick / sr;

    auto clockFires = [&] (int trigIn, int a, int b) // does the clock input tick inside samples [a, b)?
    {
        double q = 0;
        switch (trigIn)
        {
            case mi16: q = 0.25; break;
            case mi8: q = 0.5; break;
            case mi4: q = 1.0; break;
            case miBar: q = barLen; break;
            default: return false;
        }
        const double pa = ppq0 + a * pps, pb = ppq0 + b * pps;
        return std::ceil (pa / q - 1.0e-9) * q < pb;
    };
    auto trackFires = [&] (int trigIn, int a, int b)
    {
        const int t = trigIn - miTrack1;
        if (t < 0 || t >= kNumTracks) return false;
        for (int i = 0; i < trackHits[t].size; ++i)
            if (trackHits[t][i] >= a && trackHits[t][i] < b) return true;
        return false;
    };

    for (int s0 = 0; s0 < numSamples; s0 += kTick)
    {
        const int s1 = std::min (numSamples, s0 + kTick);
        bool eocNow[2] = { false, false };
        for (int c = 0; c < 2; ++c)
        {
            auto& g = gens[c];
            const auto& p = ch[c];
            const int other = 1 - c;
            if (clockFires (p.trigIn, s0, s1) || trackFires (p.trigIn, s0, s1)
                || (p.trigIn == miOtherEoc && eocLast[other]))
                g.trigger();
            if (g.phase == Gen::Idle && p.cycle) g.trigger();

            const double rateMul = std::exp2 (p.xmod * gens[other].out * 3.0f);
            const double riseS = std::max (0.001, p.sync ? mathsTimeSteps (p.rise) * stepSec : mathsTimeSeconds (p.rise));
            const double fallS = std::max (0.001, p.sync ? mathsTimeSteps (p.fall) * stepSec : mathsTimeSeconds (p.fall));

            if (g.phase == Gen::Rising)
            {
                // a rise starting from a raised level is proportionally shorter, as on MATHS
                g.u += dt * rateMul / (riseS * std::max (0.02f, 1.0f - g.from));
                g.out = g.from + (1.0f - g.from) * shape (g.u, p.curve);
                if (g.u >= 1.0)
                {
                    g.out = 1.0f;
                    g.phase = Gen::Falling;
                    g.u = 0;
                    events.push ({ s0, (uint8_t) (c == 0 ? mtAEor : mtBEor) });
                    eorCount[c].fetch_add (1, std::memory_order_relaxed);
                }
            }
            else if (g.phase == Gen::Falling)
            {
                g.u += dt * rateMul / fallS;
                g.out = 1.0f - shape (g.u, p.curve);
                if (g.u >= 1.0)
                {
                    g.out = 0.0f;
                    g.phase = Gen::Idle;
                    g.u = 0;
                    eocNow[c] = true;
                    events.push ({ s0, (uint8_t) (c == 0 ? mtAEoc : mtBEoc) });
                    eocCount[c].fetch_add (1, std::memory_order_relaxed);
                    if (p.cycle) g.trigger();
                }
            }
            g.scaled = g.out * p.level;
        }
        eocLast[0] = eocNow[0];
        eocLast[1] = eocNow[1];

        if (++scopeTick >= 32)
        {
            scopeTick = 0;
            const int w = scopeWrite.load (std::memory_order_relaxed);
            scope[0][w].store (gens[0].out, std::memory_order_relaxed);
            scope[1][w].store (gens[1].out, std::memory_order_relaxed);
            scopeWrite.store ((w + 1) % kScope, std::memory_order_release);
        }
    }
}
} // namespace gm
