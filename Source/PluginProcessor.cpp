#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DrumSynth.h"

using namespace gm;

juce::AudioProcessor::BusesProperties GeomancerProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int i = 0; i < kNumAux; ++i)
        b = b.withOutput ("Out " + juce::String (i + 1), juce::AudioChannelSet::stereo(), true);
    return b;
}

GeomancerProcessor::GeomancerProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int t = 0; t < kNumTracks; ++t)
        tracks[(size_t) t].bind (apvts, t);

    auto raw = [this] (const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };
    g.master = raw (gid::master); g.djFilter = raw (gid::djFilter); g.djReso = raw (gid::djReso);
    g.mDrive = raw (gid::mDrive); g.mSat = raw (gid::mSat); g.mSatMix = raw (gid::mSatMix); g.clip = raw (gid::clip);
    g.swing = raw (gid::swing); g.gRndSnd = raw (gid::gRndSnd); g.gRndSeq = raw (gid::gRndSeq);
    g.lock = raw (gid::lock); g.seed = raw (gid::seed); g.clock = raw (gid::clock); g.play = raw (gid::play);
    g.bpm = raw (gid::bpm); g.pattern = raw (gid::pattern); g.quant = raw (gid::quant);
    g.seqSound = raw (gid::seqSound); g.midiOut = raw (gid::midiOut); g.patNotes = raw (gid::patNotes);
    g.fill = raw (gid::fill); g.mOn = raw (gid::mOn); g.mQuant = raw (gid::mQuant); g.looper = raw (gid::looper);
    for (int c = 0; c < 2; ++c)
        for (int m = 0; m < mpCount; ++m)
            g.maths[c][m] = raw (mathsParamID (c, m));
    static const char* what[4] = { "src", "trk", "par", "amt" };
    for (int s = 0; s < kNumModSlots; ++s)
        for (int w = 0; w < 4; ++w)
            g.mod[s][w] = raw (modSlotID (s, what[w]));

    for (auto& a : audition) a.store (0);
    for (auto& p : uiPlayPos) p.store (-1);
    for (auto& m : uiMeter) m.store (0);
    for (auto& m : mathsPending) m = -1;
    loadDefaultKit();
    uiRng.s = (uint32_t) juce::Time::currentTimeMillis() | 1u;
    rng.s = uiRng.next() | 1u;
    startTimerHz (20);
}

GeomancerProcessor::~GeomancerProcessor()
{
    stopTimer();
}

bool GeomancerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& s = layouts.outputBuses.getReference (i);
        if (! s.isDisabled() && s != juce::AudioChannelSet::stereo()) return false;
    }
    for (const auto& in : layouts.inputBuses)
        if (! in.isDisabled()) return false;
    return true;
}

void GeomancerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    scratch.setSize (2, std::max (samplesPerBlock, 512));
    for (auto& t : tracks) t.prepare (sr);
    for (auto& v : previewVoices) v.prepare (sr);
    midiOut.ensureSize (8192);
    for (int i = 0; i < kNumAux; ++i)
    {
        auto* bus = getBus (false, i + 1);
        auxChannel[i] = (bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() == 2)
                            ? getChannelIndexInProcessBlockBuffer (false, i + 1, 0) : -1;
    }
    djSmooth.setTime (0.03f, sr);
    for (auto* s : { &masterGain, &mDriveGain, &mMix }) s->setTime (0.02f, sr);
    djSmooth.reset (g.djFilter->load());
    masterGain.reset (dbToGain (g.master->load()));
    mDriveGain.reset (std::pow (10.0f, g.mDrive->load() * 0.05f));
    mMix.reset (g.mSatMix->load() * clampf (g.mDrive->load() / 3.0f, 0.0f, 1.0f));
    mdcR = 1.0f - 2.0f * kPi * 10.0f / (float) sr;
    djL.reset(); djR.reset();
    pendingOffs.clear();
    maths.reset();
}

