#include "Header.h"

namespace gm
{
Header::Header (GeomancerProcessor& p)
    : proc (p),
      playBtn ("PLAY", [this] {
          auto* prm = proc.param (gid::play);
          setParam (prm, prm->getValue() > 0.5f ? 0.0f : 1.0f);
      }, col::accent2),
      copyBtn ("COPY", [this] {
          clipboard = std::make_unique<PatternSnapshot>();
          clipboard->take (proc.bank.patterns[proc.viewPattern]);
          status = "copied pattern " + juce::String (proc.viewPattern + 1);
          repaint();
      }),
      pasteBtn ("PASTE", [this] {
          if (clipboard == nullptr) return;
          proc.pushUndo (proc.viewPattern);
          clipboard->restore (proc.bank.patterns[proc.viewPattern]);
          status = "pasted into " + juce::String (proc.viewPattern + 1);
          if (onPatternEdited) onPatternEdited();
      }),
      clearBtn ("CLEAR", [this] {
          proc.pushUndo (proc.viewPattern);
          proc.bank.patterns[proc.viewPattern].clear();
          status = "cleared pattern " + juce::String (proc.viewPattern + 1);
          if (onPatternEdited) onPatternEdited();
      }),
      diceBtn ("DICE", [this] { showDiceMenu(); }, col::maths),
      undoBtn ("UNDO", [this] { if (proc.undo() && onPatternEdited) onPatternEdited(); }),
      redoBtn ("REDO", [this] { if (proc.redo() && onPatternEdited) onPatternEdited(); }),
      fillBtn ("FILL", col::solo), clipBtn ("CLIP", col::accent),
      quant ("Switch"), clock ("Clock"), looper ("Loop"),
      swing ("Swing"), rndSnd ("Rnd Snd", col::maths), rndSeq ("Rnd Seq", col::maths),
      djFilter ("Filter", col::accent2), djReso ("Reso", col::accent2), drive ("Drive"), master ("Master"),
      satType ("Sat"), bpm ("Tempo", col::accent2)
{
    for (auto* c : std::initializer_list<juce::Component*> { &playBtn, &copyBtn, &pasteBtn, &clearBtn, &diceBtn, &undoBtn, &redoBtn,
                                                             &fillBtn, &clipBtn, &quant, &clock, &looper, &swing, &rndSnd, &rndSeq,
                                                             &djFilter, &djReso, &drive, &master, &satType, &bpm })
        addAndMakeVisible (c);

    fillBtn.momentary = true;
    djFilter.bipolar = true;
    auto& s = proc.apvts;
    fillBtn.bind (s, gid::fill);
    clipBtn.bind (s, gid::clip);
    quant.bind (s, gid::quant);
    clock.bind (s, gid::clock);
    looper.bind (s, gid::looper);
    looper.colour = col::accent2;
    looper.setTooltip ("Looper (beat repeat): replay the current slice of the pattern; Off returns to the song position");
    swing.bind (s, gid::swing);
    rndSnd.bind (s, gid::gRndSnd);
    rndSeq.bind (s, gid::gRndSeq);
    djFilter.bind (s, gid::djFilter);
    djReso.bind (s, gid::djReso);
    drive.bind (s, gid::mDrive);
    master.bind (s, gid::master);
    satType.bind (s, gid::mSat);
    bpm.bind (s, gid::bpm);

    playBtn.setTooltip ("Run the sequencer from its own clock (space bar). In a DAW it follows the host transport anyway.");
    diceBtn.setTooltip ("Generate: cast a geomantic chart, randomize, mutate");
    quant.setTooltip ("Pattern switch: when a newly picked pattern takes over");
    fillBtn.setTooltip ("Hold: steps with the Fill condition play (MIDI note 16 does the same)");
    clock.setVisible (proc.wrapperType != juce::AudioProcessor::wrapperType_Standalone);
}

juce::Rectangle<int> Header::patternRect (int i) const
{
    const int x0 = 350, y0 = 12, w = 34, h = 25, gap = 3;
    return { x0 + (i % 8) * (w + gap), y0 + (i / 8) * (h + gap), w, h };
}

void Header::resized()
{
    playBtn.setBounds (196, 12, 64, 25);
    bpm.setBounds (264, 4, 52, 66);
    clock.setBounds (196, 40, 64, 24);
    clock.showTitle = false;
    clock.prefix = "Clock: ";

    const int tx = 350 + 8 * 37 + 8;
    copyBtn.setBounds (tx, 12, 44, 25);
    pasteBtn.setBounds (tx + 46, 12, 44, 25);
    clearBtn.setBounds (tx + 92, 12, 44, 25);
    undoBtn.setBounds (tx, 40, 44, 25);
    redoBtn.setBounds (tx + 46, 40, 44, 25);
    diceBtn.setBounds (tx + 92, 40, 44, 25);
    fillBtn.setBounds (tx + 142, 12, 44, 25);
    quant.setBounds (tx + 142, 40, 44, 25);
    quant.showTitle = false;
    looper.setBounds (tx + 190, 8, 46, 58);

    int x = tx + 242;
    for (auto* k : { &swing, &rndSnd, &rndSeq })
    {
        k->setBounds (x, 4, 46, 66);
        x += 46;
    }
    x += 4;
    for (auto* k : { &djFilter, &djReso, &drive })
    {
        k->setBounds (x, 4, 46, 66);
        x += 46;
    }
    satType.setBounds (x, 4, 44, 36);
    clipBtn.setBounds (x + 2, 44, 40, 22);
    x += 46;
    master.setBounds (x, 4, 46, 66);
}

static void drawFigure (juce::Graphics& g, juce::Rectangle<float> r, uint8_t bits, juce::Colour c)
{
    const float rowH = r.getHeight() / 4.0f, d = std::min (rowH * 0.62f, r.getWidth() * 0.28f);
    g.setColour (c);
    for (int row = 0; row < 4; ++row)
    {
        const float cy = r.getY() + rowH * (row + 0.5f);
        const bool single = (bits >> (3 - row)) & 1;
        if (single)
            g.fillEllipse (r.getCentreX() - d * 0.5f, cy - d * 0.5f, d, d);
        else
            for (float dx : { -0.55f, 0.55f })
                g.fillEllipse (r.getCentreX() + dx * d * 1.3f - d * 0.5f, cy - d * 0.5f, d, d);
    }
}

void Header::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());

    // logo: the current figure (click to cast a chart for the whole kit)
    drawFigure (g, glyphRect().toFloat(), figure, col::accent);
    g.setColour (col::text);
    g.setFont (uiFont (17.0f, true).withExtraKerningFactor (0.12f));
    g.drawText ("GEOMANCER", 52, 12, 140, 26, juce::Justification::left);
    g.setColour (col::dim);
    g.setFont (uiFont (11.0f));
    g.drawFittedText (status.isEmpty() ? juce::String ("click the figure to cast") : status, 52, 38, 138, 26,
                      juce::Justification::topLeft, 2);

    // pattern slots
    const int cur = proc.uiCurrentPattern.load(), q = proc.uiQueuedPattern.load();
    const bool blink = (juce::Time::getMillisecondCounter() / 180) % 2 == 0;
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto r = patternRect (i).toFloat();
        const bool view = i == proc.viewPattern;
        const bool empty = proc.bank.patterns[i].isEmpty();
        auto bg = view ? col::accent : col::raised;
        if (i == q && blink) bg = col::accent.withAlpha (0.45f);
        g.setColour (bg);
        g.fillRoundedRectangle (r, 3.0f);
        if (i == cur && proc.uiRunning.load())
        {
            g.setColour (col::accent2);
            g.fillRoundedRectangle (r.removeFromBottom (3.0f).reduced (3.0f, 0), 1.5f);
        }
        g.setColour (view ? col::bg : (empty ? col::faint : col::text));
        g.setFont (uiFont (12.0f, true));
        g.drawText (juce::String (i + 1), patternRect (i), juce::Justification::centred);
    }
    // chain order badges
    const int nChain = proc.chainLen.load();
    if (nChain >= 2)
    {
        int shown[kNumPatterns] {};
        for (int c = 0; c < nChain; ++c)
        {
            const int p = proc.chain[(size_t) c].load();
            auto r = patternRect (p).toFloat();
            const float bx = r.getRight() - 9.0f - (float) shown[p]++ * 8.0f;
            g.setColour (col::maths);
            g.fillEllipse (bx, r.getY() - 3.0f, 9.0f, 9.0f);
            g.setColour (col::bg);
            g.setFont (uiFont (7.5f, true));
            g.drawText (juce::String (c + 1), juce::Rectangle<float> (bx, r.getY() - 3.0f, 9.0f, 9.0f), juce::Justification::centred);
        }
    }

    // master meter
    const float m = juce::jlimit (0.0f, 1.0f, meter);
    auto mr = juce::Rectangle<float> ((float) getWidth() - 6.0f, 8.0f, 4.0f, (float) getHeight() - 16.0f);
    g.setColour (col::cell);
    g.fillRoundedRectangle (mr, 2.0f);
    g.setColour (m > 0.97f ? col::mute : col::accent2);
    g.fillRoundedRectangle (mr.withTop (mr.getBottom() - mr.getHeight() * std::sqrt (m)), 2.0f);
}

