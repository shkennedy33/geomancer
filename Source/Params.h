#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP.h"

namespace gm
{
// Per-track parameters. IDs are "t<N>_<suffix>" (N = 1..16) and must stay stable forever,
// because REAPER automation envelopes and saved projects refer to them.
enum TP
{
    tpLevel, tpPan, tpMute, tpSolo,
    tpTune, tpFine, tpWarp, tpStretch, tpGrain, tpReverse, tpStart, tpEnd,
    tpAttack, tpHold, tpDecay, tpMode, tpPAmt, tpPDec,
    tpFType, tpCutoff, tpReso, tpFEnv, tpFDec,
    tpCrush, tpDown, tpDrive, tpSat, tpSatMix,
    tpChoke, tpPoly, tpOut, tpRndSnd, tpRndSeq, tpVelSens, tpNote, tpMTrig,
    tpCount
};

struct TrackParamInfo { const char* suffix; const char* name; };
const TrackParamInfo& trackParamInfo (int tp);
juce::String trackParamID (int track, int tp);

// MATHS-style function generators: two channels (A, B) plus four modulation slots.
enum MP { mpRise, mpFall, mpCurve, mpCycle, mpSync, mpTrig, mpXMod, mpLevel, mpCount };
juce::String mathsParamID (int ch, int mp);           // ch 0 = A, 1 = B
juce::String modSlotID (int slot, const char* what);  // what: src, trk, par, amt
constexpr int kNumModSlots = 4;

enum ModSrc { msA, msB, msOr, msSum, msDiff, msCount };
enum ModDst { mdPitch, mdCutoff, mdDecay, mdLevel, mdPan, mdStart, mdDrive, mdDensity, mdCount };
enum MTrig { mtOff, mtAEor, mtAEoc, mtBEor, mtBEoc, mtAnyEoc, mtCount };
enum MTrigIn { miOff, mi16, mi8, mi4, miBar, miOtherEoc, miTrack1 }; // + 16 tracks

juce::StringArray modSrcNames();
juce::StringArray modDstNames();
juce::StringArray mTrigNames();
juce::StringArray mTrigInNames();

// Maths time mapping shared by DSP + GUI.
double mathsTimeSeconds (float norm01);          // free: 2 ms .. 20 s
double mathsTimeSteps (float norm01);            // synced: 1/4 step .. 64 steps (1/16 steps)
juce::String mathsTimeText (float norm01, bool synced);

namespace gid
{
    inline const juce::String master = "master", djFilter = "djFilter", djReso = "djReso",
        mDrive = "mDrive", mSat = "mSat", mSatMix = "mSatMix", clip = "clip",
        swing = "swing", gRndSnd = "gRndSnd", gRndSeq = "gRndSeq", lock = "lock", seed = "seed",
        clock = "clock", play = "play", bpm = "bpm", pattern = "pattern", quant = "quant",
        seqSound = "seqSound", midiOut = "midiOut", patNotes = "patNotes", fill = "fill",
        mOn = "mOn", mQuant = "mQuant", looper = "looper";
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

juce::StringArray satNames();
juce::StringArray filterNames();
juce::StringArray quantNames();
juce::StringArray looperNames();
double looperPpq (int index, double barLen); // 0 = off
juce::StringArray outNames();
} // namespace gm