//==============================================================================
Transport GeomancerProcessor::computeTransport (int numSamples)
{
    const bool standalone = wrapperType == wrapperType_Standalone;
    const int clockMode = standalone ? 1 : (int) g.clock->load();
    bool hostPlaying = false, hasHostSamples = false;
    juce::int64 hostSamples = 0;
    double hostPpq = 0, hostBpm = g.bpm->load(), barStart = 0, barLen = 4;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) hostBpm = *b;
            if (auto ts = pos->getTimeSignature())
                if (ts->numerator > 0 && ts->denominator > 0) barLen = ts->numerator * 4.0 / ts->denominator;
            if (auto p = pos->getPpqPosition()) { hostPpq = *p; hostPlaying = pos->getIsPlaying(); }
            if (auto ts = pos->getTimeInSamples()) { hostSamples = *ts; hasHostSamples = true; }
            if (auto bs = pos->getPpqPositionOfLastBarStart()) barStart = *bs;
            else barStart = std::floor (hostPpq / barLen) * barLen;
        }

    Transport tr;
    tr.barLen = barLen;
    if (clockMode == 0 && hostPlaying)
    {
        tr.playing = true;
        tr.ppq = hostPpq;
        tr.bpm = hostBpm;
        tr.barStart = barStart;
        tr.hasSampleTime = hasHostSamples;
        tr.timeInSamples = hostSamples;
        if (internalRunning) disarmPlay = true; // host took over: drop the "audition while stopped" latch
        internalRunning = false;
        return tr;
    }
    tr.bpm = clockMode == 0 ? hostBpm : (double) g.bpm->load();
    if (g.play->load() > 0.5f)
    {
        if (! internalRunning) { internalPpq = 0; internalSamples = 0; internalRunning = true; }
        tr.playing = true;
        tr.ppq = internalPpq;
        tr.hasSampleTime = true;
        tr.timeInSamples = internalSamples;
        internalSamples += numSamples;
        tr.barStart = std::floor (internalPpq / barLen) * barLen;
        internalPpq += numSamples * tr.bpm / (60.0 * sr);
    }
    else
        internalRunning = false;
    return tr;
}

bool GeomancerProcessor::silenced (int t) const noexcept
{
    const auto& tr = tracks[(size_t) t];
    return tr.muted() || (anySolo && ! tr.soloed());
}

void GeomancerProcessor::addHit (int t, const Event& e) noexcept
{
    if (hitListener != nullptr) hitListener->geomancerHit (t, e.offset, e.vel, e.pitch);
    tracks[(size_t) t].addEvent (e);
    trackHitOffsets[t].push (e.offset);
    if (const int grp = tracks[(size_t) t].chokeGroup(); grp > 0)
        for (int u = 0; u < kNumTracks; ++u)
            if (u != t && tracks[(size_t) u].chokeGroup() == grp)
                tracks[(size_t) u].addEvent ({ e.offset, Event::Choke });
}

void GeomancerProcessor::midiNoteOn (int note, float vel, int offset, double gateSamples) noexcept
{
    for (int i = 0; i < pendingOffs.size; ++i)
        if (pendingOffs[i].note == note)
        {
            midiOut.addEvent (juce::MidiMessage::noteOff (1, note), std::min (offset, (int) pendingOffs[i].left));
            pendingOffs[i] = pendingOffs[pendingOffs.size - 1];
            --pendingOffs.size;
            break;
        }
    const auto v = (juce::uint8) std::clamp (juce::roundToInt (vel * 127.0f), 1, 127);
    midiOut.addEvent (juce::MidiMessage::noteOn (1, note, v), offset);
    pendingOffs.push ({ note, offset + std::max (1.0, gateSamples) });
}

void GeomancerProcessor::flushNoteOffs (int n, bool all) noexcept
{
    for (int i = 0; i < pendingOffs.size;)
    {
        if (all || pendingOffs[i].left < n)
        {
            midiOut.addEvent (juce::MidiMessage::noteOff (1, pendingOffs[i].note),
                              all ? 0 : std::clamp ((int) pendingOffs[i].left, 0, n - 1));
            pendingOffs[i] = pendingOffs[pendingOffs.size - 1];
            --pendingOffs.size;
        }
        else
        {
            pendingOffs[i].left -= n;
            ++i;
        }
    }
}

