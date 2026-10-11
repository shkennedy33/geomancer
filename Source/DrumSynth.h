#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace gm
{
// A small analog-flavoured drum kit rendered from code, so a fresh instance makes sound at once.
// Samples are referenced in saved state as "synth:<name>" and re-rendered on load.
namespace drumsynth
{
    const juce::StringArray& names();                      // kick, snare, ... (default track order)
    bool render (const juce::String& name, double sr, juce::AudioBuffer<float>& out);
}
} // namespace gm
