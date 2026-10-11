#include "SoundPanel.h"
#include "StepGrid.h"
#include "../DrumSynth.h"

namespace gm
{
SoundPanel::SoundPanel (GeomancerProcessor& p)
    : proc (p), waveform (p),
      loadBtn ("LOAD", [this] { if (onBrowse) onBrowse(); }),
      prevBtn ("<", [this] { browse (-1); }), nextBtn (">", [this] { browse (1); }),
      clearBtn ("X", [this] {
          proc.samples.clearSlot (track);
          proc.setMissing (track, {});
          proc.markDirty();
          waveform.repaint();
          if (onSampleChanged) onSampleChanged();
      }),
      diceBtn ("DICE", [this] { rollSound(); }, col::maths),
      tune ("Tune"), fine ("Fine"), start ("Start", col::accent2), end ("End"), stretch ("Stretch"), grain ("Grain"),
      warp ("Warp"), reverse ("REV"),
      attack ("Attack"), hold ("Hold"), decay ("Decay"), pAmt ("P.Env"), pDec ("P.Dec"), mode ("Env"),
      cutoff ("Cutoff", col::accent2), reso ("Reso", col::accent2), fEnv ("F.Env", col::accent2), fDec ("F.Dec", col::accent2),
      fType ("Filter"),
      crush ("Bits"), down ("Down"), drive ("Drive"), satMix ("Mix"), sat ("Sat"),
      level ("Level"), pan ("Pan"), velSens ("Vel"), choke ("Choke"), poly ("Voices"),
      rndSnd ("Rnd Snd", col::maths), rndSeq ("Rnd Seq", col::maths), out ("Output"), note ("Note"), mTrig ("Maths")
{
    tune.bipolar = fine.bipolar = pAmt.bipolar = fEnv.bipolar = pan.bipolar = true;
    for (auto* c : std::initializer_list<juce::Component*> {
             &waveform, &loadBtn, &prevBtn, &nextBtn, &clearBtn, &diceBtn, &tune, &fine, &start, &end, &stretch, &grain,
             &warp, &reverse, &attack, &hold, &decay, &pAmt, &pDec, &mode, &cutoff, &reso, &fEnv, &fDec, &fType, &crush,
             &down, &drive, &satMix, &sat, &level, &pan, &velSens, &choke, &poly, &rndSnd, &rndSeq, &out, &note, &mTrig })
        addAndMakeVisible (c);
    waveform.onLoaded = [this] { if (onSampleChanged) onSampleChanged(); };
    prevBtn.setTooltip ("Previous sample in this folder");
    nextBtn.setTooltip ("Next sample in this folder");
    diceBtn.setTooltip ("Roll a new sound for this track (tune, envelope, filter, dirt)");
    mTrig.setTooltip ("Fire this track from a MATHS function generator pulse");
    setTrack (p.selectedTrack);
}

bool SoundPanel::isDragging() const
{
    for (auto* k : { &tune, &fine, &start, &end, &stretch, &grain, &attack, &hold, &decay, &pAmt, &pDec, &cutoff, &reso,
                     &fEnv, &fDec, &crush, &down, &drive, &satMix, &level, &pan, &velSens, &rndSnd, &rndSeq })
        if (k->slider.isMouseButtonDown()) return true;
    return false;
}

void SoundPanel::setTrack (int t)
{
    if (t == track) return;
    track = t;
    auto& s = proc.apvts;
    auto id = [t] (int tp) { return trackParamID (t, tp); };
    tune.bind (s, id (tpTune)); fine.bind (s, id (tpFine)); start.bind (s, id (tpStart)); end.bind (s, id (tpEnd));
    stretch.bind (s, id (tpStretch)); grain.bind (s, id (tpGrain)); warp.bind (s, id (tpWarp)); reverse.bind (s, id (tpReverse));
    attack.bind (s, id (tpAttack)); hold.bind (s, id (tpHold)); decay.bind (s, id (tpDecay)); pAmt.bind (s, id (tpPAmt));
    pDec.bind (s, id (tpPDec)); mode.bind (s, id (tpMode));
    cutoff.bind (s, id (tpCutoff)); reso.bind (s, id (tpReso)); fEnv.bind (s, id (tpFEnv)); fDec.bind (s, id (tpFDec));
    fType.bind (s, id (tpFType));
    crush.bind (s, id (tpCrush)); down.bind (s, id (tpDown)); drive.bind (s, id (tpDrive)); satMix.bind (s, id (tpSatMix));
    sat.bind (s, id (tpSat));
    level.bind (s, id (tpLevel)); pan.bind (s, id (tpPan)); velSens.bind (s, id (tpVelSens)); choke.bind (s, id (tpChoke));
    poly.bind (s, id (tpPoly));
    rndSnd.bind (s, id (tpRndSnd)); rndSeq.bind (s, id (tpRndSeq)); out.bind (s, id (tpOut)); note.bind (s, id (tpNote));
    mTrig.bind (s, id (tpMTrig));
    for (auto* c : { &warp, &mode, &fType, &sat, &choke, &poly, &out, &note, &mTrig })
        c->colour = col::track (t);
    waveform.repaint();
    repaint();
}

void SoundPanel::resized()
{
    groups.clear();
    // left column: sample
    loadBtn.setBounds (8, 4, 52, 22);
    prevBtn.setBounds (62, 4, 26, 22);
    nextBtn.setBounds (90, 4, 26, 22);
    clearBtn.setBounds (118, 4, 26, 22);
    waveform.setBounds (8, 30, 354, 104);

    constexpr int kw = 52, kh = 78, chipW = 58;
    auto knobRow = [&] (int x, int y, std::initializer_list<juce::Component*> knobs) {
        for (auto* k : knobs) { k->setBounds (x, y, kw, kh); x += kw; }
        return x;
    };
    auto chipCol = [&] (int x, int y, std::initializer_list<juce::Component*> chips) {
        for (auto* c : chips) { c->setBounds (x, y + 2, chipW, 37); y += 40; }
        return x + chipW;
    };
    auto group = [&] (const char* name, int x0, int x1, int y) { groups.push_back ({ name, { x0, y, x1 - x0, 104 } }); };

    // bottom-left: randomness + routing
    {
        const int y = 138;
        int x = knobRow (8, y + 2, { &rndSnd, &rndSeq });
        diceBtn.setBounds (x + 2, y + 30, 44, 24);
        x += 50;
        out.setBounds (x, y + 4, 66, 37);
        note.setBounds (x + 68, y + 4, 66, 37);
        mTrig.setBounds (x, y + 43, 134, 37);
    }

    // right side, two rows of groups
    int x = 374;
    const int y1 = 2, y2 = 110;
    int gx = x;
    x = knobRow (x + 4, y1 + 14, { &tune, &fine, &start, &end, &stretch, &grain });
    x = chipCol (x + 2, y1 + 12, { &warp, &reverse });
    group ("SAMPLE", gx, x + 4, y1);
    x += 12;
    gx = x;
    x = knobRow (x + 4, y1 + 14, { &attack, &hold, &decay, &pAmt, &pDec });
    x = chipCol (x + 2, y1 + 12, { &mode });
    group ("ENVELOPE", gx, x + 4, y1);

    x = 374;
    gx = x;
    x = knobRow (x + 4, y2 + 14, { &cutoff, &reso, &fEnv, &fDec });
    x = chipCol (x + 2, y2 + 12, { &fType });
    group ("FILTER", gx, x + 4, y2);
    x += 12;
    gx = x;
    x = knobRow (x + 4, y2 + 14, { &drive, &satMix, &crush, &down });
    x = chipCol (x + 2, y2 + 12, { &sat });
    group ("DIRT", gx, x + 4, y2);
    x += 12;
    gx = x;
    x = knobRow (x + 4, y2 + 14, { &level, &pan, &velSens });
    x = chipCol (x + 2, y2 + 12, { &choke, &poly });
    group ("OUTPUT", gx, x + 4, y2);
    reverse.setBounds (reverse.getBounds().withTrimmedTop (15).withHeight (22));
}

void SoundPanel::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    const auto tc = col::track (track);
    g.setFont (uiFont (13.0f, true));
    g.setColour (tc);
    juce::String name = "T" + juce::String (track + 1);
    if (auto* s = proc.samples.slot (track)) name += "  " + s->name;
    g.drawFittedText (name, 150, 4, 212, 22, juce::Justification::centredLeft, 1);
    for (auto& gr : groups)
    {
        g.setColour (col::panel2);
        g.fillRoundedRectangle (gr.r.toFloat(), 5.0f);
        g.setColour (col::dim);
        g.setFont (uiFont (10.5f, true).withExtraKerningFactor (0.1f));
        g.drawText (gr.name, gr.r.getX() + 8, gr.r.getY() + 2, 120, 12, juce::Justification::left);
    }
}