//==============================================================================
void GeomancerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    if (n <= 0) return;
    if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true); // only if the host lies about block size

    // ---- parameters
    const float gRndSnd = g.gRndSnd->load(), gRndSeq = g.gRndSeq->load();
    anySolo = false;
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& tr = tracks[(size_t) t];
        tr.mod = mathsMod[t];
        tr.readParams (gRndSnd, gRndSeq);
        anySolo = anySolo || tr.soloed();
        trackHitOffsets[t].clear();
    }
    const int quant = (int) g.quant->load();
    const bool seqSound = g.seqSound->load() > 0.5f, wantMidiOut = g.midiOut->load() > 0.5f;
    const bool patNotes = g.patNotes->load() > 0.5f;

    // ---- transport
    const Transport tr = computeTransport (n);
    const double pps = tr.bpm / (60.0 * sr);
    uiBpm.store (tr.bpm, std::memory_order_relaxed);
    midiOut.clear();

    // ---- MIDI in: drum notes, pattern select (notes 0-15 / program change), fill (note 16)
    for (const auto meta : midi)
    {
        if (meta.numBytes > 3) continue; // SysEx: getMessage() would allocate
        const auto msg = meta.getMessage();
        const int off = std::clamp (meta.samplePosition, 0, n - 1);
        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            if (patNotes && note < kNumPatterns) { chainLen = 0; seq.requestPattern (note, tr.ppq + off * pps, tr, quant); continue; }
            if (patNotes && note == kNumPatterns) { fillHeld = true; continue; }
            for (int t = 0; t < kNumTracks; ++t)
                if (tracks[(size_t) t].note() == note && ! silenced (t))
                    addHit (t, { off, Event::Hit, msg.getFloatVelocity(), 0.0f, -1.0, rng.next() });
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            if (patNotes && note == kNumPatterns) { fillHeld = false; continue; }
            for (int t = 0; t < kNumTracks; ++t)
                if (tracks[(size_t) t].note() == note)
                    tracks[(size_t) t].addEvent ({ off, Event::Release });
        }
        else if (msg.isProgramChange())
        {
            chainLen = 0;
            seq.requestPattern (msg.getProgramChangeNumber() % kNumPatterns, tr.ppq + off * pps, tr, quant);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (auto& t : tracks) t.addEvent ({ off, Event::Release });
    }

    // ---- pattern requests from the editor and from automation of the Pattern parameter
    if (const int req = guiPatternRequest.exchange (-1); req >= 0)
        seq.requestPattern (req, tr.ppq, tr, quant);
    const int pp = (int) g.pattern->load() - 1;
    if (resyncPatternParam.exchange (false)) lastPatternParam = pp;
    if (pp != lastPatternParam)
    {
        if (lastPatternParam >= 0) { chainLen = 0; seq.requestPattern (pp, tr.ppq, tr, quant); }
        lastPatternParam = pp;
    }

    // ---- audition clicks from the editor
    for (int t = 0; t < kNumTracks; ++t)
        if (const int v = audition[(size_t) t].exchange (0); v > 0)
            addHit (t, { 0, Event::Hit, (float) v / 127.0f, 0.0f, 0.25 * sr, rng.next() });

    // ---- sequencer
    SeqConfig cfg;
    cfg.quant = quant;
    cfg.swing = g.swing->load();
    cfg.fill = fillHeld || g.fill->load() > 0.5f;
    cfg.seed = (uint32_t) g.seed->load();
    cfg.lock = g.lock->load() > 0.5f;
    cfg.loopLen = looperPpq ((int) g.looper->load(), tr.barLen);
    cfg.chainLen = std::clamp (chainLen.load(), 0, kNumPatterns);
    for (int i = 0; i < cfg.chainLen; ++i) cfg.chain[i] = std::clamp (chain[(size_t) i].load(), 0, kNumPatterns - 1);
    for (int t = 0; t < kNumTracks; ++t)
    {
        cfg.rndSeq[t] = tracks[(size_t) t].rndSeq();
        cfg.density[t] = mathsMod[t].density;
    }
    seqHits.clear();
    seq.process (tr, n, sr, bank, cfg, seqHits);
    if (wasRunning && ! seq.running()) flushNoteOffs (n, true);
    wasRunning = seq.running();

    for (auto& h : seqHits)
    {
        if (silenced (h.track)) continue;
        if (seqSound) addHit (h.track, { h.offset, Event::Hit, h.vel, h.pitch, h.gate, h.key });
        else if (hitListener != nullptr) hitListener->geomancerHit (h.track, h.offset, h.vel, h.pitch);
        if (wantMidiOut) noteReqs.push ({ h.offset, tracks[(size_t) h.track].note(), h.vel, h.gate });
    }

    // ---- MATHS
    if (seq.jumpedThisBlock())
    {
        maths.reset();
        for (auto& m : mathsPending) m = -1;
    }
    runMaths (tr, n, wantMidiOut);
    // note-ons in time order, so each one's note-off bookkeeping sees its true predecessor
    for (int i = 1; i < noteReqs.size; ++i)
    {
        const auto r = noteReqs[i];
        int j = i - 1;
        while (j >= 0 && noteReqs[j].offset > r.offset) { noteReqs[j + 1] = noteReqs[j]; --j; }
        noteReqs[j + 1] = r;
    }
    for (auto& r : noteReqs) midiNoteOn (r.note, r.vel, r.offset, r.gate);
    noteReqs.clear();
    flushNoteOffs (n, ! wantMidiOut); // turning MIDI out off releases anything still hanging

    // ---- render tracks into their buses
    auto* mainL = buffer.getWritePointer (0);
    auto* mainR = buffer.getWritePointer (1);
    auto* sL = scratch.getWritePointer (0);
    auto* sR = scratch.getWritePointer (1);
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& tr2 = tracks[(size_t) t];
        if (tr2.render (sL, sR, n, samples))
        {
            float* dL = mainL;
            float* dR = mainR;
            const int out = tr2.output();
            if (out > 0 && auxChannel[out - 1] >= 0 && auxChannel[out - 1] + 1 < buffer.getNumChannels())
            {
                dL = buffer.getWritePointer (auxChannel[out - 1]);
                dR = buffer.getWritePointer (auxChannel[out - 1] + 1);
            }
            juce::FloatVectorOperations::add (dL, sL, n);
            juce::FloatVectorOperations::add (dR, sR, n);
        }
        uiMeter[(size_t) t].store (tr2.meter(), std::memory_order_relaxed);
    }
    processMaster (mainL, mainR, n);

    // ---- browser audition
    if (previewStop.exchange (false))
        for (auto& v : previewVoices) v.kill (8.0f);
    if (const auto pt = previewTrigger.load(); pt != previewSeen)
    {
        previewSeen = pt;
        if (auto* s = samples.slot (kPreviewSlot))
        {
            previewVoices[previewCurrent].kill (4.0f);
            previewCurrent ^= 1;
            previewVoices[previewCurrent].start (s, VoiceParams{}, HitMods{}, 1.0f, -1.0);
        }
    }
    for (auto& v : previewVoices)
        if (v.isActive()) v.render (mainL, mainR, n, VoiceParams{});

    // ---- MIDI out (sequencer notes only; incoming MIDI is consumed)
    midi.clear();
    midi.addEvents (midiOut, 0, n, 0); // copy, don't swap: keep our pre-sized buffer on our side

    // ---- editor feedback
    uiCurrentPattern.store (seq.current(), std::memory_order_relaxed);
    uiQueuedPattern.store (seq.queued(), std::memory_order_relaxed);
    uiRunning.store (seq.running(), std::memory_order_relaxed);
    uiLooping.store (seq.looping(), std::memory_order_relaxed);
    for (int t = 0; t < kNumTracks; ++t)
        uiPlayPos[(size_t) t].store (seq.position (t), std::memory_order_relaxed);
}

