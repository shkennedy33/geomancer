#include "StepGrid.h"

namespace gm
{
//==============================================================================
// Euclid editor that pops out of the track tools: live-updates the track as you drag.
namespace
{
class EuclidPanel : public juce::Component
{
public:
    EuclidPanel (GeomancerProcessor& p, int t, int patternIndex, std::function<void()> changed)
        : proc (p), track (t), pattern (patternIndex), onChange (std::move (changed))
    {
        const auto& pat = proc.bank.patterns[pattern];
        int on = 0;
        const int len = pat.lane (t).length;
        for (int s = 0; s < len; ++s) on += pat.step (t, s).on ? 1 : 0;
        setup (hits, "Hits", 0, kMaxSteps, std::max (on, 4));
        setup (steps, "Steps", 1, kMaxSteps, len);
        setup (rotate, "Rotate", 0, 63, 0);
        setSize (230, 110);
        proc.pushUndo (pattern);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        for (auto* s : { &hits, &steps, &rotate })
            s->setBounds (r.removeFromTop (30).withTrimmedLeft (56));
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (col::dim);
        g.setFont (uiFont (12.0f, true));
        auto r = getLocalBounds().reduced (8);
        for (auto* n : { "HITS", "STEPS", "ROTATE" })
            g.drawText (n, r.removeFromTop (30).withWidth (54), juce::Justification::centredLeft);
    }

private:
    void setup (juce::Slider& s, const char*, int lo, int hi, int v)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 20);
        s.setRange (lo, hi, 1);
        s.setValue (v, juce::dontSendNotification);
        s.setColour (juce::Slider::trackColourId, col::accent);
        s.onValueChange = [this] { apply(); };
        addAndMakeVisible (s);
    }
    void apply()
    {
        const int st = (int) steps.getValue();
        hits.setRange (0, st, 1);
        rotate.setRange (0, std::max (1, st - 1), 1);
        gen::euclid (proc.bank.patterns[pattern], track, (int) hits.getValue(), st, (int) rotate.getValue());
        if (onChange) onChange();
    }
    GeomancerProcessor& proc;
    int track, pattern;
    std::function<void()> onChange;
    juce::Slider hits, steps, rotate;
};

struct TrackClip
{
    uint64_t steps[kMaxSteps] {};
    uint32_t lane = 0;
    bool valid = false;
};
TrackClip trackClip;
} // namespace

void chooseSample (GeomancerProcessor& p, int track, std::unique_ptr<juce::FileChooser>& holder, juce::Component* owner,
                   std::function<void()> done)
{
    // The chooser lives in `holder`, a member of the launching component: if the editor closes while the
    // dialog is up, destroying it cancels the dialog and its callback can never run on freed objects.
    auto start = p.sampleDir().isNotEmpty() ? juce::File (p.sampleDir()) : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    holder = std::make_unique<juce::FileChooser> ("Load sample into track " + juce::String (track + 1), start, p.samples.wildcard());
    juce::Component::SafePointer<juce::Component> safe (owner);
    holder->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [&p, track, safe, done] (const juce::FileChooser& fc) {
                              if (safe == nullptr) return;
                              const auto f = fc.getResult();
                              if (f == juce::File()) return;
                              juce::String err;
                              if (! p.loadSample (track, f, err))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Geomancer", err);
                              if (done) done();
                          });
}

