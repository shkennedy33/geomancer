#pragma once
#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace gm
{
// Drag descriptions from the browser look like "gmfile:/path/to/sample.wav".
inline const juce::String kFileDragPrefix = "gmfile:";

// BROWSE tab: places (home, /, every drive under /mnt, /media, /run/media) + favourites, a folder
// listing with a filter, and click / arrow-key auditioning. Enter or double-click loads into the
// selected track; drag a file onto any row of the grid.
class Browser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit Browser (GeomancerProcessor& p);
    ~Browser() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void openAt (const juce::File& dirOrFile); // jump to a folder (selecting the file if given one)
    void refresh() { repaint (previewArea); }
    std::function<void (juce::String)> onStatus;
    std::function<void()> onLoaded;

private:
    struct Entry { juce::File file; bool folder; bool up; };
    struct Place { juce::String name; juce::File dir; bool favourite; };

    // files list
    int getNumRows() override { return (int) entries.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { activate (row); }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

    struct PlacesModel : juce::ListBoxModel
    {
        Browser& b;
        explicit PlacesModel (Browser& o) : b (o) {}
        int getNumRows() override { return (int) b.places.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    };

    void navigate (const juce::File& dir, const juce::File& select = {});
    void rebuild();
    void buildPlaces();
    void activate (int row);   // folder: enter; file: load into the selected track
    void audition (int row);
    void loadInto (const juce::File& f, int track);
    void toggleFavourite();
    juce::StringArray favourites() const;

    GeomancerProcessor& proc;
    std::unique_ptr<juce::PropertiesFile> settings;   // ~/.config/Geomancer: favourites, last folder
    juce::File dir;
    std::vector<Entry> entries;
    std::vector<Place> places;
    PlacesModel placesModel { *this };
    juce::ListBox placeList { "places", nullptr }, fileList { "files", nullptr };
    juce::TextEditor path, filter;
    Btn upBtn, loadBtn, kitBtn, favBtn, stopBtn, autoBtn;
    bool autoPreview = true;
    juce::File previewing;
    juce::Rectangle<int> previewArea { 904, 4, 368, 106 };
};
} // namespace gm