void GeomancerProcessor::runMaths (const Transport& tr, int n, bool wantMidiOut) noexcept
{
    const bool on = g.mOn->load() > 0.5f;
    for (auto& m : mathsMod) m = {};
    if (! on || ! seq.running()) return;

    MathsChannelParams ch[2];
    for (int c = 0; c < 2; ++c)
    {
        auto v = [&] (int mp) { return g.maths[c][mp]->load(); };
        ch[c] = { v (mpRise), v (mpFall), v (mpCurve), v (mpCycle) > 0.5f, v (mpSync) > 0.5f,
                  (int) v (mpTrig), v (mpXMod), v (mpLevel) };
    }
    mathsEvents.clear();
    maths.process (n, sr, tr.ppq, tr.bpm, tr.barLen, ch, trackHitOffsets, mathsEvents);

    const double pps = tr.bpm / (60.0 * sr);
    const bool quantise = g.mQuant->load() > 0.5f;
    const bool seqSound = g.seqSound->load() > 0.5f;
    auto fire = [&] (int t, int off)
    {
        if (silenced (t)) return;
        const double gate = 0.125 / pps; // half a 1/16 step
        const uint32_t key = rng.next();
        if (seqSound) addHit (t, { off, Event::Hit, 0.85f, 0.0f, gate, key });
        else if (hitListener != nullptr) hitListener->geomancerHit (t, off, 0.85f, 0.0f);
        if (wantMidiOut) noteReqs.push ({ off, tracks[(size_t) t].note(), 0.85f, gate });
    };

    for (auto& ev : mathsEvents)
        for (int t = 0; t < kNumTracks; ++t)
        {
            const int mt = tracks[(size_t) t].mathsTrig();
            const bool isEoc = ev.type == mtAEoc || ev.type == mtBEoc;
            if (mt == mtOff || ! (mt == ev.type || (mt == mtAnyEoc && isEoc))) continue;
            if (quantise)
            {
                const double at = tr.ppq + ev.offset * pps;
                const double grid = std::ceil (at / 0.25 - 1.0e-9) * 0.25;
                if (mathsPending[t] < 0) mathsPending[t] = grid;
            }
            else
                fire (t, ev.offset);
        }
    for (int t = 0; t < kNumTracks; ++t)
        if (mathsPending[t] >= 0)
        {
            const auto off = (int) std::floor ((mathsPending[t] - tr.ppq) / pps + 0.5);
            if (off >= n) continue;
            fire (t, std::max (0, off));
            mathsPending[t] = -1;
        }

    // modulation for the next block
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const float amt = g.mod[s][3]->load();
        if (std::abs (amt) < 1.0e-4f) continue;
        const float v = maths.value ((int) g.mod[s][0]->load()) * amt;
        const int trk = (int) g.mod[s][1]->load();
        const int par = (int) g.mod[s][2]->load();
        for (int t = trk == 0 ? 0 : trk - 1; t < (trk == 0 ? kNumTracks : trk); ++t)
        {
            auto& m = mathsMod[t];
            switch (par)
            {
                case mdPitch:   m.pitch += v * 24.0f; break;
                case mdCutoff:  m.cutoffOct += v * 5.0f; break;
                case mdDecay:   m.decayMul *= std::exp2 (v * 3.0f); break;
                case mdLevel:   m.levelDb += v * 24.0f; break;
                case mdPan:     m.pan += v; break;
                case mdStart:   m.start += clampf (v, 0.0f, 1.0f) * 0.5f; break;
                case mdDrive:   m.driveDb += std::max (0.0f, v) * 30.0f; break;
                case mdDensity: m.density = clampf (m.density + v, -1.0f, 1.0f); break;
                default: break;
            }
        }
    }
}