juce::String runTrackTool (GeomancerProcessor& p, int t, int tool, juce::Component* anchor, std::function<void()> changed,
                           int patternIndex, std::unique_ptr<juce::FileChooser>* chooser)
{
    const int pi = patternIndex >= 0 ? patternIndex : p.viewPattern;
    auto& pat = p.bank.patterns[pi];
    const auto name = "T" + juce::String (t + 1);
    auto done = [&] (juce::String s) { if (changed) changed(); return s; };
    switch (tool)
    {
        case ttLoad: if (chooser != nullptr) chooseSample (p, t, *chooser, anchor, changed); return {};
        case ttClearSample: p.samples.clearSlot (t); p.setMissing (t, {}); p.markDirty(); return done (name + " sample cleared");
        case ttAudition: p.audition[(size_t) t] = 110; return {};
        case ttEuclid:
            if (anchor != nullptr)
            {
                // parented to the editor, so the box dies with it instead of outliving it on the desktop
                auto* top = anchor->getTopLevelComponent();
                juce::CallOutBox::launchAsynchronously (std::make_unique<EuclidPanel> (p, t, pi, changed),
                                                        top->getLocalArea (anchor, anchor->getLocalBounds()), top);
            }
            return {};
        case ttCopy:
            for (int s = 0; s < kMaxSteps; ++s) trackClip.steps[s] = pat.steps[t][s].load();
            trackClip.lane = pat.lanes[t].load();
            trackClip.valid = true;
            return name + " copied";
        default: break;
    }
    p.pushUndo (pi);
    switch (tool)
    {
        case ttClearSteps: { const auto l = pat.lane (t); pat.clearTrack (t); pat.setLane (t, l); return done (name + " cleared"); }
        case ttRandom: gen::random (pat, t, 0.35f, p.uiRng); return done (name + " randomized");
        case ttCast: return done (name + " Judge: " + gen::geomancy (pat, t, p.uiRng));
        case ttMutate: gen::mutate (pat, t, 0.4f, p.uiRng); return done (name + " mutated");
        case ttShiftL: gen::shift (pat, t, -1); return done ({});
        case ttShiftR: gen::shift (pat, t, 1); return done ({});
        case ttReverse: gen::reverse (pat, t); return done (name + " reversed");
        case ttPaste:
            if (! trackClip.valid) return {};
            for (int s = 0; s < kMaxSteps; ++s) pat.steps[t][s].store (trackClip.steps[s]);
            pat.lanes[t].store (trackClip.lane);
            return done (name + " pasted");
        default:
            if (tool >= ttLen8)
            {
                static const int lens[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 24, 32, 48, 64, 96, 128 };
                auto l = pat.lane (t);
                l.length = (uint8_t) lens[std::clamp (tool - ttLen8, 0, 18)];
                pat.setLane (t, l);
                return done (name + " length " + juce::String (l.length));
            }
    }
    return {};
}

//==============================================================================
StepGrid::StepGrid (GeomancerProcessor& p) : proc (p)
{
    for (auto& s : shownPos) s = -2;
    setRepaintsOnMouseActivity (false);
}

