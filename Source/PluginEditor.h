#pragma once
#include "PluginProcessor.h"
#include "gui/Header.h"
#include "gui/StepGrid.h"
#include "gui/LaneEditor.h"
#include "gui/SoundPanel.h"
#include "gui/MathsPanel.h"
#include "gui/Browser.h"

class GeomancerEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer,
                        public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    static constexpr int kW = 1280, kH = 848;
    explicit GeomancerEditor (GeomancerProcessor&);
    ~GeomancerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    // files dropped anywhere outside the grid go to the selected track (folders fill tracks downward)
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void openBrowser (const juce::File& at);

private:
    // Everything is laid out at a fixed 1280x848 inside `content`, which is scaled to the window.
    struct Content : juce::Component
    {
        explicit Content (GeomancerEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        GeomancerEditor& ed;
    };
    void timerCallback() override;
    void setTab (int t);
    void selectionChanged();
    void setStatus (const juce::String& s);
    juce::Rectangle<int> tabRect (int i) const { return { 8 + i * 84, 604, 80, 22 }; }

    GeomancerProcessor& proc;
    gm::LnF lnf;
    Content content;
    gm::Header header;
    gm::StepGrid grid;
    gm::LaneEditor lanes;
    gm::SoundPanel sound;
    gm::MathsPanel maths;
    gm::SetupPanel setup;
    gm::Browser browser;
    juce::TooltipWindow tooltips { this, 600 };
    juce::String status;
    uint32_t shownSamples = 0;
    int shownView = -1, shownTrack = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeomancerEditor)
};
