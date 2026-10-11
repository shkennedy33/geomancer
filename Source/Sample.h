#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "DSP.h"

namespace gm
{
// One loaded audio file. Immutable after construction, shared between the pool (message thread),
// the per-track slot, and any voices currently playing it.
struct SampleData : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> buffer; // 1 or 2 channels
    double sampleRate = 44100.0;
    juce::File file;
    juce::String name;
    juce::String source;             // what state saves: a file path, or "synth:<name>" for the built-in kit
    std::vector<float> peaks;        // min/max pairs for the waveform display
    juce::uint32 retiredAt = 0;      // ms timestamp when it stopped being current anywhere

    int length() const noexcept { return buffer.getNumSamples(); }
    const float* chan (int c) const noexcept { return buffer.getReadPointer (std::min (c, buffer.getNumChannels() - 1)); }
    void buildPeaks (int bins = 600);
};

// Owns every loaded sample and the 16 track slots. The audio thread only ever reads a slot's raw
// pointer and wraps it in a Ptr (refcount bump, never a delete: the pool always holds a reference).
// A timer on the message thread frees samples that have been out of use for a few seconds.
constexpr int kPreviewSlot = kNumTracks; // the browser's audition sample

class SamplePool : private juce::Timer
{
public:
    SamplePool();
    ~SamplePool() override;

    SampleData::Ptr load (const juce::File& f, juce::String& error, double maxSeconds = 60.0);
    SampleData::Ptr loadSource (const juce::String& source, juce::String& error); // path or "synth:<name>"
    bool loadIntoSlot (int track, const juce::File& f, juce::String& error);
    bool loadSourceIntoSlot (int track, const juce::String& source, juce::String& error);
    static bool isSynth (const juce::String& source) { return source.startsWith ("synth:"); }
    void clearSlot (int track);
    SampleData* slot (int track) const noexcept { return slots[(size_t) track].load (std::memory_order_acquire); }
    void setSlot (int track, SampleData::Ptr s);

    juce::AudioFormatManager& formats() noexcept { return formatManager; }
    juce::String wildcard() const { return formatManager.getWildcardForAllFormats(); }
    static bool isAudioFile (const juce::File& f);

    std::atomic<uint32_t> version { 0 }; // bumps whenever a slot changes (the editor polls it)

private:
    void timerCallback() override;

    juce::AudioFormatManager formatManager;
    juce::ReferenceCountedArray<SampleData> pool;
    std::array<std::atomic<SampleData*>, kNumTracks + 1> slots {}; // + kPreviewSlot
    juce::CriticalSection poolLock; // message-thread-only users (load vs GC), never the audio thread
};
} // namespace gm