void StepGrid::select (int t)
{
    if (t < 0 || t == proc.selectedTrack) return;
    proc.selectedTrack = t;
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void StepGrid::edited (bool final)
{
    // during a drag, mark the host project dirty once on mouse-up instead of on every move
    if (final) proc.markDirty(); else dirtyPending = true;
    repaint();
    if (onEdited) onEdited();
}

void StepGrid::setLengths (int len, int onlyTrack)
{
    len = std::clamp (len, 1, kMaxSteps);
    for (int t = 0; t < kNumTracks; ++t)
    {
        if (onlyTrack >= 0 && t != onlyTrack) continue;
        auto l = pat().lane (t);
        l.length = (uint8_t) len;
        pat().setLane (t, l);
    }
    status = (onlyTrack >= 0 ? "T" + juce::String (onlyTrack + 1) : juce::String ("all tracks")) + " length " + juce::String (len)
           + " steps  (steps past the end are kept, just not played)";
    if (onStatus) onStatus();
    edited (false);
}

StepGrid::Hit StepGrid::hitTest (juce::Point<int> p) const
{
    Hit h;
    if (p.y < kTop)
    {
        if (p.x >= 8 && p.x < 216) { h.kind = kPage; h.page = std::clamp ((p.x - 8) / 52, 0, kMaxSteps / kCols - 1); }
        else if (p.x >= kGridX && p.x < kGridX + kCols * kCellW) { h.kind = kRuler; h.step = stepAtX (p.x); }
        return h;
    }
    h.track = rowAt (p.y);
    if (h.track < 0) return h;
    const int x = p.x;
    if (x < 8) return h;
    if (x < 26) h.kind = kLed;
    else if (x < 162) h.kind = kName;
    else if (x < 188) h.kind = kMute;
    else if (x < 216) h.kind = kSolo;
    else if (const int ex = endX (proc.bank.patterns[proc.viewPattern].lane (h.track).length); ex > 0 && x >= ex - 3 && x <= ex + 4)
        h.kind = kEnd; // the end-of-track bracket: drag it sideways
    else if (x >= kGridX && x < kGridX + kCols * kCellW) { h.kind = kCell; h.step = stepAtX (x); }
    else if (x >= kLaneX && x < kLaneX + 36) h.kind = kLen;
    else if (x >= kLaneX + 36 && x < kLaneX + 82) h.kind = kDiv;
    else if (x >= kLaneX + 82 && x < kLaneX + 118) h.kind = kDir;
    else if (x >= kLaneX + 118 && x < kLaneX + 154) h.kind = kSwg;
    return h;
}

//==============================================================================
void StepGrid::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // ruler: page selector + step numbers
    for (int pg = 0; pg < kMaxSteps / kCols; ++pg)
    {
        auto r = juce::Rectangle<float> (8.0f + pg * 52.0f, 1.0f, 49.0f, 16.0f);
        const bool on = proc.page == pg;
        g.setColour (on ? col::raised.brighter (0.15f) : col::panel);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? col::text : col::dim);
        g.setFont (uiFont (11.0f, true));
        g.drawText (juce::String (pg * kCols + 1) + "-" + juce::String ((pg + 1) * kCols), r, juce::Justification::centred);
    }
    g.setFont (uiFont (10.0f, true));
    const int selLen = proc.bank.patterns[proc.viewPattern].lane (proc.selectedTrack).length;
    for (int c = 0; c < kCols; ++c)
    {
        const int s = proc.page * kCols + c;
        const auto r = juce::Rectangle<int> (kGridX + c * kCellW, 2, kCellW - 2, 15);
        if (hover.kind == kRuler && hover.step == s)
        {
            g.setColour (col::accent.withAlpha (0.35f));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
        }
        g.setColour (s + 1 == selLen ? col::accent : ((s % 4) == 0 ? col::text : col::faint));
        g.drawText (juce::String (s + 1), r, juce::Justification::centred);
    }
    g.setColour (col::dim);
    g.setFont (uiFont (10.0f, true));
    const char* lanes[] = { "LEN", "DIV", "DIR", "SWG" };
    const int lx[] = { 0, 36, 82, 118 }, lw[] = { 36, 46, 36, 36 };
    for (int i = 0; i < 4; ++i)
        g.drawText (lanes[i], kLaneX + lx[i], 3, lw[i], 14, juce::Justification::centred);

    for (int t = 0; t < kNumTracks; ++t)
        if (g.clipRegionIntersects (rowRect (t))) // most frames only a few rows (LEDs, playheads) are dirty
            paintRow (g, t);
}

