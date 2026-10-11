#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Pattern.h"
#include "Sample.h"
#include "Track.h"
#include "Sequencer.h"
#include "Maths.h"

class GeomancerProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    GeomancerProcessor();
    ~GeomancerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Geomancer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Shared with the editor.
    juce::AudioProcessorValueTreeState apvts;
    gm::SamplePool samples;
    gm::PatternBank bank;
    gm::Maths maths;

    // GUI -> audio
    std::atomic<int> guiPatternRequest { -1 };
    std::array<std::atomic<int>, gm::kNumPatterns> chain {};   // chain mode (shift-click patterns)
    std::atomic<int> chainLen { 0 };
    juce::String chainText() const;
    std::array<std::atomic<int>, gm::kNumTracks> audition {}; // velocity 1..127, 0 = none

    // audio -> GUI
    std::atomic<int> uiCurrentPattern { 0 }, uiQueuedPattern { -1 };
    std::atomic<bool> uiRunning { false }, uiLooping { false };
    std::array<std::atomic<int>, gm::kNumTracks> uiPlayPos {};
    std::array<std::atomic<float>, gm::kNumTracks> uiMeter {};
    std::atomic<float> uiMasterMeter { 0 };
    std::atomic<double> uiBpm { 120 };
    uint32_t hitCount (int t) const noexcept { return tracks[(size_t) t].hitCounter.load (std::memory_order_relaxed); }

    // Message-thread helpers for the editor
    bool loadSample (int track, const juce::File& f, juce::String& error);
    void loadDefaultKit();
    // Browser audition: plays outside the 16 tracks, straight to the main out, after the master FX.
    bool preview (const juce::File& f, juce::String& error);
    void stopPreview() { previewStop = true; }
    void markDirty();
    void pushUndo (int pattern);
    bool undo();
    bool redo();
    // Paths of samples that couldn't be found / the last sample folder. Written on the message thread,
    // read by getStateInformation (which a host may call from any thread), hence the lock.
    juce::String missing (int t) const { const juce::SpinLock::ScopedLockType l (stringLock); return missingSample[t]; }
    void setMissing (int t, const juce::String& s) { const juce::SpinLock::ScopedLockType l (stringLock); missingSample[t] = s; }
    juce::String sampleDir() const { const juce::SpinLock::ScopedLockType l (stringLock); return lastSampleDir; }
    void setSampleDir (const juce::String& s) { const juce::SpinLock::ScopedLockType l (stringLock); lastSampleDir = s; }
    gm::Rng uiRng;

    // Persisted editor state
    int selectedTrack = 0, page = 0, laneKind = 0, viewPattern = 0, bottomTab = 0;

    // Embedding hook (Aeromancer's GEOMANCER module): hears every hit the sequencer, MATHS or MIDI
    // produce this block, whether or not "Sequencer Plays Sounds" is on. Audio thread.
    struct HitListener
    {
        virtual ~HitListener() = default;
        virtual void geomancerHit (int track, int offset, float velocity, float pitch) noexcept = 0;
    };
    HitListener* hitListener = nullptr;

    juce::RangedAudioParameter* param (const juce::String& id) const { return apvts.getParameter (id); }
    juce::RangedAudioParameter* trackParam (int t, int tp) const { return apvts.getParameter (gm::trackParamID (t, tp)); }

private:
    static BusesProperties makeBuses();
    void timerCallback() override;
    gm::Transport computeTransport (int numSamples);
    void addHit (int track, const gm::Event& e) noexcept;
    bool silenced (int track) const noexcept;
    void midiNoteOn (int note, float vel, int offset, double gateSamples) noexcept;
    void flushNoteOffs (int numSamples, bool all) noexcept;
    void runMaths (const gm::Transport& tr, int numSamples, bool midiOut) noexcept;
    void processMaster (float* L, float* R, int n) noexcept;

    std::array<gm::TrackEngine, gm::kNumTracks> tracks;
    gm::Sequencer seq;
    gm::Voice previewVoices[2];          // alternate so a new audition can fade the last one out
    int previewCurrent = 0;
    uint32_t previewSeen = 0;
    std::atomic<uint32_t> previewTrigger { 0 };
    std::atomic<bool> previewStop { false };
    gm::FixedVec<gm::SeqHit, 2048> seqHits;
    gm::FixedVec<gm::MathsEvent, 256> mathsEvents;
    gm::FixedVec<int, 64> trackHitOffsets[gm::kNumTracks];
    gm::TrackMod mathsMod[gm::kNumTracks];
    double mathsPending[gm::kNumTracks] {};
    gm::Rng rng;

    double sr = 44100;
    juce::AudioBuffer<float> scratch;
    int auxChannel[gm::kNumAux] {};

    // transport
    double internalPpq = 0;
    juce::int64 internalSamples = 0;
    bool internalRunning = false, wasRunning = false, fillHeld = false;
    std::atomic<bool> disarmPlay { false };
    int lastPatternParam = -1;
    std::atomic<bool> resyncPatternParam { false }; // set by setStateInformation: adopt, don't act on, the restored value
    juce::String missingSample[gm::kNumTracks], lastSampleDir;
    mutable juce::SpinLock stringLock;
    bool anySolo = false;

    // MIDI out
    struct PendingOff { int note; double left; };
    struct NoteReq { int offset; int note; float vel; double gate; };
    gm::FixedVec<PendingOff, 512> pendingOffs;
    gm::FixedVec<NoteReq, 2048> noteReqs; // collected per block, sent in time order
    juce::MidiBuffer midiOut;

    // master
    gm::SVF djL, djR;
    gm::Saturator mSatL, mSatR;
    gm::Smoother djSmooth, masterGain, mDriveGain, mMix;
    int djMode = 0;
    float mdcxL = 0, mdcyL = 0, mdcxR = 0, mdcyR = 0, mdcR = 0.9995f;

    // cached parameter pointers
    struct G
    {
        std::atomic<float> *master, *djFilter, *djReso, *mDrive, *mSat, *mSatMix, *clip, *swing, *gRndSnd, *gRndSeq,
            *lock, *seed, *clock, *play, *bpm, *pattern, *quant, *seqSound, *midiOut, *patNotes, *fill, *mOn, *mQuant, *looper;
        std::atomic<float>* maths[2][gm::mpCount];
        std::atomic<float>* mod[gm::kNumModSlots][4];
    } g {};

    struct UndoEntry { int pattern; std::unique_ptr<gm::PatternSnapshot> snap; };
    std::vector<UndoEntry> undoStack, redoStack;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeomancerProcessor)
};
