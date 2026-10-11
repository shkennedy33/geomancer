#include "LaneEditor.h"

namespace gm
{
static const char* kKindNames[] = { "VELOCITY", "GATE", "PROBABILITY", "RATCHET", "PITCH", "NUDGE", "CONDITION" };

LaneEditor::LaneEditor (GeomancerProcessor& p) : proc (p)
{
    struct T { const char* name; int tool; const char* tip; };
    const T defs[] = {
        { "EUCLID", ttEuclid, "Euclidean rhythm: spread N hits as evenly as possible" },
        { "RANDOM", ttRandom, "Random steps" },
        { "CAST", ttCast, "Geomancy: cast a 16-figure chart into 64 steps" },
        { "MUTATE", ttMutate, "Flip a few steps, wobble velocities" },
        { "<<", ttShiftL, "Shift steps left" },
        { ">>", ttShiftR, "Shift steps right" },
        { "REVERSE", ttReverse, "Reverse the steps" },
        { "CLEAR", ttClearSteps, "Clear this track's steps" },
        { "COPY", ttCopy, "Copy this track's steps" },
        { "PASTE", ttPaste, "Paste steps into this track" },
    };
    for (auto& d : defs)
    {
        const int tool = d.tool;
        auto* b = tools.add (new Btn (d.name, {}, tool == ttCast ? col::maths : col::text));
        b->onClick = [this, tool, b] {
            juce::Component::SafePointer<LaneEditor> safe (this);
            const auto msg = runTrackTool (proc, proc.selectedTrack, tool, b, [safe] {
                if (safe == nullptr) return;
                safe->proc.markDirty();
                safe->repaint();
                if (safe->onEdited) safe->onEdited();
            });
            if (msg.isNotEmpty()) { status = msg; if (onStatus) onStatus(); }
        };
        b->setTooltip (d.tip);
        addAndMakeVisible (b);
    }
}

void LaneEditor::resized()
{
    for (int i = 0; i < tools.size(); ++i)
        tools[i]->setBounds (StepGrid::kLaneX + (i % 2) * 77, 4 + (i / 2) * 19, 75, 18);
}

void LaneEditor::range (int& lo, int& hi) const
{
    switch (proc.laneKind)
    {
        case lkVel: lo = 1; hi = 127; break;
        case lkGate: lo = 1; hi = 64; break;
        case lkProb: lo = 0; hi = 100; break;
        case lkRatchet: lo = 1; hi = 8; break;
        case lkPitch: lo = -24; hi = 24; break;
        case lkNudge: lo = -50; hi = 50; break;
        default: lo = 0; hi = CondCount - 1; break;
    }
}

int LaneEditor::getValue (const Step& st) const
{
    switch (proc.laneKind)
    {
        case lkVel: return st.vel;
        case lkGate: return st.gate;
        case lkProb: return st.prob;
        case lkRatchet: return st.ratchet;
        case lkPitch: return st.pitch;
        case lkNudge: return st.nudge;
        default: return st.cond;
    }
}

juce::String LaneEditor::valueText (int v) const
{
    switch (proc.laneKind)
    {
        case lkGate: return juce::String (v * 100 / 16) + "%";
        case lkProb: return juce::String (v) + "%";
        case lkRatchet: return juce::String (v) + "x";
        case lkPitch: return (v > 0 ? "+" : "") + juce::String (v);
        case lkNudge: return (v > 0 ? "+" : "") + juce::String (v) + "%";
        case lkCond: return condName (v);
        default: return juce::String (v);
    }
}

void LaneEditor::setValue (int s, int v)
{
    auto st = pat().step (proc.selectedTrack, s);
    switch (proc.laneKind)
    {
        case lkVel: st.vel = (uint8_t) v; break;
        case lkGate: st.gate = (uint8_t) v; break;
        case lkProb: st.prob = (uint8_t) v; break;
        case lkRatchet: st.ratchet = (uint8_t) v; break;
        case lkPitch: st.pitch = (int8_t) v; break;
        case lkNudge: st.nudge = (int8_t) v; break;
        default: st.cond = (uint8_t) v; break;
    }
    pat().setStep (proc.selectedTrack, s, st);
}

int LaneEditor::stepAt (int x) const
{
    if (x < StepGrid::kGridX || x >= StepGrid::kGridX + StepGrid::kCols * StepGrid::kCellW) return -1;
    return proc.page * StepGrid::kCols + (x - StepGrid::kGridX) / StepGrid::kCellW;
}

int LaneEditor::valueFromY (int y) const
{
    int lo, hi;
    range (lo, hi);
    const float f = 1.0f - juce::jlimit (0.0f, 1.0f, (float) (y - kBarTop) / (float) kBarH);
    return lo + juce::roundToInt (f * (float) (hi - lo));
}

void LaneEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.drawHorizontalLine (0, 0, (float) getWidth());