void StepGrid::paintRow (juce::Graphics& g, int t)
{
    const auto& pt = proc.bank.patterns[proc.viewPattern];
    const auto lane = pt.lane (t);
    const auto tc = col::track (t);
    const int y = kTop + t * kRowH;
    const bool sel = t == proc.selectedTrack;
    const bool muted = proc.trackParam (t, tpMute)->getValue() > 0.5f;
    const bool solo = proc.trackParam (t, tpSolo)->getValue() > 0.5f;

    // header
    auto hdr = juce::Rectangle<float> (8.0f, (float) y, 208.0f, (float) kRowH - 2.0f);
    g.setColour (sel ? col::raised : col::panel);
    g.fillRoundedRectangle (hdr, 3.0f);
    if (dropRow == t)
    {
        g.setColour (col::accent);
        g.drawRoundedRectangle (hdr, 3.0f, 1.5f);
    }
    g.setColour (tc.withAlpha (sel ? 1.0f : 0.7f));
    g.fillRoundedRectangle (hdr.withWidth (3.0f), 1.5f);

    const double since = juce::Time::getMillisecondCounterHiRes() - hitTime[t];
    const float flash = (float) std::exp (-since / 90.0);
    g.setColour (col::cell.interpolatedWith (tc, std::max (0.0f, flash)));
    g.fillEllipse (13.0f, (float) y + 8.0f, 8.0f, 8.0f);

    juce::String name;
    auto* smp = proc.samples.slot (t);
    bool missing = false;
    if (smp != nullptr) name = smp->name;
    else if (const auto m = proc.missing (t); m.isNotEmpty()) { name = "missing: " + juce::File (m).getFileName(); missing = true; }
    else name = "drop a sample";
    g.setFont (uiFont (12.0f, sel));
    g.setColour (missing ? col::mute : (smp != nullptr ? (sel ? col::text : col::text.withAlpha (0.85f)) : col::faint));
    g.drawFittedText (juce::String (t + 1) + "  " + name, 28, y, 132, kRowH - 2, juce::Justification::centredLeft, 1, 0.8f);

    auto ms = [&] (int x, const char* txt, bool on, juce::Colour c) {
        auto r = juce::Rectangle<float> ((float) x, (float) y + 3.0f, 22.0f, (float) kRowH - 8.0f);
        g.setColour (on ? c : col::cell);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? col::bg : col::dim);
        g.setFont (uiFont (10.5f, true));
        g.drawText (txt, r, juce::Justification::centred);
    };
    ms (164, "M", muted, col::mute);
    ms (190, "S", solo, col::solo);

    // steps
    const int playPos = (proc.uiRunning.load() && proc.uiCurrentPattern.load() == proc.viewPattern) ? proc.uiPlayPos[(size_t) t].load() : -1;
    for (int c = 0; c < kCols; ++c)
    {
        const int s = proc.page * kCols + c;
        auto r = cellRect (t, c).toFloat();
        const bool inLen = s < lane.length;
        const bool group = ((s / 4) % 2) == 0;
        const auto st = pt.step (t, s);
        g.setColour (inLen ? (group ? col::cell : col::cellAlt) : col::panel.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 2.5f);
        if (st.on && ! inLen) // past the end: kept, but not played
        {
            g.setColour (tc.withAlpha (0.3f));
            g.drawRoundedRectangle (r.reduced (2.0f), 2.0f, 1.5f);
        }
        if (st.on && inLen)
        {
            const float v = (float) st.vel / 127.0f;
            auto fillC = tc.withMultipliedBrightness (0.45f + 0.55f * v).withAlpha (muted ? 0.35f : 1.0f);
            auto fr = r.reduced (1.5f);
            if (st.prob < 100) // probability = how much of the cell is filled
            {
                g.setColour (fillC.withAlpha (0.28f));
                g.fillRoundedRectangle (fr, 2.0f);
                fr = fr.withTop (fr.getBottom() - fr.getHeight() * std::max (0.15f, st.prob / 100.0f));
            }
            g.setColour (fillC);
            g.fillRoundedRectangle (fr, 2.0f);
            g.setColour (col::bg.withAlpha (0.75f));
            if (st.ratchet > 1)
                for (int k = 0; k < st.ratchet; ++k)
                    g.fillRect (r.getX() + 3.0f + k * (r.getWidth() - 6.0f) / st.ratchet, r.getBottom() - 5.0f,
                                std::max (1.5f, (r.getWidth() - 6.0f) / st.ratchet - 1.5f), 2.5f);
            g.setFont (uiFont (9.0f, true));
            if (st.cond != CondAlways)
                g.drawText (condName (st.cond), r.withHeight (11.0f), juce::Justification::centred);
            else if (st.pitch != 0)
                g.drawText ((st.pitch > 0 ? "+" : "") + juce::String (st.pitch), r.withHeight (11.0f), juce::Justification::centred);
            if (st.nudge != 0)
            {
                const float nx = r.getCentreX() + (float) st.nudge / 100.0f * r.getWidth();
                g.fillRect (nx - 1.0f, r.getBottom() - 9.0f, 2.0f, 3.0f);
            }
        }
        if (s == playPos)
        {
            g.setColour (col::text.withAlpha (0.9f));
            g.drawRoundedRectangle (r.expanded (0.5f), 2.5f, 1.6f);
        }
        if (hover.kind == kCell && hover.track == t && hover.step == s)
        {
            g.setColour (col::text.withAlpha (0.18f));
            g.fillRoundedRectangle (r, 2.5f);
        }
    }

    // end-of-track bracket
    if (const int ex = endX (lane.length); ex > 0)
    {
        const bool hot = (hover.kind == kEnd && hover.track == t) || (down.kind == kEnd && down.track == t);
        g.setColour (hot ? col::text : tc.withAlpha (0.85f));
        g.fillRoundedRectangle ((float) ex - 1.5f, (float) y - 1.0f, 3.0f, (float) kRowH, 1.5f);
    }

    // lane settings
    auto box = [&] (int x, int w, juce::String txt, bool hot) {
        auto r = juce::Rectangle<float> ((float) (kLaneX + x), (float) y + 1.0f, (float) w - 2.0f, (float) kRowH - 4.0f);
        g.setColour (hot ? col::raised.brighter (0.1f) : col::panel);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (col::text.withAlpha (0.85f));
        g.setFont (uiFont (11.0f, true));
        g.drawText (txt, r, juce::Justification::centred);
    };
    auto hot = [&] (Kind k) { return hover.track == t && hover.kind == k; };
    box (0, 36, juce::String (lane.length), hot (kLen));
    box (36, 46, divName (lane.div), hot (kDiv));
    box (82, 36, dirName (lane.dir), hot (kDir));
    box (118, 36, lane.swing == 0 ? juce::String ("-") : (lane.swing > 0 ? "+" : "") + juce::String (lane.swing), hot (kSwg));
}

