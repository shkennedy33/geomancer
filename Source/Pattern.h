#pragma once
// Pattern data. Every step is packed into one std::atomic<uint64_t>, so the GUI thread can edit
// while the audio thread reads without locks or torn steps.
#include <atomic>
#include <array>
#include <cstdint>
#include <juce_core/juce_core.h>
#include "DSP.h"

namespace gm
{
//==============================================================================
// Trig conditions (Elektron-ish): A:B plays on loop A of every B loops.
enum Cond : uint8_t
{
    CondAlways = 0,
    Cond1of2, Cond2of2, Cond1of3, Cond2of3, Cond3of3, Cond1of4, Cond2of4, Cond3of4, Cond4of4,
    CondFill, CondNotFill, CondFirst, CondNotFirst,
    CondCount
};
const char* condName (int c);

struct Step
{
    bool on = false;
    uint8_t cond = CondAlways;
    uint8_t vel = 100;     // 1..127
    uint8_t gate = 8;      // sixteenths of a step: 16 = 100%, 1..64
    uint8_t prob = 100;    // 0..100 %
    uint8_t ratchet = 1;   // 1..8 hits inside the step
    int8_t pitch = 0;      // -24..24 semitones
    int8_t nudge = 0;      // -50..50 % of a step (micro timing)

    uint64_t pack() const noexcept
    {
        return (uint64_t) ((on ? 1u : 0u) | ((uint32_t) cond << 1))
             | ((uint64_t) vel << 8) | ((uint64_t) gate << 16) | ((uint64_t) prob << 24)
             | ((uint64_t) ratchet << 32) | ((uint64_t) (uint8_t) pitch << 40)
             | ((uint64_t) (uint8_t) nudge << 48);
    }
    static Step unpack (uint64_t v) noexcept
    {
        Step s;
        s.on = (v & 1) != 0;
        s.cond = (uint8_t) ((v >> 1) & 0x7f);
        s.vel = (uint8_t) (v >> 8);
        s.gate = (uint8_t) (v >> 16);
        s.prob = (uint8_t) (v >> 24);
        s.ratchet = (uint8_t) (v >> 32);
        s.pitch = (int8_t) (uint8_t) (v >> 40);
        s.nudge = (int8_t) (uint8_t) (v >> 48);
        return s.sanitised();
    }
    Step sanitised() const noexcept
    {
        Step s = *this;
        s.cond = (uint8_t) std::min<int> (s.cond, CondCount - 1);
        s.vel = (uint8_t) std::clamp<int> (s.vel, 1, 127);
        s.gate = (uint8_t) std::clamp<int> (s.gate, 1, 64);
        s.prob = (uint8_t) std::clamp<int> (s.prob, 0, 100);
        s.ratchet = (uint8_t) std::clamp<int> (s.ratchet, 1, 8);
        s.pitch = (int8_t) std::clamp<int> (s.pitch, -24, 24);
        s.nudge = (int8_t) std::clamp<int> (s.nudge, -50, 50);
        return s;
    }
};

//==============================================================================
enum Direction : uint8_t { DirFwd = 0, DirRev, DirPingPong, DirRandom, DirCount };
const char* dirName (int d);

// Step divisions, as quarter-note (ppq) lengths.
constexpr int kNumDivisions = 12;
const char* divName (int d);
double divPpq (int d);
constexpr int kDefaultDiv = 7; // 1/16

struct Lane // per-track sequencing settings, stored per pattern
{
    uint8_t length = 16;    // 1..64
    uint8_t div = kDefaultDiv;
    uint8_t dir = DirFwd;
    int8_t swing = 0;       // -25..25, added to global swing (%)

    uint32_t pack() const noexcept
    {
        return (uint32_t) length | ((uint32_t) div << 8) | ((uint32_t) dir << 16) | ((uint32_t) (uint8_t) swing << 24);
    }
    static Lane unpack (uint32_t v) noexcept
    {
        Lane l;
        l.length = (uint8_t) std::clamp<int> ((int) (v & 0xff), 1, kMaxSteps);
        l.div = (uint8_t) std::clamp<int> ((int) ((v >> 8) & 0xff), 0, kNumDivisions - 1);
        l.dir = (uint8_t) std::clamp<int> ((int) ((v >> 16) & 0xff), 0, DirCount - 1);
        l.swing = (int8_t) std::clamp<int> ((int8_t) (uint8_t) (v >> 24), -25, 25);
        return l;
    }
};

//==============================================================================
struct Pattern
{
    std::atomic<uint64_t> steps[kNumTracks][kMaxSteps];
    std::atomic<uint32_t> lanes[kNumTracks];

    Pattern() { clear(); }
    void clear() noexcept;
    void clearTrack (int t) noexcept;

    Step step (int t, int s) const noexcept { return Step::unpack (steps[t][s].load (std::memory_order_relaxed)); }
    void setStep (int t, int s, const Step& st) noexcept { steps[t][s].store (st.sanitised().pack(), std::memory_order_relaxed); }
    Lane lane (int t) const noexcept { return Lane::unpack (lanes[t].load (std::memory_order_relaxed)); }
    void setLane (int t, const Lane& l) noexcept { lanes[t].store (l.pack(), std::memory_order_relaxed); }

    // Loop length used by chain mode: the longest track that has any step on, rounded up to whole bars.
    double lengthPpq (double barLen) const noexcept;
    void copyFrom (const Pattern& o) noexcept;
    void copyTrackFrom (int t, const Pattern& o, int srcTrack) noexcept;
    bool isEmpty() const noexcept;
};

// Plain (non-atomic) snapshot of one pattern, used for undo/clipboard on the message thread.
struct PatternSnapshot
{
    uint64_t steps[kNumTracks][kMaxSteps];
    uint32_t lanes[kNumTracks];
    void take (const Pattern& p) noexcept;
    void restore (Pattern& p) const noexcept;
};

struct PatternBank
{
    Pattern patterns[kNumPatterns];

    juce::MemoryBlock serialise() const;           // gzip'd binary
    bool deserialise (const juce::MemoryBlock& mb);
};

//==============================================================================
// Generators (message thread). All write into one track of a pattern.
namespace gen
{
    void euclid (Pattern& p, int track, int hits, int steps, int rotate, uint8_t vel = 100);
    void random (Pattern& p, int track, float density, Rng& rng, bool accents = true);
    // Geomancy: casts four random Mother figures, derives the whole shield chart (16 figures x 4 rows
    // = 64 bits) and writes it as steps. Returns the name of the Judge.
    juce::String geomancy (Pattern& p, int track, Rng& rng);
    // Casts one chart and deals its figures across all tracks; returns the Judge's name.
    juce::String geomancyKit (Pattern& p, Rng& rng);
    void mutate (Pattern& p, int track, float amount, Rng& rng);
    void shift (Pattern& p, int track, int by);
    void reverse (Pattern& p, int track);
    const char* figureName (uint8_t bits);
}
} // namespace gm