    for (int k = 0; k < lkCount; ++k)
    {
        auto r = kindRect (k).toFloat();
        const bool on = proc.laneKind == k;
        g.setColour (on ? col::accent : col::raised);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? col::bg : col::dim);
        g.setFont (uiFont (10.5f, true));
        g.drawText (kKindNames[k], r, juce::Justification::centred);
    }
    g.setColour (col::track (proc.selectedTrack));
    g.setFont (uiFont (11.0f, true));
    g.drawText ("T" + juce::String (proc.selectedTrack + 1), kindRect (7).toFloat(), juce::Justification::centred);

    const auto& pt = proc.bank.patterns[proc.viewPattern];
    const int t = proc.selectedTrack;
    const int len = pt.lane (t).length;
    const auto tc = col::track (t);
    int lo, hi;
    range (lo, hi);
    for (int c = 0; c < StepGrid::kCols; ++c)
    {
        const int s = proc.page * StepGrid::kCols + c;
        auto r = juce::Rectangle<float> ((float) (StepGrid::kGridX + c * StepGrid::kCellW), (float) kBarTop,
                                         (float) StepGrid::kCellW - 2.0f, (float) kBarH);
        g.setColour (s < len ? col::cell : col::bg);
        g.fillRoundedRectangle (r, 2.0f);
        const auto st = pt.step (t, s);
        const int v = getValue (st);
        const float alpha = (st.on ? 1.0f : 0.25f) * (s < len ? 1.0f : 0.4f); // past the end: dimmed, still editable
        g.setColour (tc.withAlpha (alpha));
        if (proc.laneKind == lkCond)
        {
            g.setFont (uiFont (9.5f, true));
            g.setColour (v == 0 ? col::faint.withAlpha (alpha) : tc.withAlpha (alpha));
            g.drawFittedText (v == 0 ? juce::String ("-") : juce::String (condName (v)), r.toNearestInt(), juce::Justification::centred, 2);
            continue;
        }
        if (bipolar())
        {
            const float mid = r.getCentreY();
            const float y = r.getY() + r.getHeight() * (1.0f - (float) (v - lo) / (float) (hi - lo));
            g.fillRect (r.getX() + 2.0f, std::min (mid, y), r.getWidth() - 4.0f, std::max (1.5f, std::abs (y - mid)));
            g.setColour (col::line);
            g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
        }
        else
        {
            const float f = (float) (v - lo) / (float) (hi - lo);
            g.fillRoundedRectangle (r.withTop (r.getBottom() - std::max (2.0f, r.getHeight() * f)).reduced (2.0f, 0), 1.5f);
        }
        if (st.on && (proc.laneKind == lkRatchet || proc.laneKind == lkPitch || proc.laneKind == lkNudge) && v != (proc.laneKind == lkRatchet ? 1 : 0))
        {
            g.setColour (col::text);
            g.setFont (uiFont (9.0f, true));
            g.drawText (valueText (v), r.withHeight (12.0f), juce::Justification::centred);
        }
    }
}

void LaneEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int k = 0; k < lkCount; ++k)
        if (kindRect (k).contains (e.getPosition()))
        {
            proc.laneKind = k;
            repaint();
            return;
        }
    const int s = stepAt (e.x);
    if (s < 0) return;
    proc.pushUndo (proc.viewPattern);
    editing = true;
    if (e.mods.isPopupMenu()) // right-click resets the value
    {
        setValue (s, getValue (Step{}));
        lastStep = -1;
    }
    else
    {
        const int v = proc.laneKind == lkCond ? (getValue (pat().step (proc.selectedTrack, s)) + 1) % CondCount : valueFromY (e.y);
        setValue (s, v);
        lastStep = s;
        lastVal = v;
        status = "step " + juce::String (s + 1) + "  " + kKindNames[proc.laneKind] + " " + valueText (v);
        if (onStatus) onStatus();
    }
    proc.markDirty();
    repaint();
    if (onEdited) onEdited();
}

void LaneEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! editing || proc.laneKind == lkCond || lastStep < 0) return;
    const int s = stepAt (std::clamp (e.x, StepGrid::kGridX, StepGrid::kGridX + StepGrid::kCols * StepGrid::kCellW - 1));
    if (s < 0) return;
    const int v = valueFromY (e.y);
    // draw a line from the last step to this one so fast swipes don't leave gaps
    const int a = lastStep, b = s;
    for (int k = std::min (a, b); k <= std::max (a, b); ++k)
    {
        const float f = a == b ? 1.0f : (float) (k - a) / (float) (b - a);
        setValue (k, juce::roundToInt (lastVal + (v - lastVal) * f));
    }
    lastStep = s;
    lastVal = v;
    status = "step " + juce::String (s + 1) + "  " + kKindNames[proc.laneKind] + " " + valueText (v);
    if (onStatus) onStatus();
    repaint();
    if (onEdited) onEdited();
}

void LaneEditor::mouseUp (const juce::MouseEvent&)
{
    if (editing) proc.markDirty(); // once per gesture
    editing = false;
    lastStep = -1;
}
} // namespace gm
