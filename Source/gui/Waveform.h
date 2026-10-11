#pragma once
#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace gm
{
// Waveform of the selected track's sample. Drag the start/end flags; click anywhere else to audition.
class Waveform : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget
{
public:
    explicit Waveform (GeomancerProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("gmfile:"); }
    void itemDropped (const SourceDetails& d) override { filesDropped ({ d.description.toString().fromFirstOccurrenceOf ("gmfile:", false, false) }, 0, 0); }
    std::function<void()> onLoaded;

private:
    float xOf (float frac) const { return 4.0f + frac * ((float) getWidth() - 8.0f); }
    float fracOf (int x) const { return juce::jlimit (0.0f, 1.0f, ((float) x - 4.0f) / ((float) getWidth() - 8.0f)); }
    GeomancerProcessor& proc;
    int dragging = 0; // 1 = start, 2 = end
    juce::RangedAudioParameter* dragParam = nullptr; // fixed at mouseDown: the selected track may change mid-drag
};
} // namespace gm
