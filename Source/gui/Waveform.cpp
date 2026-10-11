#include "Waveform.h"
#include "StepGrid.h"

namespace gm
{
void Waveform::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    const int t = proc.selectedTrack;
    auto* s = proc.samples.slot (t);
    const auto tc = col::track (t);
    if (s == nullptr || s->peaks.empty())
    {
        g.setColour (col::faint);
        g.setFont (uiFont (13.0f));
        g.drawFittedText (proc.missing (t).isNotEmpty() ? "missing:\n" + proc.missing (t)
                                                             : juce::String ("drop a sample here, or double-click a track name"),
                          getLocalBounds().reduced (10), juce::Justification::centred, 3);
        return;
    }
    const float start = proc.trackParam (t, tpStart)->convertFrom0to1 (proc.trackParam (t, tpStart)->getValue());
    const float end = proc.trackParam (t, tpEnd)->convertFrom0to1 (proc.trackParam (t, tpEnd)->getValue());
    const bool rev = proc.trackParam (t, tpReverse)->getValue() > 0.5f;
    const int bins = (int) s->peaks.size() / 2;
    const float mid = r.getCentreY(), half = r.getHeight() * 0.45f;
    float peak = 0.001f;
    for (auto v : s->peaks) peak = std::max (peak, std::abs (v));
    const float scale = half / peak;

    const float x0 = xOf (std::min (start, end)), x1 = xOf (std::max (start, end));
    const int w = getWidth() - 8;
    for (int px = 0; px < w; ++px)
    {
        const int b = px * bins / w;
        const float lo = s->peaks[(size_t) b * 2], hi = s->peaks[(size_t) b * 2 + 1];
        const float x = 4.0f + (float) px;
        const bool inside = x >= x0 && x <= x1;
        g.setColour (inside ? tc.withAlpha (0.9f) : col::faint.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, mid - hi * scale, mid - lo * scale + 1.0f);
    }
    // flags
    for (int i = 0; i < 2; ++i)
    {
        const float x = xOf (i == 0 ? start : end);
        g.setColour (i == 0 ? col::accent2 : col::accent);
        g.drawLine (x, 2.0f, x, r.getBottom() - 2.0f, 1.5f);
        juce::Path flag;
        const float d = i == 0 ? 1.0f : -1.0f;
        flag.addTriangle (x, 2.0f, x + 8.0f * d, 2.0f, x, 10.0f);
        g.fillPath (flag);
    }
    g.setColour (col::dim);
    g.setFont (uiFont (11.0f));
    const double secs = s->length() / s->sampleRate;
    g.drawText (juce::String (secs, 2) + " s  " + juce::String (s->sampleRate / 1000.0, 1) + " kHz"
                    + (s->buffer.getNumChannels() > 1 ? "  stereo" : "  mono") + (rev ? "   REVERSED" : ""),
                getLocalBounds().reduced (8, 4), juce::Justification::bottomLeft);
}

void Waveform::mouseDown (const juce::MouseEvent& e)
{
    const int t = proc.selectedTrack;
    if (proc.samples.slot (t) == nullptr) return;
    auto* ps = proc.trackParam (t, tpStart);
    auto* pe = proc.trackParam (t, tpEnd);
    const float xs = xOf (ps->convertFrom0to1 (ps->getValue())), xe = xOf (pe->convertFrom0to1 (pe->getValue()));
    if (std::abs ((float) e.x - xs) < 8.0f) dragging = 1;
    else if (std::abs ((float) e.x - xe) < 8.0f) dragging = 2;
    else { proc.audition[(size_t) t] = 110; return; }
    dragParam = dragging == 1 ? ps : pe;
    dragParam->beginChangeGesture();
}

void Waveform::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == 0 || dragParam == nullptr) return;
    dragParam->setValueNotifyingHost (fracOf (e.x));
    repaint();
}

void Waveform::mouseUp (const juce::MouseEvent&)
{
    if (dragParam != nullptr) dragParam->endChangeGesture();
    dragParam = nullptr;
    dragging = 0;
}

void Waveform::filesDropped (const juce::StringArray& files, int, int)
{
    loadFiles (proc, files, proc.selectedTrack); // a folder fills tracks downward from here
    repaint();
    if (onLoaded) onLoaded();
}
} // namespace gm
