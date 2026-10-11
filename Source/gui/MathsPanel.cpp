#include "MathsPanel.h"

namespace gm
{
MathsPanel::MathsPanel (GeomancerProcessor& p) : proc (p), presets ("RECIPES", {}, col::maths)
{
    auto& s = proc.apvts;
    for (int c = 0; c < 2; ++c)
    {
        auto& h = ch[c];
        const auto colour = c == 0 ? col::accent : col::accent2;
        h.rise.colour = h.fall.colour = h.curve.colour = h.level.colour = colour;
        h.rise.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        h.fall.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        h.curve.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        h.level.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        h.curve.bipolar = h.xmod.bipolar = h.level.bipolar = true;
        h.rise.bind (s, mathsParamID (c, mpRise));
        h.fall.bind (s, mathsParamID (c, mpFall));
        h.curve.bind (s, mathsParamID (c, mpCurve));
        h.xmod.bind (s, mathsParamID (c, mpXMod));
        h.level.bind (s, mathsParamID (c, mpLevel));
        h.cycle.bind (s, mathsParamID (c, mpCycle));
        h.sync.bind (s, mathsParamID (c, mpSync));
        h.trig.bind (s, mathsParamID (c, mpTrig));
        h.trig.colour = colour;
        for (auto* comp : std::initializer_list<juce::Component*> { &h.rise, &h.fall, &h.curve, &h.xmod, &h.level, &h.cycle, &h.sync, &h.trig })
            addAndMakeVisible (comp);
    }
    for (int i = 0; i < kNumModSlots; ++i)
    {
        auto& sl = slots[i];
        sl.src.bind (s, modSlotID (i, "src"));
        sl.trk.bind (s, modSlotID (i, "trk"));
        sl.dst.bind (s, modSlotID (i, "par"));
        for (auto* c : { &sl.src, &sl.trk, &sl.dst }) { c->showTitle = false; c->colour = col::maths; addAndMakeVisible (c); }
        sl.amt.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.amt.setTextBoxStyle (juce::Slider::TextBoxRight, true, 46, 20);
        sl.amt.setColour (juce::Slider::trackColourId, col::maths);
        sl.amt.setColour (juce::Slider::thumbColourId, col::maths);
        sl.amt.setColour (juce::Slider::backgroundColourId, col::cell);
        sl.att = std::make_unique<juce::SliderParameterAttachment> (*s.getParameter (modSlotID (i, "amt")), sl.amt, nullptr);
        sl.amt.setDoubleClickReturnValue (true, 0.0);
        sl.amt.getProperties().set ("bipolar", true);
        addAndMakeVisible (sl.amt);
    }
    on.bind (s, gid::mOn);
    quant.bind (s, gid::mQuant);
    quant.setTooltip ("Snap MATHS-fired hits to the 1/16 grid");
    presets.onClick = [this] {
        juce::PopupMenu m;
        m.addSectionHeader ("Starting points (they set MATHS + a few track pulses)");
        m.addItem (1, "Cross-coupled pair: A and B push each other around");
        m.addItem (2, "5 against 3: two cycling clocks, hats + perc");
        m.addItem (3, "Bouncing ball: pulses that speed up through every bar");
        m.addItem (4, "Drift: free-running, unsynced, quantized to the grid");
        m.addItem (5, "Breathing filter: slow SUM sweeps every cutoff");
        m.addSeparator();
        m.addItem (9, "Reset MATHS");
        juce::Component::SafePointer<MathsPanel> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presets), [safe] (int r) { if (safe != nullptr && r > 0) safe->loadPreset (r); });
    };
    addAndMakeVisible (on);
    addAndMakeVisible (quant);
    addAndMakeVisible (presets);
    for (int t = 0; t < kNumTracks; ++t)
    {
        pulse[t].title = juce::String (t + 1);
        pulse[t].showTitle = false;
        pulse[t].prefix = juce::String (t + 1) + ": ";
        pulse[t].colour = col::track (t);
        pulse[t].bind (s, trackParamID (t, tpMTrig));
        pulse[t].setTooltip ("Track " + juce::String (t + 1) + ": which MATHS pulse fires it");
        addAndMakeVisible (pulse[t]);
    }
}

