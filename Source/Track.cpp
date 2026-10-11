#include "Track.h"

namespace gm
{
void TrackEngine::bind (juce::AudioProcessorValueTreeState& s, int trackIndex)
{
    index = trackIndex;
    for (int i = 0; i < tpCount; ++i)
    {
        p[i] = s.getRawParameterValue (trackParamID (index, i));
        jassert (p[i] != nullptr);
    }
    rng.s = 0x9e3779b9U * (uint32_t) (index + 1);
}

void TrackEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& v : voices) v.prepare (sr);
    for (auto* s : { &gainL, &gainR, &driveGain, &mix }) s->setTime (0.02f, sr);
    dcR = 1.0f - 2.0f * kPi * 10.0f / (float) sr;
    firstBlock = true;
}

void TrackEngine::readParams (float gRndSnd, float gRndSeq) noexcept
{
    auto v = [this] (int tp) { return p[tp]->load (std::memory_order_relaxed); };

    vp.tune = v (tpTune) + v (tpFine) * 0.01f + mod.pitch;
    vp.stretchMode = v (tpWarp) > 0.5f;
    vp.stretch = v (tpStretch);
    vp.grainMs = v (tpGrain);
    vp.reverse = v (tpReverse) > 0.5f;
    vp.start = clampf (v (tpStart) + mod.start, 0.0f, 1.0f);
    vp.end = v (tpEnd);
    vp.attackMs = v (tpAttack);
    vp.holdMs = v (tpHold);
    const float dec = v (tpDecay);
    vp.decayInf = dec >= 9999.0f && mod.decayMul >= 0.999f;
    vp.decayMs = std::min (dec, 10000.0f) * mod.decayMul;
    vp.gateMode = v (tpMode) > 0.5f;
    vp.pAmt = v (tpPAmt);
    vp.pDecMs = v (tpPDec);
    vp.fType = (int) v (tpFType);
    vp.cutoff = v (tpCutoff) * std::exp2 (mod.cutoffOct);
    vp.reso = v (tpReso);
    vp.fEnv = v (tpFEnv);
    vp.fDecMs = v (tpFDec);
    vp.velSens = v (tpVelSens);

    bits = v (tpCrush);
    down = v (tpDown);
    driveDb = std::max (0.0f, v (tpDrive) + mod.driveDb);
    satType = (int) v (tpSat);
    satMix = v (tpSatMix);
    choke = (int) v (tpChoke);
    mono = v (tpPoly) > 0.5f;
    out = (int) v (tpOut);
    midiNote = (int) v (tpNote);
    mTrig = (int) v (tpMTrig);
    isMuted = v (tpMute) > 0.5f;
    isSolo = v (tpSolo) > 0.5f;
    rndSndAmt = unionAmt (v (tpRndSnd), gRndSnd);
    rndSeqAmt = unionAmt (v (tpRndSeq), gRndSeq);

    const float lvl = dbToGain (v (tpLevel) + mod.levelDb);
    const float pan = clampf (v (tpPan) + mod.pan, -1.0f, 1.0f);
    gainL.target = lvl * (pan > 0 ? 1.0f - pan : 1.0f);
    gainR.target = lvl * (pan < 0 ? 1.0f + pan : 1.0f);
    driveGain.target = std::pow (10.0f, driveDb * 0.05f);
    mix.target = satMix * clampf (driveDb / 3.0f, 0.0f, 1.0f); // fade the shaper in over the first 3 dB
    if (firstBlock)
    {
        for (auto* s : { &gainL, &gainR, &driveGain, &mix }) s->reset (s->target);
        firstBlock = false;
    }
}

bool TrackEngine::isSounding() const noexcept
{
    for (auto& v : voices)
        if (v.isActive()) return true;
    return false;
}

