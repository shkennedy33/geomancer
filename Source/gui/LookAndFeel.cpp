#include "LookAndFeel.h"

namespace gm
{
juce::Colour col::track (int t)
{
    // warm → cool sweep: kicks/snares read amber, hats teal, percussion violet
    return juce::Colour::fromHSV (std::fmod (0.075f + (float) t * 0.052f, 1.0f), 0.62f, 0.98f, 1.0f);
}

juce::Font uiFont (float height, bool bold)
{
    static const bool hasInter = juce::Font::findAllTypefaceNames().contains ("Inter");
    auto opts = juce::FontOptions (height).withStyle (bold ? "Bold" : "Regular");
    if (hasInter) opts = opts.withName ("Inter");
    return juce::Font (opts);
}

void setParam (juce::RangedAudioParameter* p, float plain)
{
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
    p->endChangeGesture();
}

juce::String noteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 4) + " " + juce::String (note);
}

//==============================================================================
LnF::LnF()
{
    setColour (juce::PopupMenu::backgroundColourId, col::panel2);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::bg);
    setColour (juce::PopupMenu::headerTextColourId, col::dim);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, col::raised);
    setColour (juce::Slider::textBoxHighlightColourId, col::accent.withAlpha (0.4f));
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextButton::buttonColourId, col::raised);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TooltipWindow::backgroundColourId, col::panel2);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::line);
    setColour (juce::TextEditor::backgroundColourId, col::panel2);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, col::accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, col::accent);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
}

void LnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto c = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float size = (float) std::min (w, h) - 4.0f;
    const auto centre = juce::Point<float> ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float r = size * 0.5f;
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);

    // track
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, r - 2.0f, r - 2.0f, 0, start, end, true);
    g.setColour (col::line);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    juce::Path val;
    const float from = bipolar ? (start + end) * 0.5f : start;
    val.addCentredArc (centre.x, centre.y, r - 2.0f, r - 2.0f, 0, std::min (from, angle), std::max (from, angle), true);
    g.setColour (s.isEnabled() ? c : col::faint);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // body
    const float br = r - 6.5f;
    g.setGradientFill (juce::ColourGradient (col::raised.brighter (0.15f), centre.x, centre.y - br, col::panel2, centre.x, centre.y + br, false));
    g.fillEllipse (centre.x - br, centre.y - br, br * 2, br * 2);
    g.setColour (col::bg.withAlpha (0.8f));
    g.drawEllipse (centre.x - br, centre.y - br, br * 2, br * 2, 1.0f);

    // pointer
    const auto tip = centre.getPointOnCircumference (br - 2.0f, angle);
    const auto base = centre.getPointOnCircumference (br * 0.25f, angle);
    g.setColour (col::text);
    g.drawLine ({ base, tip }, 2.0f);
}

void LnF::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                            juce::Slider::SliderStyle style, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto c = s.findColour (juce::Slider::trackColourId);
    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar)
    {
        // slim track + fill (from the centre when the slider is bipolar) + round thumb
        auto tr = r.withSizeKeepingCentre (r.getWidth(), 6.0f);
        g.setColour (col::cell);
        g.fillRoundedRectangle (tr, 3.0f);
        const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? tr.getCentreX() : tr.getX();
        g.setColour (c);
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (from, pos), tr.getY(), std::abs (pos - from), tr.getHeight()), 3.0f);
        g.setColour (col::text);
        g.fillEllipse (pos - 6.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f);
        return;
    }
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (c.withAlpha (0.5f));
    g.fillRoundedRectangle (r.withTop (pos), 3.0f);
}

void LnF::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? col::accent : col::raised;
    if (down) c = c.brighter (0.2f);
    else if (over) c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
}

void LnF::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? col::bg : col::text);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1);
}

void LnF::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::panel2);
    g.setColour (col::line);
    g.drawRect (0, 0, w, h);
}

void LnF::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (col::panel2);
    g.fillPath (path);
    g.setColour (col::line);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

//==============================================================================
Knob::Knob (juce::String name, juce::Colour c) : title (std::move (name)), colour (c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setMouseDragSensitivity (180);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.onValueChange = [this] { repaint(); };
    slider.onDragStart = [this] { repaint(); };
    slider.onDragEnd = [this] { repaint(); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

void Knob::bind (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    attachment.reset();
    param = s.getParameter (id);
    if (param == nullptr) return;
    attachment = std::make_unique<juce::SliderParameterAttachment> (*param, slider, nullptr);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.getProperties().set ("bipolar", bipolar);
    slider.setTooltip (param->getName (64));
    repaint();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (24);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto text = r.removeFromBottom (24);
    const bool hot = slider.isMouseOverOrDragging();
    g.setFont (uiFont (10.5f, true));
    g.setColour (hot ? col::text : col::dim);
    g.drawText (title.toUpperCase(), text.removeFromTop (12), juce::Justification::centred, false);
    g.setFont (uiFont (10.5f));
    g.setColour (hot ? colour : col::faint.brighter (0.3f));
    if (param != nullptr)
        g.drawText (param->getCurrentValueAsText(), text, juce::Justification::centred, false);
}

//==============================================================================
Chip::Chip (juce::String name) : title (std::move (name)) {}

void Chip::bind (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    attachment.reset();
    param = s.getParameter (id);
    if (param == nullptr) return;
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); }, nullptr);
    attachment->sendInitialUpdate();
    repaint();
}