//==============================================================================
void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    down = hitTest (e.getPosition());
    velDrag = false;
    lastPaintStep = -1;
    const int t = down.track;
    switch (down.kind)
    {
        case kPage: proc.page = down.page; repaint(); if (onEdited) onEdited(); break;
        case kRuler:
            proc.pushUndo (proc.viewPattern);
            setLengths (down.step + 1, e.mods.isShiftDown() ? proc.selectedTrack : -1);
            break;
        case kEnd:
            select (t);
            proc.pushUndo (proc.viewPattern);
            break;
        case kLed: select (t); proc.audition[(size_t) t] = 110; break;
        case kName:
            select (t);
            if (e.mods.isPopupMenu()) trackMenu (t);
            break;
        case kMute: case kSolo:
        {
            auto* prm = proc.trackParam (t, down.kind == kMute ? tpMute : tpSolo);
            setParam (prm, prm->getValue() > 0.5f ? 0.0f : 1.0f);
            repaint (rowRect (t));
            break;
        }
        case kCell:
        {
            select (t);
            if (e.mods.isPopupMenu()) { stepMenu (t, down.step); break; }
            proc.pushUndo (proc.viewPattern);
            auto st = pat().step (t, down.step);
            if (e.mods.isShiftDown() && st.on) // shift-drag = velocity
            {
                velDrag = true;
                dragStartVal = st.vel;
                break;
            }
            paintOn = ! st.on;
            st.on = paintOn;
            pat().setStep (t, down.step, st);
            lastPaintStep = down.step;
            edited();
            break;
        }
        case kLen: case kSwg:
            proc.pushUndo (proc.viewPattern);
            dragStartVal = down.kind == kLen ? pat().lane (t).length : pat().lane (t).swing;
            break;
        case kDiv: case kDir:
        {
            juce::PopupMenu m;
            const auto l = pat().lane (t);
            if (down.kind == kDiv)
                for (int d = 0; d < kNumDivisions; ++d) m.addItem (d + 1, divName (d), true, d == l.div);
            else
                for (int d = 0; d < DirCount; ++d)
                    m.addItem (d + 1, juce::StringArray { "Forward", "Reverse", "Ping-pong", "Random" }[d], true, d == l.dir);
            const bool isDiv = down.kind == kDiv;
            const int pi = proc.viewPattern;
            juce::Component::SafePointer<StepGrid> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (e.x, e.y, 1, 1))),
                             [safe, t, isDiv, pi] (int r) {
                                 if (safe == nullptr || r == 0) return;
                                 auto& pat = safe->proc.bank.patterns[pi];
                                 safe->proc.pushUndo (pi);
                                 auto l2 = pat.lane (t);
                                 if (isDiv) l2.div = (uint8_t) (r - 1); else l2.dir = (uint8_t) (r - 1);
                                 pat.setLane (t, l2);
                                 safe->edited();
                             });
            break;
        }
        default: break;
    }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    const int t = down.track;
    const int dy = e.getDistanceFromDragStartY();
    if (down.kind == kCell)
    {
        if (velDrag)
        {
            auto st = pat().step (t, down.step);
            st.vel = (uint8_t) std::clamp (dragStartVal - dy, 1, 127);
            pat().setStep (t, down.step, st);
            status = "velocity " + juce::String (st.vel);
            if (onStatus) onStatus();
            edited (false);
            return;
        }
        const int x = e.x;
        if (x < kGridX || x >= kGridX + kCols * kCellW) return;
        const int s = proc.page * kCols + (x - kGridX) / kCellW;
        if (s == lastPaintStep) return;
        // fill every step between the last painted one and this one (fast drags skip cells)
        const int a = lastPaintStep < 0 ? s : lastPaintStep;
        for (int k = std::min (a, s); k <= std::max (a, s); ++k)
        {
            auto st = pat().step (t, k);
            if (st.on != paintOn) { st.on = paintOn; pat().setStep (t, k, st); }
        }
        lastPaintStep = s;
        edited (false);
    }
    else if (down.kind == kEnd)
    {
        const int len = std::clamp (proc.page * kCols + juce::roundToInt ((float) (e.x - kGridX) / (float) kCellW), 1, kMaxSteps);
        if (len != pat().lane (t).length) setLengths (len, t);
    }
    else if (down.kind == kRuler)
    {
        const int len = stepAtX (e.x) + 1;
        if (len != pat().lane (proc.selectedTrack).length) setLengths (len, e.mods.isShiftDown() ? proc.selectedTrack : -1);
    }
    else if (down.kind == kLen || down.kind == kSwg)
    {
        auto l = pat().lane (t);
        if (down.kind == kLen) l.length = (uint8_t) std::clamp (dragStartVal - dy / 5, 1, kMaxSteps);
        else l.swing = (int8_t) std::clamp (dragStartVal - dy / 5, -25, 25);
        pat().setLane (t, l);
        edited (false);
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    velDrag = false;
    down = {};
    if (dirtyPending) { dirtyPending = false; proc.markDirty(); }
}

