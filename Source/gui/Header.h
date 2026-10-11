#pragma once
#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace gm
{
// Logo + cast glyph, transport, the 16 pattern slots, pattern tools, global/master knobs.
class Header : public juce::Component
{
public:
    explicit Header (GeomancerProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh(); // called by the editor timer

    std::function<void()> onPatternEdited;  // pattern contents changed by a tool here
    std::function<void()> onViewChanged;    // view pattern changed
    juce::String status;                    // last oracle / message shown under the logo
    uint8_t figure = 0b1010;                // geomantic figure drawn in the logo

private:
    void castKit();
    void showDiceMenu();
    juce::Rectangle<int> patternRect (int i) const;
    juce::Rectangle<int> glyphRect() const { return { 14, 14, 30, 46 }; }

    GeomancerProcessor& proc;
    Btn playBtn, copyBtn, pasteBtn, clearBtn, diceBtn, undoBtn, redoBtn;
    Toggle fillBtn, clipBtn;
    Chip quant, clock, looper;
    Knob swing, rndSnd, rndSeq, djFilter, djReso, drive, master;
    Chip satType;
    Knob bpm;
    std::unique_ptr<PatternSnapshot> clipboard;
    int shownCurrent = -1, shownQueued = -2, shownView = -1;
    bool shownRunning = false;
    float meter = 0;
};
} // namespace gm