int Chip::count() const
{
    if (param == nullptr) return 1;
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (param)) return c->choices.size();
    const auto& r = param->getNormalisableRange();
    return (int) (r.end - r.start) + 1;
}

int Chip::index() const
{
    if (param == nullptr) return 0;
    const auto& r = param->getNormalisableRange();
    return juce::roundToInt (param->convertFrom0to1 (param->getValue()) - r.start);
}

void Chip::setIndex (int i)
{
    if (param == nullptr || attachment == nullptr) return;
    const auto& r = param->getNormalisableRange();
    attachment->setValueAsCompleteGesture (r.start + (float) std::clamp (i, 0, count() - 1));
}

void Chip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (showTitle && title.isNotEmpty())
    {
        g.setFont (uiFont (10.5f, true));
        g.setColour (col::dim);
        g.drawText (title.toUpperCase(), r.removeFromTop (13.0f), juce::Justification::centred, false);
        r.removeFromTop (2.0f);
    }
    r = r.reduced (1.0f);
    g.setColour (isMouseOver() ? col::raised.brighter (0.1f) : col::raised);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (colour.withAlpha (0.9f));
    g.fillRoundedRectangle (r.removeFromLeft (3.0f), 1.5f);
    g.setColour (col::text);
    g.setFont (uiFont (std::min (12.5f, r.getHeight() * 0.62f), true));
    if (param != nullptr)
        g.drawFittedText (prefix + param->getCurrentValueAsText(), r.toNearestInt().reduced (3, 0), juce::Justification::centred, 1);
}

void Chip::mouseDown (const juce::MouseEvent&)
{
    dragStartIndex = index();
    dragged = false;
}

void Chip::mouseDrag (const juce::MouseEvent& e)
{
    if (attachment == nullptr) return;
    const int delta = -e.getDistanceFromDragStartY() / 12;
    if (delta != 0 && ! dragged)
    {
        dragged = true;
        attachment->beginGesture(); // one undo point / automation touch per drag, not per step
    }
    if (dragged)
    {
        const auto& r = param->getNormalisableRange();
        attachment->setValueAsPartOfGesture (r.start + (float) std::clamp (dragStartIndex + delta, 0, count() - 1));
    }
}

void Chip::mouseUp (const juce::MouseEvent& e)
{
    if (dragged && attachment != nullptr) attachment->endGesture();
    if (dragged || param == nullptr || ! e.mouseWasClicked() || count() > 64) return;
    juce::PopupMenu m;
    const int n = count(), cur = index();
    const auto& r = param->getNormalisableRange();
    for (int i = 0; i < n; ++i)
        m.addItem (i + 1, param->getText (param->convertTo0to1 (r.start + (float) i), 64), true, i == cur);
    juce::Component::SafePointer<Chip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMaximumNumColumns (n > 24 ? 4 : 1),
                     [safe] (int res) { if (safe != nullptr && res > 0) safe->setIndex (res - 1); });
}

void Chip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    setIndex (index() + (w.deltaY > 0 ? 1 : -1) * (w.isReversed ? -1 : 1));
}

//==============================================================================
Toggle::Toggle (juce::String t, juce::Colour on) : text (std::move (t)), onColour (on) {}

void Toggle::bind (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    attachment.reset();
    param = s.getParameter (id);
    if (param == nullptr) return;
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); }, nullptr);
    repaint();
}

void Toggle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getState();
    g.setColour (on ? onColour : (isMouseOver() ? col::raised.brighter (0.1f) : col::raised));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? col::bg : col::text);
    g.setFont (uiFont (std::min (12.0f, r.getHeight() * 0.6f), true));
    g.drawFittedText (text, r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1);
}

void Toggle::mouseDown (const juce::MouseEvent&)
{
    if (attachment == nullptr) return;
    attachment->setValueAsCompleteGesture (momentary ? 1.0f : (getState() ? 0.0f : 1.0f));
}

void Toggle::mouseUp (const juce::MouseEvent&)
{
    if (momentary && attachment != nullptr) attachment->setValueAsCompleteGesture (0.0f);
}

//==============================================================================
void Btn::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    auto c = lit ? colour : col::raised;
    if (down) c = c.brighter (0.25f);
    else if (isMouseOver()) c = c.brighter (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (lit ? col::bg : colour);
    g.setFont (uiFont (std::min (12.0f, r.getHeight() * 0.62f), true));
    g.drawFittedText (text, r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1);
}

void Btn::mouseUp (const juce::MouseEvent& e)
{
    down = false;
    repaint();
    if (e.mouseWasClicked() && onClick) onClick();
}
} // namespace gm