void MathsPanel::resized()
{
    for (int c = 0; c < 2; ++c)
    {
        auto& h = ch[c];
        const int y = c == 0 ? 2 : 110;
        int x = 30;
        for (auto* k : { &h.rise, &h.fall, &h.curve, &h.xmod, &h.level })
        {
            k->setBounds (x, y + 14, 46, 78);
            x += 46;
        }
        h.trig.setBounds (x + 4, y + 10, 80, 37);
        h.cycle.setBounds (x + 4, y + 52, 39, 22);
        h.sync.setBounds (x + 45, y + 52, 39, 22);
    }
    on.setBounds (scopeArea.getX(), 160, 100, 24);
    quant.setBounds (scopeArea.getX() + 104, 160, 90, 24);
    presets.setBounds (scopeArea.getX() + 198, 160, 86, 24);

    const int mx = 780;
    for (int i = 0; i < kNumModSlots; ++i)
    {
        auto& sl = slots[i];
        const int y = 20 + i * 30;
        sl.src.setBounds (mx, y, 54, 26);
        sl.trk.setBounds (mx + 72, y, 54, 26);
        sl.dst.setBounds (mx + 128, y, 74, 26);
        sl.amt.setBounds (mx + 206, y, 284, 26);
    }
    for (int t = 0; t < kNumTracks; ++t)
        pulse[t].setBounds (mx + (t % 8) * 61, 156 + (t / 8) * 29, 59, 26);
}

void MathsPanel::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    for (int c = 0; c < 2; ++c)
    {
        const int y = c == 0 ? 2 : 110;
        auto r = juce::Rectangle<float> (8.0f, (float) y, 338.0f, 104.0f);
        g.setColour (col::panel2);
        g.fillRoundedRectangle (r, 5.0f);
        const auto colour = c == 0 ? col::accent : col::accent2;
        g.setColour (colour);
        g.setFont (uiFont (22.0f, true));
        g.drawText (c == 0 ? "A" : "B", 10, y + 30, 20, 30, juce::Justification::centred);
        // EOR / EOC lamps
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 2; ++k)
        {
            const float a = (float) std::exp (-(now - flash[c * 2 + k]) / 120.0);
            g.setColour (col::cell.interpolatedWith (k == 0 ? col::text : colour, std::max (0.0f, a)));
            g.fillEllipse (14.0f, (float) y + 66.0f + k * 14.0f, 8.0f, 8.0f);
        }
        g.setColour (col::faint);
        g.setFont (uiFont (8.5f, true));
        g.drawText ("RISE", 22, y + 63, 30, 12, juce::Justification::left);
        g.drawText ("CYC", 22, y + 77, 30, 12, juce::Justification::left);
    }

    // scope
    auto sr = scopeArea.toFloat();
    g.setColour (col::bg);
    g.fillRoundedRectangle (sr, 5.0f);
    g.setColour (col::line);
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine ((int) (sr.getY() + sr.getHeight() * i / 4.0f), sr.getX() + 4, sr.getRight() - 4);
    const int w = proc.maths.scopeWrite.load (std::memory_order_acquire);
    for (int c = 0; c < 2; ++c)
    {
        juce::Path path;
        for (int i = 0; i < Maths::kScope; ++i)
        {
            const int idx = (w + i) % Maths::kScope;
            const float v = proc.maths.scope[c][idx].load (std::memory_order_relaxed);
            const float x = sr.getX() + 4.0f + (sr.getWidth() - 8.0f) * (float) i / (Maths::kScope - 1);
            const float y = sr.getBottom() - 6.0f - (sr.getHeight() - 12.0f) * juce::jlimit (0.0f, 1.0f, v);
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour ((c == 0 ? col::accent : col::accent2).withAlpha (0.9f));
        g.strokePath (path, juce::PathStrokeType (1.8f));
    }
    g.setFont (uiFont (10.5f, true));
    g.setColour (col::dim);
    g.drawText (proc.param (gid::mOn)->getValue() > 0.5f ? "A  B" : "MATHS is off", sr.reduced (8, 4), juce::Justification::topRight);
    g.setColour (col::faint);
    g.setFont (uiFont (10.5f));
    g.drawFittedText ("Function generators: they rise, fall, and loop (CYCLE). The end of a rise or cycle fires drum tracks; "
                      "their levels modulate sounds. X-MOD lets each one speed up or slow down the other.",
                      scopeArea.getX(), 188, scopeArea.getWidth(), 28, juce::Justification::topLeft, 2);

    // mod slot header
    g.setColour (col::dim);
    g.setFont (uiFont (10.5f, true).withExtraKerningFactor (0.1f));
    g.drawText ("MODULATION   source  ->  track  /  destination  /  amount", 780, 4, 480, 14, juce::Justification::left);
    g.drawText ("PULSES -> TRACKS", 780, 141, 300, 14, juce::Justification::left);
}

