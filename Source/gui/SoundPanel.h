#pragma once
#include "Waveform.h"

namespace gm
{
// Everything about the selected track's sound: sample, warp, envelopes, filter, dirt, output, randomness.
class SoundPanel : public juce::Component
{
public:
    explicit SoundPanel (GeomancerProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setTrack (int t);
    bool isDragging() const; // a knob is mid-drag: rebinding now would orphan its parameter gesture
    void refresh() { waveform.repaint(); }
    std::function<void()> onSampleChanged, onBrowse;
    std::function<void (juce::String)> onStatus;

private:
    struct Group { juce::String name; juce::Rectangle<int> r; };
    void browse (int dir);
    void rollSound();

    GeomancerProcessor& proc;
    Waveform waveform;
    Btn loadBtn, prevBtn, nextBtn, clearBtn, diceBtn;
    Knob tune, fine, start, end, stretch, grain;
    Chip warp;
    Toggle reverse;
    Knob attack, hold, decay, pAmt, pDec;
    Chip mode;
    Knob cutoff, reso, fEnv, fDec;
    Chip fType;
    Knob crush, down, drive, satMix;
    Chip sat;
    Knob level, pan, velSens;
    Chip choke, poly;
    Knob rndSnd, rndSeq;
    Chip out, note, mTrig;
    std::vector<Group> groups;
    int track = -1;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace gm