void SoundPanel::browse (int dir)
{
    auto* s = proc.samples.slot (track);
    if (s != nullptr && SamplePool::isSynth (s->source))
    {
        const auto& names = drumsynth::names();
        const int idx = (names.indexOf (s->name) + dir + names.size()) % names.size();
        juce::String err;
        proc.samples.loadSourceIntoSlot (track, "synth:" + names[idx], err);
        proc.setMissing (track, {});
        proc.markDirty();
        proc.audition[(size_t) track] = 110;
        if (onStatus) onStatus ("built-in kit: " + names[idx]);
        waveform.repaint();
        repaint();
        if (onSampleChanged) onSampleChanged();
        return;
    }
    juce::File current = s != nullptr ? s->file : juce::File (proc.missing (track));
    juce::File folder = current.getParentDirectory();
    if (! folder.isDirectory())
    {
        if (proc.sampleDir().isEmpty()) return;
        folder = juce::File (proc.sampleDir());
    }
    auto files = folder.findChildFiles (juce::File::findFiles, false);
    files.sort();
    juce::Array<juce::File> audio;
    for (auto& f : files)
        if (SamplePool::isAudioFile (f)) audio.add (f);
    if (audio.isEmpty()) return;
    int idx = audio.indexOf (current);
    idx = idx < 0 ? (dir > 0 ? 0 : audio.size() - 1) : (idx + dir + audio.size()) % audio.size();
    juce::String err;
    if (proc.loadSample (track, audio[idx], err))
    {
        proc.audition[(size_t) track] = 110;
        if (onStatus) onStatus (juce::String (idx + 1) + "/" + juce::String (audio.size()) + "  " + audio[idx].getFileName());
    }
    waveform.repaint();
    repaint();
    if (onSampleChanged) onSampleChanged();
}