void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.getPosition());
    if (h.kind != hover.kind || h.track != hover.track || h.step != hover.step)
    {
        const int oldT = hover.track;
        hover = h;
        if (oldT >= 0) repaint (rowRect (oldT));
        if (h.track >= 0) repaint (rowRect (h.track));
        repaint (0, 0, getWidth(), kTop); // ruler hover
        setMouseCursor (h.kind == kEnd ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        if (h.kind == kRuler)
        {
            status = "click: every track's length = " + juce::String (h.step + 1) + " steps   (shift-click: just T"
                   + juce::String (proc.selectedTrack + 1) + ")";
            if (onStatus) onStatus();
        }
        else if (h.kind == kEnd)
        {
            status = "drag the bracket to change T" + juce::String (h.track + 1) + "'s length (steps past it are kept)";
            if (onStatus) onStatus();
        }
        if (h.kind == kCell)
        {
            const auto st = pat().step (h.track, h.step);
            status = "T" + juce::String (h.track + 1) + " step " + juce::String (h.step + 1)
                   + (st.on ? "  vel " + juce::String (st.vel) + "  prob " + juce::String (st.prob) + "%  ratchet "
                                  + juce::String (st.ratchet) + "  " + condName (st.cond)
                            : juce::String ("  (click to set, shift-drag = velocity, right-click = options)"));
            if (onStatus) onStatus();
        }
    }
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
    const int oldT = hover.track;
    hover = {};
    if (oldT >= 0) repaint (rowRect (oldT));
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.getPosition());
    if (h.kind == kName && onBrowse) onBrowse (h.track);
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto h = hitTest (e.getPosition());
    const int dir = (w.deltaY > 0 ? 1 : -1) * (w.isReversed ? -1 : 1);
    if (h.kind == kCell)
    {
        auto st = pat().step (h.track, h.step);
        if (! st.on) return;
        st.vel = (uint8_t) std::clamp (st.vel + dir * 8, 1, 127);
        pat().setStep (h.track, h.step, st);
        edited();
    }
    else if (h.kind == kLen || h.kind == kSwg)
    {
        auto l = pat().lane (h.track);
        if (h.kind == kLen) l.length = (uint8_t) std::clamp (l.length + dir, 1, kMaxSteps);
        else l.swing = (int8_t) std::clamp (l.swing + dir, -25, 25);
        pat().setLane (h.track, l);
        edited();
    }
}

