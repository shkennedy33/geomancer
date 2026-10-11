#pragma once
#include "Voice.h"
#include "Params.h"

namespace gm
{
struct Event
{
    enum Type : uint8_t { Choke = 0, Release = 1, Hit = 2 }; // order = priority at equal offsets
    int offset = 0;
    uint8_t type = Hit;
    float vel = 1;
    float pitch = 0;         // step pitch, semitones
    double gate = -1;        // samples; < 0 = held until Release
    uint32_t key = 0;        // randomness key for per-hit sound variation
};

// Continuous modulation from MATHS, refreshed per block.
struct TrackMod
{
    float pitch = 0, cutoffOct = 0, decayMul = 1, levelDb = 0, pan = 0, start = 0, driveDb = 0, density = 0;
};

class TrackEngine
{
public:
    void bind (juce::AudioProcessorValueTreeState& s, int trackIndex);
    void prepare (double sr);
    void readParams (float globalRndSnd, float globalRndSeq) noexcept; // once per block

    FixedVec<Event, 512> events;
    void addEvent (const Event& e) noexcept { events.push (e); }
    // Renders this block's voices + bus FX into L/R (which are cleared first). Returns false if silent.
    bool render (float* L, float* R, int n, SamplePool& pool) noexcept;

    int chokeGroup() const noexcept { return choke; }
    int output() const noexcept { return out; }
    int note() const noexcept { return midiNote; }
    bool muted() const noexcept { return isMuted; }
    bool soloed() const noexcept { return isSolo; }
    float rndSeq() const noexcept { return rndSeqAmt; }
    int mathsTrig() const noexcept { return mTrig; }
    bool isSounding() const noexcept;
    float meter() const noexcept { return peak; }

    TrackMod mod;
    std::atomic<uint32_t> hitCounter { 0 };

private:
    void handle (const Event& e, SamplePool& pool) noexcept;
    void startVoice (SampleData* s, const Event& e) noexcept;

    int index = 0;
    double sr = 44100;
    std::atomic<float>* p[tpCount] {};
    VoiceParams vp;
    Voice voices[kVoicesPerTrack + 2];
    uint32_t serial = 0;
    Rng rng;

    int choke = 0, out = 0, midiNote = 36, mTrig = 0;
    bool isMuted = false, isSolo = false, mono = false;
    float rndSndAmt = 0, rndSeqAmt = 0;
    float bits = 16, down = 1, driveDb = 0, satMix = 1;
    int satType = 0;

    Crusher crusher;
    Saturator satL, satR;
    float dcxL = 0, dcyL = 0, dcxR = 0, dcyR = 0, dcR = 0.9995f;
    Smoother gainL, gainR, driveGain, mix;
    float peak = 0;
    bool busWarm = false; // bus FX still ringing (crusher hold / DC blocker) after voices end
    bool firstBlock = true;
};
} // namespace gm
