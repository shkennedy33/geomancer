#include "Browser.h"
#include "StepGrid.h"

namespace gm
{
static juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Geomancer";
    o.filenameSuffix = "settings";
    o.folderName = "Geomancer";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

static bool naturalLess (const juce::File& a, const juce::File& b)
{
    return a.getFileName().compareNatural (b.getFileName()) < 0;
}

Browser::Browser (GeomancerProcessor& p)
    : proc (p),
      upBtn ("UP", [this] { navigate (dir.getParentDirectory(), dir); }),
      loadBtn ("LOAD", [this] {
          const int row = fileList.getSelectedRow();
          if (row >= 0 && row < (int) entries.size() && ! entries[(size_t) row].folder) loadInto (entries[(size_t) row].file, proc.selectedTrack);
      }, col::accent),
      kitBtn ("FOLDER -> KIT", [this] {
          const auto msg = loadFiles (proc, { dir.getFullPathName() }, proc.selectedTrack);
          if (onStatus) onStatus (msg);
          if (onLoaded) onLoaded();
      }),
      favBtn ("+ FAVOURITE", [this] { toggleFavourite(); }, col::solo),
      stopBtn ("STOP", [this] { proc.stopPreview(); }),
      autoBtn ("AUTO PREVIEW", {}, col::accent2)
{
    settings = std::make_unique<juce::PropertiesFile> (settingsOptions());
    autoPreview = settings->getBoolValue ("autoPreview", true);
    autoBtn.lit = autoPreview;
    autoBtn.onClick = [this] {
        autoPreview = ! autoPreview;
        autoBtn.lit = autoPreview;
        autoBtn.repaint();
        settings->setValue ("autoPreview", autoPreview);
    };
    upBtn.setTooltip ("Parent folder (Backspace)");
    loadBtn.setTooltip ("Load the selected file into the selected track (Enter / double-click)");
    kitBtn.setTooltip ("Load this folder's samples into consecutive tracks, starting at the selected one");
    favBtn.setTooltip ("Pin / unpin this folder in the places list");

    for (auto* lb : { &placeList, &fileList })
    {
        lb->setColour (juce::ListBox::backgroundColourId, col::bg);
        lb->setColour (juce::ListBox::outlineColourId, col::line);
        lb->setOutlineThickness (1);
        lb->setRowHeight (18);
        addAndMakeVisible (lb);
    }
    placeList.setModel (&placesModel);
    fileList.setModel (this);
    fileList.setMultipleSelectionEnabled (false);

    for (auto* te : { &path, &filter })
    {
        te->setFont (uiFont (12.5f));
        te->setColour (juce::TextEditor::outlineColourId, col::line);
        te->setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
        te->setIndents (6, 4);
        addAndMakeVisible (te);
    }
    filter.setTextToShowWhenEmpty ("filter...", col::faint);
    filter.onTextChange = [this] { rebuild(); };
    filter.onReturnKey = [this] { fileList.grabKeyboardFocus(); };
    path.onReturnKey = [this] {
        const juce::File f (path.getText().trim());
        if (f.isDirectory()) navigate (f);
        else if (f.existsAsFile()) navigate (f.getParentDirectory(), f);
        else path.setText (dir.getFullPathName(), false);
    };
    for (auto* b : { &upBtn, &loadBtn, &kitBtn, &favBtn, &stopBtn, &autoBtn })
        addAndMakeVisible (b);

    buildPlaces();
    juce::File start (settings->getValue ("lastFolder"));
    if (! start.isDirectory()) start = juce::File (proc.sampleDir());
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    navigate (start);
}

Browser::~Browser()
{
    proc.stopPreview();
    fileList.setModel (nullptr);
    placeList.setModel (nullptr);
}

juce::StringArray Browser::favourites() const
{
    juce::StringArray f;
    f.addTokens (settings->getValue ("favourites"), "\n", "");
    f.removeEmptyStrings();
    return f;
}

void Browser::buildPlaces()
{
    places.clear();
    places.push_back ({ "Home", juce::File::getSpecialLocation (juce::File::userHomeDirectory), false });
   #if JUCE_LINUX
    places.push_back ({ "/  (root)", juce::File ("/"), false });
   #endif
   #if ! JUCE_LINUX
    juce::Array<juce::File> roots; // Windows: C:\, D:\ ...  macOS: / and every /Volumes/* disk
    juce::File::findFileSystemRoots (roots);
    if (juce::File ("/Volumes").isDirectory()) roots.addArray (juce::File ("/Volumes").findChildFiles (juce::File::findDirectories, false));
    for (auto& r : roots) places.push_back ({ r.getFullPathName(), r, false });
   #endif
    // Linux: drives come from the mount table, not by probing folders: stat-ing an automount point
    // (e.g. a CIFS share whose machine is offline) can freeze the UI for seconds.
    juce::StringArray seen;
    juce::StringArray lines;
    lines.addLines (juce::File ("/proc/mounts").loadFileAsString());
    for (auto& line : lines)
    {
        juce::StringArray f;
        f.addTokens (line, " ", "");
        if (f.size() < 3) continue;
        const auto mp = f[1].replace ("\\040", " ");
        if (! (mp.startsWith ("/mnt/") || mp.startsWith ("/media/") || mp.startsWith ("/run/media/")) || seen.contains (mp)) continue;
        seen.add (mp);
        const bool network = f[2] == "autofs" || f[2].startsWith ("cifs") || f[2].startsWith ("nfs") || f[2].startsWith ("fuse.sshfs");
        places.push_back ({ mp + (network ? "  (network)" : ""), juce::File (mp), false });
    }
    std::sort (places.begin() + std::min<int> (2, (int) places.size()), places.end(), [] (const Place& a, const Place& b) { return a.name.compareNatural (b.name) < 0; });
    for (auto& f : favourites())
        places.push_back ({ juce::File (f).getFileName(), juce::File (f), true });
    placeList.updateContent();
    placeList.repaint();
}

void Browser::navigate (const juce::File& d, const juce::File& select)
{
    if (! d.isDirectory()) return;
    dir = d;
    path.setText (dir.getFullPathName(), false);
    settings->setValue ("lastFolder", dir.getFullPathName());
    favBtn.text = favourites().contains (dir.getFullPathName()) ? "- FAVOURITE" : "+ FAVOURITE";
    favBtn.repaint();
    rebuild();
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].file == select)
        {
            fileList.selectRow (i, false, true);
            fileList.scrollToEnsureRowIsOnscreen (i);
            return;
        }
    fileList.deselectAllRows();
    fileList.scrollToEnsureRowIsOnscreen (0);
}