void Header::mouseDown (const juce::MouseEvent& e)
{
    if (glyphRect().expanded (6).contains (e.getPosition()))
    {
        castKit();
        return;
    }
    for (int i = 0; i < kNumPatterns; ++i)
        if (patternRect (i).contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                m.addItem (1, "Copy pattern " + juce::String (i + 1) + " here (into " + juce::String (proc.viewPattern + 1) + ")",
                           i != proc.viewPattern);
                m.addItem (2, "Clear pattern " + juce::String (i + 1));
                m.addItem (3, "Clear chain", proc.chainLen.load() > 0);
                juce::Component::SafePointer<Header> safe (this);
                const int dest = proc.viewPattern;
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, i, dest] (int r) {
                    if (safe == nullptr || r == 0) return;
                    auto& pr = safe->proc;
                    if (r == 1) { pr.pushUndo (dest); pr.bank.patterns[dest].copyFrom (pr.bank.patterns[i]); }
                    if (r == 2) { pr.pushUndo (i); pr.bank.patterns[i].clear(); }
                    if (r == 3) { pr.chainLen = 0; safe->status = "chain cleared"; pr.markDirty(); safe->repaint(); return; }
                    if (safe->onPatternEdited) safe->onPatternEdited();
                });
                return;
            }
            if (e.mods.isShiftDown()) // build a chain: shift-click patterns in playing order
            {
                const int n = proc.chainLen.load();
                if (n == 0) { proc.chain[0] = proc.viewPattern; proc.chain[1] = i; proc.chainLen = proc.viewPattern == i ? 1 : 2; }
                else if (n < kNumPatterns) { proc.chain[(size_t) n] = i; proc.chainLen = n + 1; }
                status = "chain: " + proc.chainText();
                proc.markDirty();
                repaint();
                return;
            }
            if (proc.chainLen.load() > 0) { proc.chainLen = 0; status = "chain cleared"; proc.markDirty(); }
            proc.viewPattern = i;
            proc.guiPatternRequest = i;
            if (onViewChanged) onViewChanged();
            repaint();
            return;
        }
}