void StepGrid::stepMenu (int t, int s)
{
    const auto st = pat().step (t, s);
    juce::PopupMenu m, vel, rat, prob, cond, gate, pitch, nudge;
    for (int v : { 127, 110, 100, 80, 60, 40, 20 }) vel.addItem (1000 + v, juce::String (v), true, st.vel == v);
    for (int r = 1; r <= 8; ++r) rat.addItem (2000 + r, juce::String (r) + (r == 1 ? " (none)" : "x"), true, st.ratchet == r);
    for (int p : { 100, 90, 75, 50, 33, 25, 10 }) prob.addItem (3000 + p, juce::String (p) + "%", true, st.prob == p);
    for (int c = 0; c < CondCount; ++c) cond.addItem (4000 + c, condName (c), true, st.cond == c);
    for (int gt : { 2, 4, 8, 12, 16, 32, 64 }) gate.addItem (5000 + gt, juce::String (gt * 100 / 16) + "%", true, st.gate == gt);
    for (int p : { 12, 7, 5, 3, 2, 1, 0, -1, -2, -3, -5, -7, -12 }) pitch.addItem (6100 + p, (p > 0 ? "+" : "") + juce::String (p), true, st.pitch == p);
    for (int n : { -40, -25, -10, 0, 10, 25, 40 }) nudge.addItem (7100 + n, (n > 0 ? "+" : "") + juce::String (n) + "%", true, st.nudge == n);
    m.addSectionHeader ("Track " + juce::String (t + 1) + " · step " + juce::String (s + 1));
    m.addSubMenu ("Velocity", vel);
    m.addSubMenu ("Ratchet", rat);
    m.addSubMenu ("Probability", prob);
    m.addSubMenu ("Condition", cond);
    m.addSubMenu ("Gate", gate);
    m.addSubMenu ("Pitch", pitch);
    m.addSubMenu ("Nudge", nudge);
    m.addSeparator();
    m.addItem (9001, st.on ? "Turn off" : "Turn on");
    m.addItem (9002, "Reset step to defaults");
    juce::Component::SafePointer<StepGrid> safe (this);
    const int pi = proc.viewPattern;
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, t, s, pi] (int r) {
        if (safe == nullptr || r == 0) return;
        auto& pat = safe->proc.bank.patterns[pi];
        safe->proc.pushUndo (pi);
        auto st2 = pat.step (t, s);
        if (r >= 7000 && r < 8000) st2.nudge = (int8_t) (r - 7100);
        else if (r >= 6000) st2.pitch = (int8_t) (r - 6100);
        else if (r >= 5000) st2.gate = (uint8_t) (r - 5000);
        else if (r >= 4000) st2.cond = (uint8_t) (r - 4000);
        else if (r >= 3000) st2.prob = (uint8_t) (r - 3000);
        else if (r >= 2000) st2.ratchet = (uint8_t) (r - 2000);
        else if (r >= 1000) st2.vel = (uint8_t) (r - 1000);
        if (r >= 1000 && r < 9000) st2.on = true;
        if (r == 9001) st2.on = ! st2.on;
        if (r == 9002) { const bool on = st2.on; st2 = Step{}; st2.on = on; }
        pat.setStep (t, s, st2);
        safe->edited();
    });
}