void Browser::rebuild()
{
    entries.clear();
    if (dir.getParentDirectory() != dir) entries.push_back ({ dir.getParentDirectory(), true, true });
    const auto needle = filter.getText().trim().toLowerCase();
    auto folders = dir.findChildFiles (juce::File::findDirectories, false);
    auto files = dir.findChildFiles (juce::File::findFiles, false);
    std::sort (folders.begin(), folders.end(), naturalLess);
    std::sort (files.begin(), files.end(), naturalLess);
    auto keep = [&needle] (const juce::File& f) {
        return ! f.getFileName().startsWithChar ('.') && (needle.isEmpty() || f.getFileName().toLowerCase().contains (needle));
    };
    for (auto& f : folders)
        if (keep (f)) entries.push_back ({ f, true, false });
    for (auto& f : files)
        if (keep (f) && SamplePool::isAudioFile (f)) entries.push_back ({ f, false, false });
    fileList.updateContent();
    fileList.repaint();
}

void Browser::toggleFavourite()
{
    auto favs = favourites();
    const auto p = dir.getFullPathName();
    if (favs.contains (p)) favs.removeString (p); else favs.add (p);
    settings->setValue ("favourites", favs.joinIntoString ("\n"));
    settings->saveIfNeeded();
    buildPlaces();
    navigate (dir);
}

void Browser::audition (int row)
{
    if (row < 0 || row >= (int) entries.size() || entries[(size_t) row].folder) return;
    juce::String err;
    previewing = entries[(size_t) row].file;
    if (! proc.preview (previewing, err) && onStatus) onStatus (err);
    repaint (previewArea);
}

void Browser::loadInto (const juce::File& f, int track)
{
    juce::String err;
    if (proc.loadSample (track, f, err))
    {
        if (onStatus) onStatus ("T" + juce::String (track + 1) + " <- " + f.getFileName());
        if (onLoaded) onLoaded();
    }
    else if (onStatus) onStatus (err);
}

void Browser::activate (int row)
{
    if (row < 0 || row >= (int) entries.size()) return;
    const auto e = entries[(size_t) row];
    if (e.folder) navigate (e.file, e.up ? dir : juce::File());
    else loadInto (e.file, proc.selectedTrack);
}

void Browser::selectedRowsChanged (int row)
{
    if (autoPreview) audition (row);
}

void Browser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row == fileList.getSelectedRow()) audition (row); // clicking the selected file again replays it
}

void Browser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { activate (row); }

juce::var Browser::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty()) return {};
    const int row = rows[0];
    if (row < 0 || row >= (int) entries.size() || entries[(size_t) row].folder) return {};
    return kFileDragPrefix + entries[(size_t) row].file.getFullPathName();
}

bool Browser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::backspaceKey || k.isKeyCode (juce::KeyPress::leftKey))
    {
        if (filter.hasKeyboardFocus (false) || path.hasKeyboardFocus (false)) return false;
        navigate (dir.getParentDirectory(), dir);
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::rightKey))
    {
        const int row = fileList.getSelectedRow();
        if (row >= 0 && row < (int) entries.size() && entries[(size_t) row].folder && ! entries[(size_t) row].up)
            navigate (entries[(size_t) row].file);
        return true;
    }
    return false;
}

void Browser::openAt (const juce::File& f)
{
    if (f.existsAsFile()) navigate (f.getParentDirectory(), f);
    else if (f.isDirectory()) navigate (f);
    fileList.grabKeyboardFocus();
}

