#pragma once
#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace gm
{
// The MATHS tab: two function generators, a scope, four modulation slots and the pulse->track map.
class MathsPanel : public juce::Component
{
public:
    explicit MathsPanel (GeomancerProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh(); // editor timer
    std::function<void (juce::String)> onStatus;

private:
    struct Channel
    {
        Knob rise { "Rise", col::accent }, fall { "Fall", col::accent }, curve { "Curve", col::accent },
            xmod { "X-Mod", col::maths }, level { "Level", col::accent };
        Toggle cycle { "CYCLE" }, sync { "SYNC", col::accent2 };
        Chip trig { "Trig in" };
    };
    struct Slot
    {
        Chip src, trk, dst;
        juce::Slider amt;
        std::unique_ptr<juce::SliderParameterAttachment> att;
    };
    void loadPreset (int which);

    GeomancerProcessor& proc;
    Channel ch[2];
    Slot slots[kNumModSlots];
    Toggle on { "MATHS ON", col::maths }, quant { "QUANTIZE", col::accent2 };
    Btn presets;
    Chip pulse[kNumTracks];
    juce::Rectangle<int> scopeArea { 354, 6, 404, 146 };
    uint32_t lastEoc[2] {}, lastEor[2] {};
    double flash[4] {};
};

// SETUP tab: sequencer routing, MIDI, randomness lock, kit tools, and the cheat sheet.
class SetupPanel : public juce::Component
{
public:
    explicit SetupPanel (GeomancerProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onKitChanged;
    std::function<void (juce::String)> onStatus;

private:
    void loadKitFolder();
    void relink();
    GeomancerProcessor& proc;
    Toggle seqSound { "SEQ PLAYS SOUNDS" }, midiOut { "SEQ -> MIDI OUT", col::accent2 },
        patNotes { "NOTES 0-15 = PATTERNS" }, lock { "LOCK RANDOMNESS", col::maths };
    Chip seed { "Seed" };
    Btn kitBtn, clearKitBtn, relinkBtn, demoBtn, builtinBtn;
    std::unique_ptr<juce::FileChooser> chooser; // dies with the panel, cancelling an open dialog
};
} // namespace gm