void MathsPanel::refresh()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int c = 0; c < 2; ++c)
    {
        const auto eor = proc.maths.eorCount[c].load(), eoc = proc.maths.eocCount[c].load();
        if (eor != lastEor[c]) { lastEor[c] = eor; flash[c * 2] = now; }
        if (eoc != lastEoc[c]) { lastEoc[c] = eoc; flash[c * 2 + 1] = now; }
    }
    repaint (scopeArea);
    repaint (8, 60, 20, 40);
    repaint (8, 168, 20, 40);
}

void MathsPanel::loadPreset (int which)
{
    auto set = [this] (const juce::String& id, float plain) { setParam (proc.param (id), plain); };
    auto chp = [&] (int c, int mp, float v) { set (mathsParamID (c, mp), v); };
    auto slot = [&] (int i, int src, int trk, int dst, float amt) {
        set (modSlotID (i, "src"), (float) src);
        set (modSlotID (i, "trk"), (float) trk);
        set (modSlotID (i, "par"), (float) dst);
        set (modSlotID (i, "amt"), amt);
    };
    auto pulseTo = [&] (int t, int mt) { set (trackParamID (t, tpMTrig), (float) mt); };
    // log256 step mapping: n = log(steps / 0.25) / log(256)
    auto steps = [] (double st) { return (float) (std::log (st / 0.25) / std::log (256.0)); };

    for (int i = 0; i < kNumModSlots; ++i) set (modSlotID (i, "amt"), 0.0f);
    for (int t = 0; t < kNumTracks; ++t) pulseTo (t, mtOff);
    for (int c = 0; c < 2; ++c)
    {
        chp (c, mpCurve, 0); chp (c, mpXMod, 0); chp (c, mpLevel, 1); chp (c, mpTrig, miOff);
        chp (c, mpSync, 1); chp (c, mpCycle, 1);
    }
    juce::String name;
    switch (which)
    {
        case 1:
            chp (0, mpRise, steps (1.5)); chp (0, mpFall, steps (2.5)); chp (0, mpXMod, 0.45f);
            chp (1, mpRise, steps (1.0)); chp (1, mpFall, steps (3.5)); chp (1, mpXMod, -0.5f); chp (1, mpCurve, 0.6f);
            slot (0, msSum, 0, mdCutoff, 0.35f);
            slot (1, msA, 3, mdDensity, 0.6f);
            pulseTo (5, mtAEoc); pulseTo (7, mtBEor);
            name = "cross-coupled pair";
            break;
        case 2:
            chp (0, mpRise, steps (1.25)); chp (0, mpFall, steps (1.25));
            chp (1, mpRise, steps (0.75)); chp (1, mpFall, steps (0.75));
            pulseTo (2, mtAEoc); pulseTo (5, mtBEoc);
            slot (0, msB, 3, mdPitch, 0.12f);
            name = "5 against 3";
            break;
        case 3: // A throws the ball once a bar; while A is high it holds B back, so B's pulses speed up as A falls
            chp (0, mpRise, steps (0.25)); chp (0, mpFall, steps (16)); chp (0, mpCurve, -0.6f);
            chp (1, mpRise, steps (0.4)); chp (1, mpFall, steps (0.8)); chp (1, mpXMod, -0.85f);
            chp (1, mpTrig, miOtherEoc);
            set (gid::mQuant, 1.0f);
            pulseTo (6, mtBEoc); pulseTo (8, mtAEor);
            slot (0, msA, 7, mdLevel, -0.5f);
            name = "bouncing ball";
            break;
        case 4:
            chp (0, mpSync, 0); chp (1, mpSync, 0);
            chp (0, mpRise, 0.21f); chp (0, mpFall, 0.37f); chp (0, mpXMod, 0.3f);
            chp (1, mpRise, 0.28f); chp (1, mpFall, 0.31f); chp (1, mpXMod, -0.35f);
            set (gid::mQuant, 1.0f);
            pulseTo (2, mtAEor); pulseTo (3, mtBEoc); pulseTo (9, mtAnyEoc);
            slot (0, msDiff, 0, mdPan, 0.5f);
            name = "drift";
            break;
        case 5:
            chp (0, mpRise, steps (24)); chp (0, mpFall, steps (40)); chp (0, mpCurve, 0.3f);
            chp (1, mpRise, steps (10)); chp (1, mpFall, steps (14)); chp (1, mpLevel, -0.6f);
            slot (0, msSum, 0, mdCutoff, 0.55f);
            slot (1, msA, 0, mdDrive, 0.4f);
            name = "breathing filter";
            break;
        default:
            name = "reset";
            break;
    }
    if (which != 9) set (gid::mOn, 1.0f);
    if (onStatus) onStatus ("MATHS: " + name);
}

