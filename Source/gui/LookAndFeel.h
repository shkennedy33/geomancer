#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace gm
{
namespace col
{
    const juce::Colour bg { 0xff111215 }, panel { 0xff191b20 }, panel2 { 0xff20232a }, raised { 0xff2a2e37 },
        cell { 0xff262a32 }, cellAlt { 0xff2e333c }, line { 0xff353a45 }, text { 0xffe8e6e3 }, dim { 0xff8b919c },
        faint { 0xff5a606b }, accent { 0xffff8a2a }, accent2 { 0xff36d6c3 }, mute { 0xffe5484d }, solo { 0xfff2c94c },
        maths { 0xffb48cff };
    juce::Colour track (int t);
}

juce::Font uiFont (float height, bool bold = false);

class LnF : public juce::LookAndFeel_V4
{
public:
    LnF();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return uiFont (std::min (13.0f, h * 0.6f), true); }
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }
    juce::Font getLabelFont (juce::Label&) override { return uiFont (12.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
};

//==============================================================================
// Rotary knob with a name and a value readout, bound to a parameter. Rebindable (the sound panel
// re-targets its knobs whenever the selected track changes).
class Knob : public juce::Component
{
public:
    explicit Knob (juce::String name, juce::Colour c = col::accent);
    void bind (juce::AudioProcessorValueTreeState& s, const juce::String& paramID);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    juce::Slider slider;
    juce::String title;
    juce::Colour colour;
    bool bipolar = false;

private:
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
};

// Compact value chip for choice/int parameters: click for a menu, drag or wheel to step.
class Chip : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Chip (juce::String name = {});
    void bind (juce::AudioProcessorValueTreeState& s, const juce::String& paramID);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String title, prefix;
    juce::Colour colour = col::accent;
    bool showTitle = true;

private:
    void setIndex (int i);
    int index() const;
    int count() const;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int dragStartIndex = 0;
    bool dragged = false;
};

// Toggle bound to a bool parameter.
class Toggle : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Toggle (juce::String text, juce::Colour on = col::accent);
    void bind (juce::AudioProcessorValueTreeState& s, const juce::String& paramID);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool getState() const { return param != nullptr && param->getValue() > 0.5f; }
    juce::String text;
    juce::Colour onColour;
    bool momentary = false;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Flat text button that calls a lambda.
class Btn : public juce::Component, public juce::SettableTooltipClient
{
public:
    Btn (juce::String t, std::function<void()> f, juce::Colour c = col::text) : text (std::move (t)), onClick (std::move (f)), colour (c) {}
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override;
    juce::String text;
    std::function<void()> onClick;
    juce::Colour colour;
    bool lit = false;

private:
    bool down = false;
};

void setParam (juce::RangedAudioParameter* p, float plainValue);
juce::String noteName (int note);
} // namespace gm