void StepGrid::trackMenu (int t)
{
    juce::PopupMenu m, len;
    static const int lens[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 24, 32, 48, 64, 96, 128 };
    const int cur = pat().lane (t).length;
    for (int i = 0; i < 19; ++i) len.addItem (ttLen8 + i, juce::String (lens[i]), true, cur == lens[i]);
    m.addSectionHeader ("Track " + juce::String (t + 1));
    m.addItem (ttLoad, "Load sample (browser)...");
    m.addItem (ttClearSample, "Clear sample", proc.samples.slot (t) != nullptr);
    m.addItem (ttAudition, "Audition");
    m.addSeparator();
    m.addItem (ttEuclid, "Euclidean...");
    m.addItem (ttRandom, "Random");
    m.addItem (ttCast, "Cast (geomancy, 64 steps)");
    m.addItem (ttMutate, "Mutate");
    m.addItem (ttShiftL, "Shift left");
    m.addItem (ttShiftR, "Shift right");
    m.addItem (ttReverse, "Reverse");
    m.addSubMenu ("Length", len);
    m.addSeparator();
    m.addItem (ttCopy, "Copy steps");
    m.addItem (ttPaste, "Paste steps");
    m.addItem (ttClearSteps, "Clear steps");
    juce::Component::SafePointer<StepGrid> safe (this);
    const int pi = proc.viewPattern; // act on the pattern the menu was opened on, even if playback moves the view
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, t, pi] (int r) {
        if (safe == nullptr || r == 0) return;
        if (r == ttLoad && safe->onBrowse) { safe->onBrowse (t); return; }
        const auto msg = runTrackTool (safe->proc, t, r, safe.getComponent(), [safe] { if (safe != nullptr) safe->edited(); },
                                       pi, &safe->chooser);
        if (msg.isNotEmpty()) { safe->status = msg; if (safe->onStatus) safe->onStatus(); }
    });
}

juce::Array<juce::File> collectAudio (const juce::StringArray& paths)
{
    juce::Array<juce::File> audio;
    auto add = [&audio] (juce::Array<juce::File> files) {
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) {
            return a.getFullPathName().compareNatural (b.getFullPathName()) < 0; // "kick 2" before "kick 10"
        });
        for (auto& f : files)
            if (SamplePool::isAudioFile (f)) audio.add (f);
    };
    for (auto& path : paths)
    {
        const juce::File f (path);
        if (f.isDirectory())
        {
            const int before = audio.size();
            add (f.findChildFiles (juce::File::findFiles, false));
            if (audio.size() == before) // nothing playable at the top: look inside subfolders
                add (f.findChildFiles (juce::File::findFiles, true));
        }
        else if (SamplePool::isAudioFile (f))
            audio.add (f);
    }
    return audio;
}

juce::String loadFiles (GeomancerProcessor& p, const juce::StringArray& paths, int firstTrack)
{
    const auto audio = collectAudio (paths);
    int loaded = 0;
    juce::String err;
    for (auto& f : audio)
    {
        if (firstTrack + loaded >= kNumTracks) break;
        if (p.loadSample (firstTrack + loaded, f, err)) ++loaded;
    }
    if (loaded == 0) return err.isNotEmpty() ? err : juce::String ("no audio files found there");
    return "loaded " + juce::String (loaded) + " sample" + (loaded > 1 ? "s" : "") + " into T" + juce::String (firstTrack + 1)
         + (loaded > 1 ? "-T" + juce::String (firstTrack + loaded) : juce::String())
         + (audio.size() > loaded ? "  (" + juce::String (audio.size() - loaded) + " more didn't fit)" : juce::String());
}

void StepGrid::itemDropped (const SourceDetails& d)
{
    filesDropped ({ d.description.toString().fromFirstOccurrenceOf ("gmfile:", false, false) }, 0, d.localPosition.y);
}

void StepGrid::filesDropped (const juce::StringArray& files, int, int y)
{
    dropRow = -1;
    int t = rowAt (y);
    if (t < 0) t = proc.selectedTrack;
    status = loadFiles (proc, files, t);
    if (onStatus) onStatus();
    select (t);
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void StepGrid::refresh()
{
    const bool running = proc.uiRunning.load() && proc.uiCurrentPattern.load() == proc.viewPattern;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const auto sv = proc.samples.version.load();
    if (sv != shownSamples) { shownSamples = sv; repaint(); }
    for (int t = 0; t < kNumTracks; ++t)
    {
        bool dirty = false;
        const auto h = proc.hitCount (t);
        if (h != lastHits[t]) { lastHits[t] = h; hitTime[t] = now; dirty = true; }
        if (now - hitTime[t] < 400.0) dirty = true;
        const int pos = running ? proc.uiPlayPos[(size_t) t].load() : -1;
        if (pos != shownPos[t]) { shownPos[t] = pos; dirty = true; }
        if (dirty) repaint (rowRect (t));
    }
}
} // namespace gm