//==============================================================================
SetupPanel::SetupPanel (GeomancerProcessor& p)
    : proc (p),
      kitBtn ("LOAD KIT FOLDER...", [this] { loadKitFolder(); }),
      clearKitBtn ("CLEAR KIT", [this] {
          for (int t = 0; t < kNumTracks; ++t) { proc.samples.clearSlot (t); proc.setMissing (t, {}); }
          proc.markDirty();
          if (onKitChanged) onKitChanged();
      }),
      relinkBtn ("FIND MISSING...", [this] { relink(); }),
      builtinBtn ("BUILT-IN KIT", [this] {
          proc.loadDefaultKit();
          proc.markDirty();
          if (onStatus) onStatus ("built-in synth kit loaded");
          if (onKitChanged) onKitChanged();
      }),
      demoBtn ("STARTER BEAT", [this] {
          auto& pat = proc.bank.patterns[proc.viewPattern];
          proc.pushUndo (proc.viewPattern);
          pat.clear();
          auto put = [&] (int t, std::initializer_list<int> steps, uint8_t vel) {
              for (int s : steps) { auto st = pat.step (t, s); st.on = true; st.vel = vel; pat.setStep (t, s, st); }
          };
          put (0, { 0, 6, 10 }, 120);
          put (1, { 4, 12 }, 110);
          put (2, { 0, 2, 4, 6, 8, 10, 12, 14 }, 90);
          put (2, { 1, 3, 5, 7, 9, 11, 13, 15 }, 45);
          put (3, { 14 }, 100);
          proc.markDirty();
          if (onKitChanged) onKitChanged();
      })
{
    auto& s = proc.apvts;
    seqSound.bind (s, gid::seqSound);
    midiOut.bind (s, gid::midiOut);
    patNotes.bind (s, gid::patNotes);
    lock.bind (s, gid::lock);
    seed.bind (s, gid::seed);
    seed.colour = col::maths;
    for (auto* c : std::initializer_list<juce::Component*> { &seqSound, &midiOut, &patNotes, &lock, &seed, &kitBtn, &clearKitBtn, &relinkBtn, &demoBtn, &builtinBtn })
        addAndMakeVisible (c);
    lock.setTooltip ("Randomness repeats identically on every pass: what you hear is what REAPER renders");
    midiOut.setTooltip ("Send the sequencer's notes out as MIDI (REAPER: track record mode 'Record: output (MIDI)')");
}

