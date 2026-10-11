#pragma once
#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace gm
{
// Track tools shared by the grid's context menu and the lane editor's tool buttons.
enum TrackTool { ttClearSteps = 1, ttRandom, ttCast, ttMutate, ttShiftL, ttShiftR, ttReverse, ttCopy, ttPaste, ttEuclid,
                 ttLoad, ttClearSample, ttAudition, ttLen8 = 100 }; // ttLen8 + k: length presets
// patternIndex < 0 = the pattern in view now; chooser = where a Load dialog is held (owner-scoped lifetime)
juce::String runTrackTool (GeomancerProcessor& p, int track, int tool, juce::Component* anchor, std::function<void()> changed,
                           int patternIndex = -1, std::unique_ptr<juce::FileChooser>* chooser = nullptr);
void chooseSample (GeomancerProcessor& p, int track, std::unique_ptr<juce::FileChooser>& holder, juce::Component* owner,
                   std::function<void()> done);

// Audio files from dropped paths (folders searched, subfolders too if the top has no audio), natural order.
juce::Array<juce::File> collectAudio (const juce::StringArray& paths);
// Loads them into consecutive tracks from firstTrack; returns a status message.
juce::String loadFiles (GeomancerProcessor& p, const juce::StringArray& paths, int firstTrack);

class StepGrid : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget
{
public:
    static constexpr int kCols = 32, kRowH = 25, kTop = 20, kGridX = 222, kCellW = 28, kLaneX = 1124;
    explicit StepGrid (GeomancerProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int, int y) override { dropRow = rowAt (y); repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropRow = -1; repaint(); }
    // drags from the browser tab
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("gmfile:"); }
    void itemDragMove (const SourceDetails& d) override { dropRow = rowAt (d.localPosition.y); repaint(); }
    void itemDragExit (const SourceDetails&) override { dropRow = -1; repaint(); }
    void itemDropped (const SourceDetails& d) override;
    std::function<void (int track)> onBrowse;

    void refresh(); // editor timer: playheads + trigger LEDs
    std::function<void()> onSelectionChanged, onEdited, onStatus;
    juce::String status;
    std::unique_ptr<juce::FileChooser> chooser;

private:
    enum Kind { kNone, kPage, kRuler, kName, kLed, kMute, kSolo, kCell, kEnd, kLen, kDiv, kDir, kSwg };
    struct Hit { Kind kind = kNone; int track = -1; int step = -1; int page = -1; };
    Hit hitTest (juce::Point<int> p) const;
    int rowAt (int y) const { const int r = (y - kTop) / kRowH; return (y >= kTop && r < kNumTracks) ? r : -1; }
    juce::Rectangle<int> rowRect (int t) const { return { 0, kTop + t * kRowH, getWidth(), kRowH - 1 }; }
    juce::Rectangle<int> cellRect (int t, int c) const { return { kGridX + c * kCellW, kTop + t * kRowH, kCellW - 2, kRowH - 2 }; }
    Pattern& pat() { return proc.bank.patterns[proc.viewPattern]; }
    void stepMenu (int t, int s);
    void trackMenu (int t);
    void select (int t);
    void edited (bool final = true);
    void paintRow (juce::Graphics& g, int t);
    // x of the end-of-track bracket on the current page, or -1 if the track ends on another page
    int endX (int len) const { const int c = len - proc.page * kCols; return (c >= 0 && c <= kCols) ? kGridX + c * kCellW - 1 : -1; }
    int stepAtX (int x) const { return proc.page * kCols + std::clamp ((x - kGridX) / kCellW, 0, kCols - 1); }
    void setLengths (int len, int onlyTrack); // onlyTrack < 0 = every track (= the pattern's length)

    GeomancerProcessor& proc;
    Hit down, hover;
    bool paintOn = false, velDrag = false, dirtyPending = false;
    int dragStartVal = 0, lastPaintStep = -1, dropRow = -1;
    uint32_t lastHits[kNumTracks] {};
    double hitTime[kNumTracks] {};
    int shownPos[kNumTracks] {};
    uint32_t shownSamples = 0;
};
} // namespace gm