void GeomancerProcessor::processMaster (float* L, float* R, int n) noexcept
{
    djSmooth.target = g.djFilter->load();
    masterGain.target = dbToGain (g.master->load());
    const float driveDb = g.mDrive->load();
    mDriveGain.target = std::pow (10.0f, driveDb * 0.05f);
    // the wet amount fades in over the first 3 dB of drive, so the shaper never switches in with a click
    mMix.target = g.mSatMix->load() * clampf (driveDb / 3.0f, 0.0f, 1.0f);
    const int satType = (int) g.mSat->load();
    const float reso = g.djReso->load() * 0.9f;
    const bool clip = g.clip->load() > 0.5f;
    const bool satOn = mMix.target > 0.0f || mMix.value > 1.0e-4f;
    if (! satOn) { mdcxL = mdcyL = mdcxR = mdcyR = 0.0f; mSatL.reset(); mSatR.reset(); }

    float pk = 0, compFor = -1.0f, comp = 1.0f;
    for (int s0 = 0; s0 < n; s0 += 32)
    {
        const int s1 = std::min (n, s0 + 32);
        // DJ filter: left of centre = low-pass sweeping down, right = high-pass sweeping up.
        // While "open" it keeps running as a 20 kHz low-pass so engaging the LP side is seamless.
        float v = djSmooth.value;
        for (int i = s0; i < s1; ++i) v = djSmooth.next();
        int mode = SVF::Off;
        if (v < -0.02f) { mode = SVF::LP; djL.set (20000.0f * std::exp2 (v * 10.0f), reso, sr); }
        else if (v > 0.02f)
        {
            if (djMode != SVF::HP) { djL.reset(); djR.reset(); } // a 20 Hz high-pass starts settled from zero
            mode = SVF::HP;
            djL.set (20.0f * std::exp2 (v * 10.0f), reso, sr);
        }
        else djL.set (20000.0f, 0.0f, sr);
        djMode = mode;
        djR.g = djL.g; djR.k = djL.k; djR.a1 = djL.a1; djR.a2 = djL.a2; djR.a3 = djL.a3;

        for (int i = s0; i < s1; ++i)
        {
            float l = L[i], r = R[i];
            if (mode != SVF::Off) { l = djL.process (l, mode); r = djR.process (r, mode); }
            else { djL.process (l, SVF::LP); djR.process (r, SVF::LP); }
            if (satOn)
            {
                const float gd = mDriveGain.next(), m = mMix.next();
                if (gd != compFor) { compFor = gd; comp = std::pow (gd, -0.3f); } // pow only while drive moves
                const float yl = mSatL.process (l * gd, satType), yr = mSatR.process (r * gd, satType);
                const float ol = yl - mdcxL + mdcR * mdcyL; mdcxL = yl; mdcyL = ol;
                const float orr = yr - mdcxR + mdcR * mdcyR; mdcxR = yr; mdcyR = orr;
                l += m * (ol * comp - l);
                r += m * (orr * comp - r);
            }
            const float mg = masterGain.next();
            l *= mg;
            r *= mg;
            if (clip) // transparent below -2 dBFS, then a soft knee that never exceeds 0 dBFS
            {
                auto sc = [] (float x)
                {
                    const float a = std::fabs (x);
                    return a <= 0.8f ? x : std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
                };
                l = sc (l);
                r = sc (r);
            }
            L[i] = l;
            R[i] = r;
            pk = std::max (pk, std::max (std::fabs (l), std::fabs (r)));
        }
    }
    uiMasterMeter.store (std::max (pk, uiMasterMeter.load (std::memory_order_relaxed) * 0.8f), std::memory_order_relaxed);
}