void Header::castKit()
{
    auto& pat = proc.bank.patterns[proc.viewPattern];
    proc.pushUndo (proc.viewPattern);
    status = "Judge: " + gen::geomancyKit (pat, proc.uiRng);
    // show the Judge figure itself in the logo
    for (uint8_t b = 0; b < 16; ++b)
        if (status.endsWith (gen::figureName (b))) figure = b;
    if (onPatternEdited) onPatternEdited();
    repaint();
}

void Header::showDiceMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Whole pattern");
    m.addItem (1, "Cast geomantic chart (all tracks)");
    m.addItem (2, "Random beat (loaded tracks)");
    m.addItem (3, "Mutate a little");
    m.addItem (4, "Mutate a lot");
    m.addSectionHeader ("Sound");
    m.addItem (5, "Nudge every loaded sound (tune / decay / filter)");
    juce::Component::SafePointer<Header> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&diceBtn), [safe] (int r) {
        if (safe == nullptr || r == 0) return;
        auto& pr = safe->proc;
        auto& pat = pr.bank.patterns[pr.viewPattern];
        if (r == 1) { safe->castKit(); return; }
        if (r == 5)
        {
            for (int t = 0; t < kNumTracks; ++t)
            {
                if (pr.samples.slot (t) == nullptr) continue;
                auto nudge = [&] (int tp, float spread) {
                    auto* prm = pr.trackParam (t, tp);
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, prm->getValue() + pr.uiRng.bi() * spread));
                    prm->endChangeGesture();
                };
                nudge (tpTune, 0.04f);
                nudge (tpDecay, 0.12f);
                nudge (tpCutoff, 0.1f);
            }
            safe->status = "kit nudged";
            safe->repaint();
            return;
        }
        pr.pushUndo (pr.viewPattern);
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (pr.samples.slot (t) == nullptr) continue;
            if (r == 2)
            {
                static const float density[] = { 0.3f, 0.2f, 0.55f, 0.15f };
                gen::random (pat, t, density[t % 4], pr.uiRng);
            }
            else
                gen::mutate (pat, t, r == 3 ? 0.25f : 0.8f, pr.uiRng);
        }
        safe->status = r == 2 ? "rolled a beat" : "mutated";
        if (safe->onPatternEdited) safe->onPatternEdited();
    });
}

void Header::refresh()
{
    const int cur = proc.uiCurrentPattern.load(), q = proc.uiQueuedPattern.load();
    const bool running = proc.uiRunning.load();
    const float m = proc.uiMasterMeter.load();
    const bool standalone = proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    const bool internal = standalone || proc.param (gid::clock)->getValue() > 0.5f;
    playBtn.lit = proc.param (gid::play)->getValue() > 0.5f;
    playBtn.text = playBtn.lit ? "STOP" : "PLAY";
    playBtn.repaint();
    bpm.setVisible (internal);

    // the view follows pattern changes that came from MIDI / automation
    if (cur != shownCurrent && shownCurrent >= 0 && running && q < 0 && proc.viewPattern != cur)
    {
        proc.viewPattern = cur;
        if (onViewChanged) onViewChanged();
    }
    if (cur != shownCurrent || q != shownQueued || running != shownRunning || proc.viewPattern != shownView || q >= 0)
    {
        shownCurrent = cur;
        shownQueued = q;
        shownRunning = running;
        shownView = proc.viewPattern;
        repaint (patternRect (0).getUnion (patternRect (15)).expanded (4)); // slots only, not the knobs
    }
    if (std::abs (m - meter) > 0.01f)
    {
        meter = m;
        repaint (getWidth() - 8, 0, 8, getHeight());
    }
}
} // namespace gm