//==============================================================================
void Browser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) entries.size()) return;
    const auto& e = entries[(size_t) row];
    if (selected)
    {
        g.setColour (col::accent.withAlpha (0.25f));
        g.fillRect (0, 0, w, h);
    }
    const bool playing = ! e.folder && e.file == previewing;
    g.setColour (e.folder ? col::accent2 : (playing ? col::accent : col::text.withAlpha (0.88f)));
    g.setFont (uiFont (12.5f, e.folder));
    const juce::String icon = e.up ? "<-" : (e.folder ? "[ ]" : (playing ? ">" : " "));
    g.drawText (icon, 6, 0, 22, h, juce::Justification::centredLeft);
    g.drawText (e.up ? juce::String ("..  (up)") : e.file.getFileName(), 30, 0, w - 110, h, juce::Justification::centredLeft, true);
    if (! e.folder)
    {
        g.setColour (col::faint);
        g.setFont (uiFont (10.5f));
        const auto kb = e.file.getSize() / 1024;
        g.drawText (kb < 1024 ? juce::String (kb) + " KB" : juce::String (kb / 1024.0, 1) + " MB", w - 80, 0, 74, h, juce::Justification::centredRight);
    }
}

void Browser::PlacesModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) b.places.size()) return;
    const auto& pl = b.places[(size_t) row];
    // highlight only the closest place containing the current folder (not "/" as well as /mnt/archive)
    int best = -1, bestLen = -1;
    for (int i = 0; i < (int) b.places.size(); ++i)
    {
        const auto& d = b.places[(size_t) i].dir;
        if ((b.dir == d || b.dir.isAChildOf (d)) && d.getFullPathName().length() > bestLen)
        {
            best = i;
            bestLen = d.getFullPathName().length();
        }
    }
    const bool here = row == best;
    if (here)
    {
        g.setColour (col::raised);
        g.fillRect (0, 0, w, h);
    }
    g.setColour (pl.favourite ? col::solo : (here ? col::text : col::dim));
    g.setFont (uiFont (12.0f, here));
    g.drawText ((pl.favourite ? "* " : "") + pl.name, 8, 0, w - 12, h, juce::Justification::centredLeft, true);
}

void Browser::PlacesModel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) b.places.size()) b.navigate (b.places[(size_t) row].dir);
    b.placeList.repaint();
}

void Browser::resized()
{
    placeList.setBounds (8, 4, 190, 210);
    path.setBounds (206, 4, 470, 24);
    upBtn.setBounds (680, 4, 44, 24);
    filter.setBounds (728, 4, 166, 24);
    fileList.setBounds (206, 32, 688, 182);
    loadBtn.setBounds (904, 134, 120, 26);
    kitBtn.setBounds (1028, 134, 120, 26);
    stopBtn.setBounds (1152, 134, 120, 26);
    autoBtn.setBounds (904, 166, 120, 26);
    favBtn.setBounds (1028, 166, 120, 26);
}

void Browser::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    auto r = previewArea.toFloat();
    g.setColour (col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    auto* s = proc.samples.slot (kPreviewSlot);
    if (s != nullptr && ! s->peaks.empty() && previewing.existsAsFile())
    {
        const int bins = (int) s->peaks.size() / 2, w = previewArea.getWidth() - 8;
        float pk = 0.001f;
        for (auto v : s->peaks) pk = std::max (pk, std::abs (v));
        const float mid = r.getCentreY() - 6.0f, half = (r.getHeight() - 24.0f) * 0.5f / pk;
        g.setColour (col::accent.withAlpha (0.85f));
        for (int px = 0; px < w; ++px)
        {
            const int b = px * bins / w;
            g.drawVerticalLine (previewArea.getX() + 4 + px, mid - s->peaks[(size_t) b * 2 + 1] * half, mid - s->peaks[(size_t) b * 2] * half + 1.0f);
        }
        g.setColour (col::dim);
        g.setFont (uiFont (11.0f));
        g.drawFittedText (previewing.getFileName() + "   " + juce::String (s->length() / s->sampleRate, 2) + " s  "
                              + juce::String (s->sampleRate / 1000.0, 1) + " kHz  " + (s->buffer.getNumChannels() > 1 ? "stereo" : "mono"),
                          previewArea.reduced (6, 2), juce::Justification::bottomLeft, 1);
    }
    else
    {
        g.setColour (col::faint);
        g.setFont (uiFont (12.0f));
        g.drawFittedText ("Click or arrow through files to hear them. Enter / double-click loads into the selected track; "
                          "drag a file onto any row of the grid.",
                          previewArea.reduced (12), juce::Justification::centred, 3);
    }
    g.setColour (col::track (proc.selectedTrack));
    g.setFont (uiFont (11.5f, true));
    g.drawText ("loads into T" + juce::String (proc.selectedTrack + 1) + "   (Backspace = up, -> = open folder)",
                904, 114, 368, 16, juce::Justification::centredLeft);
}
} // namespace gm
