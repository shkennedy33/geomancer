#include "DrumSynth.h"
#include "DSP.h"

namespace gm::drumsynth
{
const juce::StringArray& names()
{
    static const juce::StringArray n { "kick", "snare", "hat", "openhat", "clap", "tomlo", "tommid", "tomhi",
                                       "rim", "cowbell", "cymbal", "shaker", "zap", "boom" };
    return n;
}

namespace
{
struct Gen
{
    double sr;
    Rng rng { 0x51f15eedU };
    float noise() { return rng.bi(); }
    float env (double t, double decay) const { return (float) std::exp (-t / decay); }
};

// 808-style metallic source: six detuned square oscillators
float metal (double t, float baseScale)
{
    static const float f[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
    float s = 0;
    for (float fr : f)
        s += std::fmod ((float) t * fr * baseScale, 1.0f) < 0.5f ? 1.0f : -1.0f;
    return s / 6.0f;
}
} // namespace

bool render (const juce::String& name, double sr, juce::AudioBuffer<float>& out)
{
    Gen g { sr };
    double len = 0.5;
    if (name == "kick") len = 0.9;
    else if (name == "openhat") len = 0.7;
    else if (name == "cymbal") len = 2.2;
    else if (name == "boom") len = 2.0;
    else if (name == "cowbell" || name == "tomlo") len = 0.6;
    const int n = (int) (len * sr);
    out.setSize (1, n);
    auto* d = out.getWritePointer (0);
    SVF hp, bp, lp;
    double phase = 0;

    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        float y = 0;
        if (name == "kick")
        {
            const double f = 46.0 + 110.0 * std::exp (-t / 0.035);
            phase += f / sr;
            y = std::sin (2.0 * kPi * (float) phase) * g.env (t, 0.32) + 0.35f * g.noise() * g.env (t, 0.002);
            y = std::tanh (y * 1.6f);
        }
        else if (name == "boom")
        {
            const double f = 41.0 + 30.0 * std::exp (-t / 0.05);
            phase += f / sr;
            y = std::tanh (2.2f * std::sin (2.0f * kPi * (float) phase)) * g.env (t, 0.7) * 0.8f;
        }
        else if (name == "snare")
        {
            if (i == 0) hp.set (1800.0f, 0.1f, sr);
            phase += (180.0 * (1.0 + 0.4 * std::exp (-t / 0.01))) / sr;
            const float tone = std::sin (2.0f * kPi * (float) phase) * 0.55f + std::sin (2.0f * kPi * (float) phase * 1.83f) * 0.3f;
            y = tone * g.env (t, 0.05) + hp.process (g.noise(), SVF::HP) * 0.75f * g.env (t, 0.13);
        }
        else if (name == "hat" || name == "openhat" || name == "cymbal")
        {
            if (i == 0) { hp.set (name == "cymbal" ? 5200.0f : 7200.0f, 0.25f, sr); bp.set (10000.0f, 0.2f, sr); }
            const float m = metal (t, name == "cymbal" ? 1.7f : 2.4f) * 0.7f + g.noise() * 0.3f;
            const double dec = name == "hat" ? 0.035 : (name == "openhat" ? 0.25 : 0.75);
            y = (hp.process (m, SVF::HP) * 0.8f + bp.process (m, SVF::BP) * 0.3f) * g.env (t, dec) * 1.4f;
        }
        else if (name == "clap")
        {
            if (i == 0) bp.set (1150.0f, 0.45f, sr);
            float e = 0;
            for (double b : { 0.0, 0.011, 0.022 }) // three hand slaps, then the room
                if (t >= b) e = std::max (e, g.env (t - b, 0.006));
            if (t >= 0.031) e = std::max (e, 0.7f * g.env (t - 0.031, 0.11));
            y = bp.process (g.noise(), SVF::BP) * e * 2.2f;
        }
        else if (name.startsWith ("tom"))
        {
            const double base = name == "tomlo" ? 85.0 : (name == "tommid" ? 125.0 : 175.0);
            phase += base * (1.0 + 0.6 * std::exp (-t / 0.03)) / sr;
            y = std::sin (2.0f * kPi * (float) phase) * g.env (t, name == "tomlo" ? 0.22 : 0.16) + 0.1f * g.noise() * g.env (t, 0.004);
        }
        else if (name == "rim")
        {
            if (i == 0) bp.set (1700.0f, 0.7f, sr);
            phase += 480.0 / sr;
            y = (bp.process (g.noise(), SVF::BP) * 1.8f + std::sin (2.0f * kPi * (float) phase) * 0.6f) * g.env (t, 0.012);
        }
        else if (name == "cowbell")
        {
            if (i == 0) bp.set (900.0f, 0.5f, sr);
            const float sq = (std::fmod ((float) t * 540.0f, 1.0f) < 0.5f ? 0.5f : -0.5f) + (std::fmod ((float) t * 800.0f, 1.0f) < 0.5f ? 0.5f : -0.5f);
            y = bp.process (sq, SVF::BP) * (0.75f * g.env (t, 0.02) + 0.6f * g.env (t, 0.16));
        }
        else if (name == "shaker")
        {
            if (i == 0) hp.set (6000.0f, 0.1f, sr);
            const float sw = (float) std::min (1.0, t / 0.025);
            y = hp.process (g.noise(), SVF::HP) * sw * g.env (t, 0.05) * 0.9f;
        }
        else if (name == "zap")
        {
            phase += (90.0 + 2400.0 * std::exp (-t / 0.018)) / sr;
            y = std::sin (2.0f * kPi * (float) phase) * g.env (t, 0.09);
        }
        else
            return false;
        d[i] = y;
    }
    // 2 ms fade-out so truncated tails never click
    const int fade = std::min (n, (int) (0.002 * sr));
    for (int i = 0; i < fade; ++i) d[n - 1 - i] *= (float) i / (float) fade;
    const float pk = out.getMagnitude (0, 0, n);
    if (pk > 0) out.applyGain (0.89f / pk);
    return true;
}
} // namespace gm::drumsynth
