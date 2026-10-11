#include "PluginEditor.h"

using namespace gm;

GeomancerEditor::GeomancerEditor (GeomancerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), content (*this), header (p), grid (p), lanes (p), sound (p), maths (p), setup (p), browser (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.setBounds (0, 0, kW, kH);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &grid, &lanes, &sound, &maths, &setup, &browser })
        content.addChildComponent (c);
    header.setBounds (0, 0, kW, 72);
    grid.setBounds (0, 76, kW, StepGrid::kTop + kNumTracks * StepGrid::kRowH);
    lanes.setBounds (0, 498, kW, 102);
    for (auto* c : std::initializer_list<juce::Component*> { &sound, &maths, &setup, &browser })
        c->setBounds (0, 628, kW, 218);
    header.setVisible (true);
    grid.setVisible (true);
    lanes.setVisible (true);

    auto patternChanged = [this] { proc.markDirty(); grid.repaint(); lanes.repaint(); header.repaint(); };
    header.onPatternEdited = [this, patternChanged] { patternChanged(); setStatus (header.status); };
    header.onViewChanged = [this] { grid.repaint(); lanes.repaint(); };
    grid.onSelectionChanged = [this] { selectionChanged(); };
    grid.onEdited = [this] { lanes.repaint(); header.repaint(); };
    grid.onStatus = [this] { setStatus (grid.status); };
    lanes.onEdited = [this] { grid.repaint(); header.repaint(); };
    lanes.onStatus = [this] { setStatus (lanes.status); };
    sound.onSampleChanged = [this] { grid.repaint(); };
    sound.onStatus = [this] (juce::String s) { setStatus (s); };
    maths.onStatus = [this] (juce::String s) { setStatus (s); };
    setup.onStatus = [this] (juce::String s) { setStatus (s); };
    setup.onKitChanged = [this] { grid.repaint(); lanes.repaint(); header.repaint(); sound.refresh(); };

    browser.onStatus = [this] (juce::String s) { setStatus (s); };
    browser.onLoaded = [this] { grid.repaint(); sound.refresh(); sound.repaint(); };
    grid.onBrowse = [this] (int t) {
        auto* s = proc.samples.slot (t);
        openBrowser (s != nullptr && s->file.existsAsFile() ? s->file : juce::File (proc.sampleDir()));
    };
    sound.onBrowse = [this] {
        auto* s = proc.samples.slot (proc.selectedTrack);
        openBrowser (s != nullptr && s->file.existsAsFile() ? s->file : juce::File (proc.sampleDir()));
    };
    setTab (proc.bottomTab);
    setResizable (true, true);
    setResizeLimits (kW * 6 / 10, kH * 6 / 10, kW * 2, kH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / kH);
    setSize (kW, kH);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

GeomancerEditor::~GeomancerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GeomancerEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

void GeomancerEditor::resized()
{
    const float scale = (float) getWidth() / (float) kW;
    content.setTransform (juce::AffineTransform::scale (scale));
}

void GeomancerEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    static const char* names[] = { "SOUND", "MATHS", "SETUP", "BROWSE" };
    for (int i = 0; i < 4; ++i)
    {
        auto r = ed.tabRect (i).toFloat();
        const bool on = ed.proc.bottomTab == i;
        g.setColour (on ? col::panel : col::bg);
        g.fillRoundedRectangle (r.withBottom (r.getBottom() + 6.0f), 4.0f);
        g.setColour (on ? (i == 1 ? col::maths : col::accent) : col::dim);
        g.setFont (uiFont (11.5f, true).withExtraKerningFactor (0.12f));
        g.drawText (names[i], r, juce::Justification::centred);
    }
    g.setColour (col::dim);
    g.setFont (uiFont (12.0f));
    g.drawText (ed.status, 356, 604, 916, 22, juce::Justification::centredLeft);
}

void GeomancerEditor::Content::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 4; ++i)
        if (ed.tabRect (i).contains (e.getPosition()))
            ed.setTab (i);
}

void GeomancerEditor::setTab (int t)
{
    proc.bottomTab = std::clamp (t, 0, 3);
    sound.setVisible (proc.bottomTab == 0);
    maths.setVisible (proc.bottomTab == 1);
    setup.setVisible (proc.bottomTab == 2);
    browser.setVisible (proc.bottomTab == 3);
    if (proc.bottomTab != 3) proc.stopPreview();
    content.repaint();
}

void GeomancerEditor::selectionChanged()
{
    lanes.repaint();
    grid.repaint();
    if (sound.isDragging()) return; // the timer rebinds once the knob is let go
    sound.setTrack (proc.selectedTrack);
    sound.refresh();
    shownTrack = proc.selectedTrack;
}

void GeomancerEditor::setStatus (const juce::String& s)
{
    if (s == status) return;
    status = s;
    content.repaint (356, 604, 916, 22);
}

void GeomancerEditor::timerCallback()
{
    header.refresh();
    grid.refresh();
    if (maths.isVisible()) maths.refresh();
    const auto sv = proc.samples.version.load();
    if (sv != shownSamples)
    {
        shownSamples = sv;
        sound.refresh();
        sound.repaint();
    }
    if (proc.selectedTrack != shownTrack) selectionChanged(); // e.g. a preset / state restore moved it
    if (proc.viewPattern != shownView)
    {
        shownView = proc.viewPattern;
        grid.repaint();
        lanes.repaint();
    }
}

void GeomancerEditor::openBrowser (const juce::File& at)
{
    setTab (3);
    browser.openAt (at);
}

void GeomancerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    setStatus (loadFiles (proc, files, proc.selectedTrack));
    grid.repaint();
    sound.refresh();
    sound.repaint();
}

bool GeomancerEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (k == juce::KeyPress::spaceKey)
    {
        auto* p = proc.param (gid::play);
        setParam (p, p->getValue() > 0.5f ? 0.0f : 1.0f);
        return true;
    }
    if (mods.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (mods.isShiftDown() ? proc.redo() : proc.undo()) { grid.repaint(); lanes.repaint(); header.repaint(); }
        return true;
    }
    if (mods.isCommandDown() && (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y'))
    {
        if (proc.redo()) { grid.repaint(); lanes.repaint(); header.repaint(); }
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::downKey))
    {
        proc.selectedTrack = std::clamp (proc.selectedTrack + (k.isKeyCode (juce::KeyPress::upKey) ? -1 : 1), 0, kNumTracks - 1);
        selectionChanged();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::rightKey))
    {
        proc.page = std::clamp (proc.page + (k.isKeyCode (juce::KeyPress::leftKey) ? -1 : 1), 0, kMaxSteps / StepGrid::kCols - 1);
        grid.repaint();
        lanes.repaint();
        return true;
    }
    return false;
}
