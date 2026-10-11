#pragma once
#include "Sample.h"

namespace gm
{
// Sound parameters of a track as the voices see them (refreshed once per block).
struct VoiceParams
{
    float tune = 0;            // semitones incl. fine + continuous modulation
    bool stretchMode = false;
    float stretch = 1, grainMs = 50;
    bool reverse = false;
    float start = 0, end = 1;
    float attackMs = 0, holdMs = 0, decayMs = 10000;
    bool decayInf = true, gateMode = false;
    float pAmt = 0, pDecMs = 60;
    int fType = 0;
    float cutoff = 20000, reso = 0.1f, fEnv = 0, fDecMs = 200;
    float velSens = 0.75f;
};

// Per-hit deviations (randomizer, MATHS sample-and-hold, step pitch).
struct HitMods
{
    float pitch = 0;       // semitones
    float cutoffOct = 0;
    float decayMul = 1;
    float gain = 1;
    float pan = 0;         // -1..1
    float startAdd = 0;    // fraction of sample length
    bool flipReverse = false;
    float stretchMul = 1;
};

struct Grain { double pos = 0, inc = 0; int age = 0, len = 1; bool on = false; };

class Voice
{
public:
    void prepare (double sr) noexcept { hostSR = sr; }
    bool isActive() const noexcept { return active; }
    bool isDying() const noexcept { return killing || stage == Release; }
    uint32_t serial = 0;    // trigger order, for voice stealing

    void start (SampleData* s, const VoiceParams& p, const HitMods& m, float velocity, double gateSamples) noexcept;
    void release() noexcept;          // gate off (MIDI note-off)
    void kill (float ms = 3.0f) noexcept; // choke / steal: fast fade
    void render (float* L, float* R, int n, const VoiceParams& live) noexcept;
    float level() const noexcept { return env * killGain; }

private:
    enum Stage { Attack, Hold, Sustain, Decay, Release, Done };
    void updateControl (const VoiceParams& live) noexcept;
    void finish() noexcept;
    inline void readFrame (double pos, float& l, float& r) const noexcept;
    double alignGrain (double target, const Grain& prev) const noexcept;

    double hostSR = 44100;
    SampleData::Ptr smp;
    bool active = false, killing = false, stretchMode = false, stereo = false;
    int dirSign = 1;
    double startPos = 0, endPos = 0, srcPerOut = 1;

    // repitch playback
    double pos = 0, inc = 1;
    // granular playback
    Grain grains[4];
    int lastGrain = 0;
    double head = 0, headInc = 1;
    int hop = 0, grainLen = 2048;
    bool headDone = false;

    // envelopes
    Stage stage = Done;
    bool gateMode = false, held = false;
    float env = 0, attackInc = 1, decayCoef = 1, releaseCoef = 0.999f;
    int holdLeft = 0;
    double gateLeft = 0;
    float pEnv = 0, pEnvCoef16 = 1, fEnvVal = 0, fEnvCoef16 = 1;
    float killGain = 1, killStep = 0;
    int declick = 0; static constexpr int kDeclick = 32;

    // per-hit
    HitMods mods;
    float velGain = 1, gainL = 1, gainR = 1;

    // filter
    SVF svf[2];
    int fType = 0, ctrl = 0;
    float ratio = 1;
};
} // namespace gm
