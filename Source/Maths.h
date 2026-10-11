#pragma once
// MATHS-inspired emergent rhythm: two rise/fall function generators. Each one can self-cycle
// (becoming an LFO/clock), be triggered by the clock, by the other's end-of-cycle, or by a drum
// track, and can speed the other up or slow it down (cross-mod). Their end-of-rise / end-of-cycle
// pulses fire drum tracks; their levels modulate drum parameters. Small changes give rhythms that
// shift and evolve.
#include <atomic>
#include "DSP.h"
#include "Params.h"

namespace gm
{
struct MathsChannelParams
{
    float rise = 0.3f, fall = 0.4f, curve = 0;
    bool cycle = true, sync = true;
    int trigIn = 0;
    float xmod = 0, level = 1;
};

struct MathsEvent
{
    int offset;
    uint8_t type; // MTrig values: mtAEor, mtAEoc, mtBEor, mtBEoc
};

class Maths
{
public:
    static constexpr int kTick = 16;
    static constexpr int kScope = 256;

    void reset() noexcept;
    // trackHits[t] lists sample offsets this block where track t was hit (for "Track N" trigger inputs)
    void process (int numSamples, double sr, double ppq0, double bpm, double barLen,
                  const MathsChannelParams (&ch)[2], const FixedVec<int, 64> (&trackHits)[kNumTracks],
                  FixedVec<MathsEvent, 256>& events) noexcept;

    float value (int src) const noexcept; // ModSrc
    float out (int ch) const noexcept { return gens[ch].out; }

    // GUI scope (written by the audio thread, read by the editor)
    std::atomic<float> scope[2][kScope];
    std::atomic<int> scopeWrite { 0 };
    std::atomic<uint32_t> eocCount[2] {}, eorCount[2] {};

private:
    struct Gen
    {
        enum Phase { Idle, Rising, Falling } phase = Idle;
        double u = 0;
        float out = 0, from = 0, scaled = 0;
        void trigger() noexcept
        {
            if (phase == Rising) return; // like MATHS: a rise can't be interrupted
            phase = Rising;
            from = out;
            u = 0;
        }
    };
    static float shape (double u, float c) noexcept;

    Gen gens[2];
    bool eocLast[2] { false, false }; // EOC on the previous tick (feeds "Other cycle" inputs)
    int scopeTick = 0;
};
} // namespace gm