void SoundPanel::rollSound()
{
    auto& r = proc.uiRng;
    auto set = [&] (int tp, float plain) { setParam (proc.trackParam (track, tp), plain); };
    set (tpTune, std::round (r.bi() * 7.0f));
    set (tpFine, r.bi() * 20.0f);
    set (tpStart, r.uni() < 0.25f ? r.uni() * 0.15f : 0.0f);
    set (tpReverse, r.uni() < 0.12f ? 1.0f : 0.0f);
    set (tpWarp, r.uni() < 0.2f ? 1.0f : 0.0f);
    set (tpStretch, std::exp2 (r.bi() * 1.2f));
    set (tpGrain, 15.0f + r.uni() * 120.0f);
    set (tpDecay, r.uni() < 0.35f ? 10000.0f : 40.0f + r.uni() * r.uni() * 1500.0f);
    set (tpHold, r.uni() * r.uni() * 120.0f);
    set (tpPAmt, r.uni() < 0.35f ? r.bi() * 18.0f : 0.0f);
    set (tpPDec, 10.0f + r.uni() * 250.0f);
    const int ft = r.uni() < 0.45f ? 0 : 1 + (int) (r.uni() * 3.0f);
    set (tpFType, (float) ft);
    set (tpCutoff, 200.0f * std::exp2 (r.uni() * 6.5f));
    set (tpReso, r.uni() * 0.75f);
    set (tpFEnv, r.uni() < 0.4f ? r.bi() * 0.6f : 0.0f);
    set (tpDrive, r.uni() < 0.5f ? r.uni() * 22.0f : 0.0f);
    set (tpSat, (float) (int) (r.uni() * 5.0f));
    set (tpCrush, r.uni() < 0.15f ? 4.0f + r.uni() * 8.0f : 16.0f);
    set (tpDown, r.uni() < 0.12f ? 2.0f + r.uni() * 10.0f : 1.0f);
    proc.audition[(size_t) track] = 110;
    if (onStatus) onStatus ("T" + juce::String (track + 1) + " sound rolled");
}
} // namespace gm