void TrackEngine::startVoice (SampleData* s, const Event& e) noexcept
{
    HitMods m;
    if (rndSndAmt > 0.0f)
    {
        const float r = rndSndAmt;
        const uint32_t k = e.key;
        m.pitch = hashBi (k ^ 0x1111u) * r * r * 12.0f;
        m.cutoffOct = hashBi (k ^ 0x2222u) * r * 2.0f;
        m.decayMul = std::exp2 (hashBi (k ^ 0x3333u) * r * 1.5f);
        m.gain = std::max (0.2f, 1.0f + hashBi (k ^ 0x4444u) * r * 0.35f);
        m.pan = hashBi (k ^ 0x5555u) * r * 0.7f;
        m.startAdd = hash01 (k ^ 0x6666u) * r * r * 0.25f;
        m.flipReverse = r > 0.6f && hash01 (k ^ 0x7777u) < (r - 0.6f) * 0.5f;
        m.stretchMul = std::exp2 (hashBi (k ^ 0x8888u) * r * 0.75f);
    }
    m.pitch += e.pitch;

    if (mono)
    {
        for (auto& v : voices)
            if (v.isActive()) v.kill (3.0f);
    }
    else
    {
        int live = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.isActive() && ! v.isDying())
            {
                ++live;
                if (oldest == nullptr || v.serial < oldest->serial) oldest = &v;
            }
        if (live >= kVoicesPerTrack && oldest != nullptr) oldest->kill (3.0f);
    }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.isActive()) { target = &v; break; }
    if (target == nullptr) // everything is busy fading: hard-steal the quietest
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.level() < target->level()) target = &v;
    }
    target->serial = ++serial;
    target->start (s, vp, m, e.vel, e.gate);
}

void TrackEngine::handle (const Event& e, SamplePool& pool) noexcept
{
    switch (e.type)
    {
        case Event::Choke:
            for (auto& v : voices) v.kill (6.0f);
            break;
        case Event::Release:
            for (auto& v : voices) v.release();
            break;
        case Event::Hit:
        {
            hitCounter.fetch_add (1, std::memory_order_relaxed);
            if (auto* s = pool.slot (index))
                startVoice (s, e);
            break;
        }
        default: break;
    }
}

bool TrackEngine::render (float* L, float* R, int n, SamplePool& pool) noexcept
{
    const bool live = events.size > 0 || isSounding();
    if (! live && ! busWarm)
    {
        for (auto* s : { &gainL, &gainR, &driveGain, &mix }) s->reset (s->target);
        peak *= 0.5f;
        return false;
    }
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);

    // insertion sort by (offset, type): chokes and releases land before hits at the same sample
    for (int i = 1; i < events.size; ++i)
    {
        auto e = events[i];
        int j = i - 1;
        while (j >= 0 && (events[j].offset > e.offset || (events[j].offset == e.offset && events[j].type > e.type)))
        {
            events[j + 1] = events[j];
            --j;
        }
        events[j + 1] = e;
    }

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= pos) return;
        for (auto& v : voices)
            if (v.isActive()) v.render (L + pos, R + pos, end - pos, vp);
        pos = end;
    };
    for (auto& e : events)
    {
        renderTo (std::clamp (e.offset, 0, n));
        handle (e, pool);
    }
    renderTo (n);
    events.clear();
    const bool crushOn = bits < 15.95f || down > 1.01f;
    const bool satOn = mix.target > 0.0f || mix.value > 1.0e-4f;
    if (! satOn) { dcxL = dcyL = dcxR = dcyR = 0.0f; satL.reset(); satR.reset(); }
    float pk = 0, compFor = -1.0f, comp = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        if (crushOn) crusher.process (l, r, bits, down);
        if (satOn)
        {
            const float g = driveGain.next(), m = mix.next();
            if (g != compFor) { compFor = g; comp = std::pow (g, -0.3f); } // pow only while drive moves
            const float yl = satL.process (l * g, satType), yr = satR.process (r * g, satType);
            const float ol = yl - dcxL + dcR * dcyL; dcxL = yl; dcyL = ol;
            const float orr = yr - dcxR + dcR * dcyR; dcxR = yr; dcyR = orr;
            l += m * (ol * comp - l);
            r += m * (orr * comp - r);
        }
        l *= gainL.next();
        r *= gainR.next();
        L[i] = l;
        R[i] = r;
        pk = std::max (pk, std::max (std::fabs (l), std::fabs (r)));
    }
    peak = std::max (pk, peak * 0.7f);
    // keep the bus running until the DC blocker's tail (Tube/Rectify make lots of DC) has died away
    busWarm = live || (satOn && std::abs (dcyL) + std::abs (dcyR) > 1.0e-5f) || std::abs (mix.value - mix.target) > 1.0e-4f;
    return true;
}
} // namespace gm
