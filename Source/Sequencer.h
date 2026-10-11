#pragma once
#include "Pattern.h"

namespace gm
{
struct Transport
{
    bool playing = false;
    double ppq = 0;           // quarter notes at block start
    double bpm = 120;
    double barStart = 0;      // ppq of the bar containing ppq (host-provided when available)
    double barLen = 4;        // in ppq
    bool hasSampleTime = false;
    int64_t timeInSamples = 0; // host sample clock: continuity is judged on this, not on ppq (tempo changes)
};

struct SeqHit
{
    int offset;
    int track;
    float vel;
    float pitch;
    double gate;     // samples
    uint32_t key;
};

// Per-block inputs that come from parameters / modulation.
struct SeqConfig
{
    int quant = 2;                      // Instant, Beat, Bar, 2 Bars, 4 Bars
    float swing = 50;                   // percent
    bool fill = false;
    uint32_t seed = 1;
    bool lock = false;                  // true: randomness repeats every pass (render == playback)
    float rndSeq[kNumTracks] {};        // effective (track ∪ global)
    float density[kNumTracks] {};       // MATHS density modulation, -1..1
    int chain[kNumPatterns] {};         // chain mode: patterns played in order, each for its own length
    int chainLen = 0;                   // < 2 = chain off
    double loopLen = 0;                 // looper (beat repeat) length in ppq, 0 = off
};

// Stateless-per-step scheduler: every step's time is computed from (pattern start, step index), so
// a step evaluates identically in whichever block it lands. Loops, seeks and offline renders in
// REAPER need no special handling beyond detecting the jump.
class Sequencer
{
public:
    void requestPattern (int p, double atPpq, const Transport& tr, int quant) noexcept;

    void process (const Transport& tr, int numSamples, double sr, const PatternBank& bank,
                  const SeqConfig& cfg, FixedVec<SeqHit, 2048>& out) noexcept;

    int current() const noexcept { return currentPattern; }
    int queued() const noexcept { return queuedPattern; }
    int chainPosition() const noexcept { return chainPos; }
    bool running() const noexcept { return wasPlaying; }
    bool looping() const noexcept { return loopActive > 0; }
    int position (int track) const noexcept { return playPos[track]; }
    uint32_t pass() const noexcept { return passCounter; }
    double patternStart() const noexcept { return patStart; }
    bool jumpedThisBlock() const noexcept { return jumped; }
    void setCurrent (int p) noexcept { currentPattern = p; queuedPattern = -1; }

    static int stepIndex (int64_t n, int len, int dir, uint32_t key) noexcept;

private:
    // Emits the hits of [a, b) in pattern time; `shift` moves them to real time (looper repeats).
    void generate (double a, double b, double shift, double p0, double ppqPerSample, int numSamples, const Pattern& pat,
                   const SeqConfig& cfg, uint32_t seed, FixedVec<SeqHit, 2048>& out) noexcept;
    void emit (double a, double b, double p0, double pps, int numSamples, const Pattern& pat,
               const SeqConfig& cfg, FixedVec<SeqHit, 2048>& out) noexcept;

    int currentPattern = 0, queuedPattern = -1;
    double switchPpq = 0, patStart = 0, expectedPpq = 0;
    bool wasPlaying = false, jumped = false, resetOnSwitch = true;
    uint32_t passCounter = 0, lastSeed = 1;
    int chainPos = 0;
    bool chainAdvance = false;
    double loopActive = 0, loopStart = 0;
    double frontier = 0;                    // pattern time already emitted up to here (carried across blocks)
    int64_t lastSampleTime = 0;
    int lastBlockLen = 0;
    bool hadSampleTime = false;
    int64_t cycleOffset[kNumTracks] {}, lastCycle[kNumTracks] {}; // trig-condition loop counts since play
    int64_t cycleAt (const Pattern& pat, int t, double ppq) const noexcept;
    int playPos[kNumTracks] {};
};
} // namespace gm
