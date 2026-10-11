#include "Params.h"

namespace gm
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;
using API = juce::AudioParameterInt;
using Range = juce::NormalisableRange<float>;

static const TrackParamInfo kTrackParams[tpCount] = {
    { "level", "Level" }, { "pan", "Pan" }, { "mute", "Mute" }, { "solo", "Solo" },
    { "tune", "Tune" }, { "fine", "Fine" }, { "warp", "Warp" }, { "stretch", "Stretch" }, { "grain", "Grain" },
    { "rev", "Reverse" }, { "start", "Start" }, { "end", "End" },
    { "atk", "Attack" }, { "hold", "Hold" }, { "dec", "Decay" }, { "mode", "Env Mode" },
    { "pamt", "Pitch Env" }, { "pdec", "Pitch Decay" },
    { "ftype", "Filter" }, { "cut", "Cutoff" }, { "res", "Resonance" }, { "fenv", "Filter Env" }, { "fdec", "Filter Decay" },
    { "crush", "Bits" }, { "down", "Downsample" }, { "drive", "Drive" }, { "sat", "Sat Type" }, { "satmix", "Sat Mix" },
    { "choke", "Choke" }, { "poly", "Voices" }, { "out", "Output" },
    { "rndsnd", "Rnd Sound" }, { "rndseq", "Rnd Seq" }, { "vel", "Vel Sens" }, { "note", "MIDI Note" },
    { "mtrig", "Maths Trig" },
};

const TrackParamInfo& trackParamInfo (int tp) { return kTrackParams[tp]; }
juce::String trackParamID (int track, int tp) { return "t" + juce::String (track + 1) + "_" + kTrackParams[tp].suffix; }

static const char* kMathsSuffix[mpCount] = { "rise", "fall", "curve", "cycle", "sync", "trig", "xmod", "level" };
juce::String mathsParamID (int ch, int mp) { return juce::String (ch == 0 ? "mA_" : "mB_") + kMathsSuffix[mp]; }
juce::String modSlotID (int slot, const char* what) { return "md" + juce::String (slot + 1) + "_" + what; }

juce::StringArray modSrcNames() { return { "A", "B", "OR", "SUM", "A-B" }; }
juce::StringArray modDstNames() { return { "Pitch", "Cutoff", "Decay", "Level", "Pan", "Start", "Drive", "Density" }; }
juce::StringArray mTrigNames() { return { "Off", "A rise", "A cycle", "B rise", "B cycle", "A|B cycle" }; }
juce::StringArray mTrigInNames()
{
    juce::StringArray s { "Off", "1/16", "1/8", "1/4", "Bar", "Other cycle" };
    for (int t = 0; t < kNumTracks; ++t) s.add ("Track " + juce::String (t + 1));
    return s;
}
juce::StringArray satNames() { return { "Soft", "Hard", "Fold", "Tube", "Rectify" }; }
juce::StringArray filterNames() { return { "Off", "LP", "BP", "HP" }; }
juce::StringArray quantNames() { return { "Instant", "Beat", "Bar", "2 Bars", "4 Bars" }; }
juce::StringArray looperNames() { return { "Off", "1/32", "1/16", "1/8", "1/4", "1/2", "Bar" }; }
double looperPpq (int i, double barLen)
{
    static const double q[] = { 0.0, 0.125, 0.25, 0.5, 1.0, 2.0 };
    return i <= 0 ? 0.0 : (i >= 6 ? barLen : q[i]);
}
juce::StringArray outNames()
{
    juce::StringArray s { "Main" };
    for (int i = 0; i < kNumAux; ++i) s.add ("Out " + juce::String (i + 1));
    return s;
}

double mathsTimeSeconds (float n) { return 0.002 * std::pow (10000.0, (double) clampf (n, 0, 1)); }
double mathsTimeSteps (float n) { return 0.25 * std::pow (256.0, (double) clampf (n, 0, 1)); }
juce::String mathsTimeText (float n, bool synced)
{
    if (synced)
    {
        const double st = mathsTimeSteps (n);
        return juce::String (st, st < 10 ? 2 : 1) + " st";
    }
    const double s = mathsTimeSeconds (n);
    return s < 1.0 ? juce::String (s * 1000.0, 0) + " ms" : juce::String (s, 2) + " s";
}

//==============================================================================
static juce::String msText (float v, int) { return v < 1000.0f ? juce::String (v, v < 10 ? 1 : 0) + " ms" : juce::String (v / 1000.0f, 2) + " s"; }
static juce::String hzText (float v, int) { return v < 1000.0f ? juce::String (v, 0) + " Hz" : juce::String (v / 1000.0f, 1) + " kHz"; }
static juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
static juce::String dbText (float v, int) { return v <= -60.0f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; }
static juce::String stText (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 2) + " st"; }
static juce::String panText (float v, int)
{
    const int p = juce::roundToInt (v * 50.0f);
    return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
}
static float numFromText (const juce::String& s) { return s.retainCharacters ("-0123456789.").getFloatValue(); }

