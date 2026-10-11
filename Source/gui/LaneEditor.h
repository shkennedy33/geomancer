#pragma once
#include "StepGrid.h"

namespace gm
{
// Per-step parameter lanes for the selected track, column-aligned with the step grid above,
// plus a column of track tools on the right.
class LaneEditor : public juce::Component
{
public:
    enum LaneKind { lkVel, lkGate, lkProb, lkRatchet, lkPitch, lkNudge, lkCond, lkCount };
    explicit LaneEditor (GeomancerProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onEdited, onStatus;
    juce::String status;

private:
    static constexpr int kBarTop = 4, kBarH = 92;
    juce::Rectangle<int> kindRect (int k) const { return { 8 + (k % 2) * 104, 4 + (k / 2) * 23, 100, 20 }; }
    int stepAt (int x) const;
    int valueFromY (int y) const;
    void setValue (int s, int v);
    int getValue (const Step& st) const;
    void range (int& lo, int& hi) const;
    bool bipolar() const { return proc.laneKind == lkPitch || proc.laneKind == lkNudge; }
    juce::String valueText (int v) const;
    Pattern& pat() { return proc.bank.patterns[proc.viewPattern]; }

    GeomancerProcessor& proc;
    juce::OwnedArray<Btn> tools;
    int lastStep = -1, lastVal = 0;
    bool editing = false;
};
} // namespace gm