void SetupPanel::resized()
{
    int y = 12;
    for (auto* t : { &seqSound, &midiOut, &patNotes, &lock })
    {
        t->setBounds (12, y, 190, 26);
        y += 32;
    }
    seed.setBounds (12, y, 90, 40);
    kitBtn.setBounds (230, 12, 170, 26);
    relinkBtn.setBounds (230, 44, 170, 26);
    builtinBtn.setBounds (230, 76, 170, 26);
    clearKitBtn.setBounds (230, 108, 170, 26);
    demoBtn.setBounds (230, 140, 170, 26);
}

void SetupPanel::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    auto r = juce::Rectangle<int> (430, 8, 836, 206);
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r.toFloat(), 5.0f);
    r = r.reduced (12, 8);
    g.setFont (uiFont (12.0f));
    const juce::String text =
        "MIDI IN   Each track answers its own note (default 36 = C2 in REAPER, upward to 51). "
        "Notes 0-15 pick patterns 1-16, switching on the SWITCH grid (Bar by default); note 16 holds FILL; program changes pick patterns too.\n"
        "ARRANGE   In REAPER, the sequencer locks to the transport. Put pattern notes (0-15) in a MIDI item on this track and the song follows them. "
        "Or skip the internal sequencer: turn off SEQ PLAYS SOUNDS and drive the pads from REAPER's piano roll.\n"
        "RECORD   Audio: render or record the track as usual. Per-drum stems: set a track's OUTPUT to Out 1-16, then use REAPER's "
        "routing to send those plugin outputs to their own tracks. MIDI: enable SEQ -> MIDI OUT and set the track's record mode to 'Record: output (MIDI)'.\n"
        "PATTERNS   Click a slot to switch (on the SWITCH grid). Shift-click slots to build a CHAIN that plays them in order, "
        "each for its own length; a plain click clears it. Right-click a slot to copy it into the current one.\n"
        "GRID   Click = on/off, drag = paint, shift-drag = velocity, wheel = velocity, right-click = step options. "
        "Drag LEN / SWG up and down. Drop samples or a whole folder onto the rows. Space = play, Ctrl+Z / Ctrl+Shift+Z = undo / redo.";
    g.setColour (col::text.withAlpha (0.85f));
    g.drawFittedText (text, r, juce::Justification::topLeft, 14, 1.0f);
}

void SetupPanel::loadKitFolder()
{
    auto start = proc.sampleDir().isNotEmpty() ? juce::File (proc.sampleDir()) : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of drum samples", start);
    juce::Component::SafePointer<SetupPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc) {
                              if (safe == nullptr) return;
                              const auto dir = fc.getResult();
                              if (! dir.isDirectory()) return;
                              auto files = dir.findChildFiles (juce::File::findFiles, false);
                              if (files.isEmpty()) files = dir.findChildFiles (juce::File::findFiles, true);
                              files.sort();
                              int t = 0;
                              juce::String err;
                              for (auto& f : files)
                                  if (t < kNumTracks && SamplePool::isAudioFile (f) && safe->proc.loadSample (t, f, err)) ++t;
                              if (safe->onStatus) safe->onStatus ("kit: loaded " + juce::String (t) + " samples from " + dir.getFileName());
                              if (safe->onKitChanged) safe->onKitChanged();
                          });
}

void SetupPanel::relink()
{
    chooser = std::make_unique<juce::FileChooser> ("Search this folder (and below) for missing samples",
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    juce::Component::SafePointer<SetupPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc) {
                              if (safe == nullptr) return;
                              const auto dir = fc.getResult();
                              if (! dir.isDirectory()) return;
                              int found = 0;
                              for (int t = 0; t < kNumTracks; ++t)
                              {
                                  const auto miss = safe->proc.missing (t);
                                  if (miss.isEmpty()) continue;
                                  const auto name = juce::File (miss).getFileName();
                                  auto hits = dir.findChildFiles (juce::File::findFiles, true, name);
                                  juce::String err;
                                  if (! hits.isEmpty() && safe->proc.loadSample (t, hits[0], err)) ++found;
                              }
                              if (safe->onStatus) safe->onStatus ("relinked " + juce::String (found) + " sample(s)");
                              if (safe->onKitChanged) safe->onKitChanged();
                          });
}
} // namespace gm