//==============================================================================
void GeomancerProcessor::timerCallback()
{
    if (disarmPlay.exchange (false))
        if (auto* p = param (gid::play))
            p->setValueNotifyingHost (0.0f);
}

bool GeomancerProcessor::loadSample (int track, const juce::File& f, juce::String& error)
{
    if (! samples.loadIntoSlot (track, f, error)) return false;
    setMissing (track, {});
    setSampleDir (f.getParentDirectory().getFullPathName());
    markDirty();
    return true;
}

bool GeomancerProcessor::preview (const juce::File& f, juce::String& error)
{
    auto s = samples.load (f, error, 20.0);
    if (s == nullptr) return false;
    samples.setSlot (kPreviewSlot, s);
    previewTrigger.fetch_add (1);
    return true;
}

void GeomancerProcessor::loadDefaultKit()
{
    const auto& names = drumsynth::names();
    for (int t = 0; t < kNumTracks; ++t)
    {
        juce::String err;
        if (t < names.size()) samples.loadSourceIntoSlot (t, "synth:" + names[t], err);
        else samples.clearSlot (t);
        setMissing (t, {});
    }
}

void GeomancerProcessor::markDirty()
{
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void GeomancerProcessor::pushUndo (int pattern)
{
    UndoEntry e { pattern, std::make_unique<PatternSnapshot>() };
    e.snap->take (bank.patterns[pattern]);
    undoStack.push_back (std::move (e));
    if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool GeomancerProcessor::undo()
{
    if (undoStack.empty()) return false;
    auto e = std::move (undoStack.back());
    undoStack.pop_back();
    UndoEntry r { e.pattern, std::make_unique<PatternSnapshot>() };
    r.snap->take (bank.patterns[e.pattern]);
    redoStack.push_back (std::move (r));
    e.snap->restore (bank.patterns[e.pattern]);
    viewPattern = e.pattern;
    markDirty();
    return true;
}

bool GeomancerProcessor::redo()
{
    if (redoStack.empty()) return false;
    auto e = std::move (redoStack.back());
    redoStack.pop_back();
    UndoEntry u { e.pattern, std::make_unique<PatternSnapshot>() };
    u.snap->take (bank.patterns[e.pattern]);
    undoStack.push_back (std::move (u));
    e.snap->restore (bank.patterns[e.pattern]);
    viewPattern = e.pattern;
    markDirty();
    return true;
}

//==============================================================================
void GeomancerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("GEOMANCER");
    root.setAttribute ("version", 1);
    if (auto xml = apvts.copyState().createXml())
        root.addChildElement (xml.release());
    auto* smp = root.createNewChildElement ("SAMPLES");
    for (int t = 0; t < kNumTracks; ++t)
    {
        juce::String path;
        if (auto* s = samples.slot (t)) path = s->source;
        else path = missing (t); // keep the reference to a missing file alive
        if (path.isNotEmpty())
        {
            auto* e = smp->createNewChildElement ("S");
            e->setAttribute ("t", t);
            e->setAttribute ("path", path);
        }
    }
    root.setAttribute ("patterns", bank.serialise().toBase64Encoding());
    root.setAttribute ("viewPattern", viewPattern);
    root.setAttribute ("selTrack", selectedTrack);
    root.setAttribute ("page", page);
    root.setAttribute ("lane", laneKind);
    root.setAttribute ("tab", bottomTab);
    root.setAttribute ("sampleDir", sampleDir());
    root.setAttribute ("chain", chainText());
    copyXmlToBinary (root, destData);
}

void GeomancerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("GEOMANCER")) return;

    if (auto* p = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*p));

    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (xml->getStringAttribute ("patterns")))
        bank.deserialise (mb);

    for (int t = 0; t < kNumTracks; ++t)
    {
        samples.clearSlot (t);
        setMissing (t, {});
    }
    if (auto* smp = xml->getChildByName ("SAMPLES"))
        for (auto* e : smp->getChildIterator())
        {
            const int t = e->getIntAttribute ("t", -1);
            const auto src = e->getStringAttribute ("path");
            if (t < 0 || t >= kNumTracks) continue;
            juce::String err;
            const bool ok = SamplePool::isSynth (src) ? samples.loadSourceIntoSlot (t, src, err)
                                                      : (juce::File (src).existsAsFile() && samples.loadIntoSlot (t, juce::File (src), err));
            if (! ok) setMissing (t, src);
        }

    viewPattern = std::clamp (xml->getIntAttribute ("viewPattern", 0), 0, kNumPatterns - 1);
    selectedTrack = std::clamp (xml->getIntAttribute ("selTrack", 0), 0, kNumTracks - 1);
    page = std::clamp (xml->getIntAttribute ("page", 0), 0, gm::kMaxSteps / 32 - 1);
    laneKind = std::clamp (xml->getIntAttribute ("lane", 0), 0, 6);
    bottomTab = std::clamp (xml->getIntAttribute ("tab", 0), 0, 3);
    setSampleDir (xml->getStringAttribute ("sampleDir"));
    juce::StringArray links;
    links.addTokens (xml->getStringAttribute ("chain"), " ", "");
    links.removeEmptyStrings();
    int n = 0;
    for (auto& l : links)
        if (n < kNumPatterns) chain[(size_t) n++] = std::clamp (l.getIntValue() - 1, 0, kNumPatterns - 1);
    chainLen = n;
    guiPatternRequest = viewPattern;
    resyncPatternParam = true; // don't let a restored Pattern parameter fight the restored view
    undoStack.clear();         // snapshots from before the load don't belong to this bank
    redoStack.clear();
}

juce::String GeomancerProcessor::chainText() const
{
    juce::StringArray s;
    for (int i = 0; i < chainLen.load(); ++i) s.add (juce::String (chain[(size_t) i].load() + 1));
    return s.joinIntoString (" ");
}

juce::AudioProcessorEditor* GeomancerProcessor::createEditor()
{
    return new GeomancerEditor (*this);
}

#ifndef GEOMANCER_EMBEDDED // Aeromancer compiles Geomancer in as a module and has its own entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GeomancerProcessor();
}
#endif