static Range skewed (float lo, float hi, float centre, float interval = 0.0f)
{
    Range r (lo, hi, interval);
    r.setSkewForCentre (centre);
    return r;
}

static std::unique_ptr<APF> fparam (const juce::String& id, const juce::String& name, Range range, float def,
                                    std::function<juce::String (float, int)> toText, const juce::String& label = {})
{
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
                                  juce::AudioParameterFloatAttributes()
                                      .withLabel (label)
                                      .withStringFromValueFunction (std::move (toText))
                                      .withValueFromStringFunction (numFromText));
}

static void addTrack (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int t)
{
    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("track" + juce::String (t + 1),
                                                                       "Track " + juce::String (t + 1), " | ");
    const juce::String pre = "T" + juce::String (t + 1) + " ";
    auto id = [t] (int tp) { return trackParamID (t, tp); };
    auto nm = [&pre] (int tp) { return pre + kTrackParams[tp].name; };

    group->addChild (fparam (id (tpLevel), nm (tpLevel), Range (-60.0f, 12.0f), 0.0f, dbText));
    group->addChild (fparam (id (tpPan), nm (tpPan), Range (-1.0f, 1.0f), 0.0f, panText));
    group->addChild (std::make_unique<APB> (juce::ParameterID { id (tpMute), 1 }, nm (tpMute), false));
    group->addChild (std::make_unique<APB> (juce::ParameterID { id (tpSolo), 1 }, nm (tpSolo), false));
    group->addChild (fparam (id (tpTune), nm (tpTune), Range (-24.0f, 24.0f, 0.01f), 0.0f, stText));
    group->addChild (fparam (id (tpFine), nm (tpFine), Range (-100.0f, 100.0f, 0.1f), 0.0f,
                             [] (float v, int) { return juce::String (v, 0) + " ct"; }));
    group->addChild (std::make_unique<APC> (juce::ParameterID { id (tpWarp), 1 }, nm (tpWarp), juce::StringArray { "Repitch", "Stretch" }, 0));
    group->addChild (fparam (id (tpStretch), nm (tpStretch), skewed (0.25f, 4.0f, 1.0f), 1.0f,
                             [] (float v, int) { return juce::String (v, 2) + "x"; }));
    group->addChild (fparam (id (tpGrain), nm (tpGrain), skewed (8.0f, 250.0f, 50.0f), 50.0f, msText));
    group->addChild (std::make_unique<APB> (juce::ParameterID { id (tpReverse), 1 }, nm (tpReverse), false));
    group->addChild (fparam (id (tpStart), nm (tpStart), Range (0.0f, 1.0f), 0.0f, pctText));
    group->addChild (fparam (id (tpEnd), nm (tpEnd), Range (0.0f, 1.0f), 1.0f, pctText));
    group->addChild (fparam (id (tpAttack), nm (tpAttack), skewed (0.0f, 500.0f, 30.0f), 0.0f, msText));
    group->addChild (fparam (id (tpHold), nm (tpHold), skewed (0.0f, 2000.0f, 150.0f), 0.0f, msText));
    group->addChild (fparam (id (tpDecay), nm (tpDecay), skewed (5.0f, 10000.0f, 400.0f), 10000.0f,
                             [] (float v, int i) { return v >= 9999.0f ? juce::String ("inf") : msText (v, i); }));
    group->addChild (std::make_unique<APC> (juce::ParameterID { id (tpMode), 1 }, nm (tpMode), juce::StringArray { "One-shot", "Gate" }, 0));
    group->addChild (fparam (id (tpPAmt), nm (tpPAmt), Range (-48.0f, 48.0f, 0.01f), 0.0f, stText));
    group->addChild (fparam (id (tpPDec), nm (tpPDec), skewed (1.0f, 2000.0f, 80.0f), 60.0f, msText));
    group->addChild (std::make_unique<APC> (juce::ParameterID { id (tpFType), 1 }, nm (tpFType), filterNames(), 0));
    group->addChild (fparam (id (tpCutoff), nm (tpCutoff), skewed (20.0f, 20000.0f, 1000.0f), 20000.0f, hzText));
    group->addChild (fparam (id (tpReso), nm (tpReso), Range (0.0f, 1.0f), 0.1f, pctText));
    group->addChild (fparam (id (tpFEnv), nm (tpFEnv), Range (-1.0f, 1.0f), 0.0f, pctText));
    group->addChild (fparam (id (tpFDec), nm (tpFDec), skewed (1.0f, 4000.0f, 200.0f), 200.0f, msText));
    group->addChild (fparam (id (tpCrush), nm (tpCrush), Range (1.0f, 16.0f, 0.01f), 16.0f,
                             [] (float v, int) { return v >= 15.95f ? juce::String ("off") : juce::String (v, 1) + " bit"; }));
    group->addChild (fparam (id (tpDown), nm (tpDown), skewed (1.0f, 50.0f, 6.0f), 1.0f,
                             [] (float v, int) { return v <= 1.01f ? juce::String ("off") : juce::String (v, 1) + "x"; }));
    group->addChild (fparam (id (tpDrive), nm (tpDrive), Range (0.0f, 40.0f), 0.0f,
                             [] (float v, int) { return juce::String (v, 1) + " dB"; }));
    group->addChild (std::make_unique<APC> (juce::ParameterID { id (tpSat), 1 }, nm (tpSat), satNames(), 0));
    group->addChild (fparam (id (tpSatMix), nm (tpSatMix), Range (0.0f, 1.0f), 1.0f, pctText));
    group->addChild (std::make_unique<APC> (juce::ParameterID { id (tpChoke), 1 }, nm (tpChoke), juce::StringArray { "None", "A", "B", "C", "D" }, (t == 2 || t == 3) ? 1 : 0));
    group->addChild (std::make_unique<APC> (juce::ParameterID { id (tpPoly), 1 }, nm (tpPoly), juce::StringArray { "Poly", "Mono" }, 0));
    group->addChild (std::make_unique<APC> (juce::ParameterID { id (tpOut), 1 }, nm (tpOut), outNames(), 0));
    group->addChild (fparam (id (tpRndSnd), nm (tpRndSnd), Range (0.0f, 1.0f), 0.0f, pctText));
    group->addChild (fparam (id (tpRndSeq), nm (tpRndSeq), Range (0.0f, 1.0f), 0.0f, pctText));
    group->addChild (fparam (id (tpVelSens), nm (tpVelSens), Range (0.0f, 1.0f), 0.75f, pctText));
    group->addChild (std::make_unique<API> (juce::ParameterID { id (tpNote), 1 }, nm (tpNote), 0, 127, 36 + t,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int v, int) { return juce::MidiMessage::getMidiNoteName (v, true, true, 4) + " (" + juce::String (v) + ")"; })
            .withValueFromStringFunction ([] (const juce::String& s) {
                return s.containsChar ('(') ? s.fromLastOccurrenceOf ("(", false, false).getIntValue() : s.getIntValue(); })));
    group->addChild (std::make_unique<APC> (juce::ParameterID { id (tpMTrig), 1 }, nm (tpMTrig), mTrigNames(), 0));
    layout.add (std::move (group));
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto g = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", " | ");
    g->addChild (fparam (gid::master, "Master", Range (-60.0f, 12.0f), 0.0f, dbText));
    g->addChild (fparam (gid::djFilter, "Master Filter", Range (-1.0f, 1.0f), 0.0f,
                         [] (float v, int) { return std::abs (v) < 0.02f ? juce::String ("open") : (v < 0 ? "LP " : "HP ") + juce::String (juce::roundToInt (std::abs (v) * 100)); }));
    g->addChild (fparam (gid::djReso, "Master Reso", Range (0.0f, 1.0f), 0.2f, pctText));
    g->addChild (fparam (gid::mDrive, "Master Drive", Range (0.0f, 40.0f), 0.0f,
                         [] (float v, int) { return juce::String (v, 1) + " dB"; }));
    g->addChild (std::make_unique<APC> (juce::ParameterID { gid::mSat, 1 }, "Master Sat Type", satNames(), 0));
    g->addChild (fparam (gid::mSatMix, "Master Sat Mix", Range (0.0f, 1.0f), 1.0f, pctText));
    g->addChild (std::make_unique<APB> (juce::ParameterID { gid::clip, 1 }, "Soft Clip", true));
    g->addChild (fparam (gid::swing, "Swing", Range (50.0f, 75.0f), 50.0f, [] (float v, int) { return juce::String (v, 1) + "%"; }));
    g->addChild (fparam (gid::gRndSnd, "Global Rnd Sound", Range (0.0f, 1.0f), 0.0f, pctText));
    g->addChild (fparam (gid::gRndSeq, "Global Rnd Seq", Range (0.0f, 1.0f), 0.0f, pctText));
    g->addChild (std::make_unique<APB> (juce::ParameterID { gid::lock, 1 }, "Lock Randomness", false));
    g->addChild (std::make_unique<API> (juce::ParameterID { gid::seed, 1 }, "Seed", 0, 9999, 1));
    g->addChild (std::make_unique<APC> (juce::ParameterID { gid::clock, 1 }, "Clock", juce::StringArray { "Host", "Internal" }, 0));
    g->addChild (std::make_unique<APB> (juce::ParameterID { gid::play, 1 }, "Play (internal)", false));
    g->addChild (fparam (gid::bpm, "Tempo (internal)", Range (30.0f, 300.0f, 0.1f), 120.0f,
                         [] (float v, int) { return juce::String (v, 1) + " bpm"; }));
    g->addChild (std::make_unique<API> (juce::ParameterID { gid::pattern, 1 }, "Pattern", 1, kNumPatterns, 1));
    g->addChild (std::make_unique<APC> (juce::ParameterID { gid::quant, 1 }, "Pattern Quantize", quantNames(), 2));
    g->addChild (std::make_unique<APB> (juce::ParameterID { gid::seqSound, 1 }, "Sequencer Plays Sounds", true));
    g->addChild (std::make_unique<APB> (juce::ParameterID { gid::midiOut, 1 }, "Sequencer MIDI Out", false));
    g->addChild (std::make_unique<APB> (juce::ParameterID { gid::patNotes, 1 }, "Pattern Select Notes", true));
    g->addChild (std::make_unique<APB> (juce::ParameterID { gid::fill, 1 }, "Fill", false));
    g->addChild (std::make_unique<APC> (juce::ParameterID { gid::looper, 1 }, "Looper", looperNames(), 0));
    layout.add (std::move (g));

    for (int t = 0; t < kNumTracks; ++t)
        addTrack (layout, t);

    auto m = std::make_unique<juce::AudioProcessorParameterGroup> ("maths", "Maths", " | ");
    m->addChild (std::make_unique<APB> (juce::ParameterID { gid::mOn, 1 }, "Maths On", false));
    m->addChild (std::make_unique<APB> (juce::ParameterID { gid::mQuant, 1 }, "Maths Quantize", true));
    for (int ch = 0; ch < 2; ++ch)
    {
        const juce::String n = ch == 0 ? "Maths A " : "Maths B ";
        const bool synced = true;
        m->addChild (fparam (mathsParamID (ch, mpRise), n + "Rise", Range (0.0f, 1.0f), ch == 0 ? 0.25f : 0.4f,
                             [synced] (float v, int) { return mathsTimeText (v, synced); }));
        m->addChild (fparam (mathsParamID (ch, mpFall), n + "Fall", Range (0.0f, 1.0f), ch == 0 ? 0.45f : 0.55f,
                             [synced] (float v, int) { return mathsTimeText (v, synced); }));
        m->addChild (fparam (mathsParamID (ch, mpCurve), n + "Curve", Range (-1.0f, 1.0f), 0.0f,
                             [] (float v, int) { return std::abs (v) < 0.02f ? juce::String ("lin") : (v < 0 ? "log " : "exp ") + juce::String (juce::roundToInt (std::abs (v) * 100)); }));
        m->addChild (std::make_unique<APB> (juce::ParameterID { mathsParamID (ch, mpCycle), 1 }, n + "Cycle", true));
        m->addChild (std::make_unique<APB> (juce::ParameterID { mathsParamID (ch, mpSync), 1 }, n + "Sync", true));
        m->addChild (std::make_unique<APC> (juce::ParameterID { mathsParamID (ch, mpTrig), 1 }, n + "Trig In", mTrigInNames(), 0));
        m->addChild (fparam (mathsParamID (ch, mpXMod), n + "Cross Mod", Range (-1.0f, 1.0f), 0.0f, pctText));
        m->addChild (fparam (mathsParamID (ch, mpLevel), n + "Level", Range (-1.0f, 1.0f), 1.0f, pctText));
    }
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const juce::String n = "Mod " + juce::String (s + 1) + " ";
        juce::StringArray trk { "All" };
        for (int t = 0; t < kNumTracks; ++t) trk.add ("T" + juce::String (t + 1));
        m->addChild (std::make_unique<APC> (juce::ParameterID { modSlotID (s, "src"), 1 }, n + "Source", modSrcNames(), s % msCount));
        m->addChild (std::make_unique<APC> (juce::ParameterID { modSlotID (s, "trk"), 1 }, n + "Track", trk, 0));
        m->addChild (std::make_unique<APC> (juce::ParameterID { modSlotID (s, "par"), 1 }, n + "Dest", modDstNames(), s % mdCount));
        m->addChild (fparam (modSlotID (s, "amt"), n + "Amount", Range (-1.0f, 1.0f), 0.0f, pctText));
    }
    layout.add (std::move (m));
    return layout;
}
} // namespace gm
