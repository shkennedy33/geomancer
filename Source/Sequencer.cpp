#include "Sequencer.h"

namespace gm
{
int Sequencer::stepIndex (int64_t n, int len, int dir, uint32_t key) noexcept
{
    if (len <= 1) return 0;
    switch (dir)
    {
        case DirRev: return len - 1 - (int) (n % len);
        case DirPingPong:
        {
            const int64_t period = 2 * (int64_t) len - 2;
            const int m = (int) (n % period);
            return m < len ? m : (int) (period - m);
        }
        case DirRandom: return (int) (hash32 (key) % (uint32_t) len);
        default: return (int) (n % len);
    }
}

static bool condOk (int c, int64_t cycle, bool fill) noexcept
{
    switch (c)
    {
        case Cond1of2: return cycle % 2 == 0;
        case Cond2of2: return cycle % 2 == 1;
        case Cond1of3: return cycle % 3 == 0;
        case Cond2of3: return cycle % 3 == 1;
        case Cond3of3: return cycle % 3 == 2;
        case Cond1of4: return cycle % 4 == 0;
        case Cond2of4: return cycle % 4 == 1;
        case Cond3of4: return cycle % 4 == 2;
        case Cond4of4: return cycle % 4 == 3;
        case CondFill: return fill;
        case CondNotFill: return ! fill;
        case CondFirst: return cycle == 0;
        case CondNotFirst: return cycle != 0;
        default: return true;
    }
}

void Sequencer::requestPattern (int p, double atPpq, const Transport& tr, int quant) noexcept
{
    p = std::clamp (p, 0, kNumPatterns - 1);
    chainAdvance = false;
    if (! wasPlaying) { currentPattern = p; queuedPattern = -1; return; }
    if (p == currentPattern) { queuedPattern = -1; return; }
    queuedPattern = p;
    resetOnSwitch = quant != 0;
    if (quant == 0) { switchPpq = atPpq; return; }
    const double q = quant == 1 ? 1.0 : tr.barLen * (quant == 2 ? 1.0 : quant == 3 ? 2.0 : 4.0);
    const double origin = quant == 2 ? tr.barStart : 0.0;
    switchPpq = origin + std::ceil ((atPpq - origin) / q - 1.0e-6) * q;
}

int64_t Sequencer::cycleAt (const Pattern& pat, int t, double ppq) const noexcept
{
    const auto lane = pat.lane (t);
    const auto n = (int64_t) std::floor ((ppq - patStart) / divPpq (lane.div));
    const int64_t period = lane.dir == DirPingPong ? std::max (1, 2 * lane.length - 2) : lane.length;
    return n < 0 ? 0 : n / period;
}

void Sequencer::process (const Transport& tr, int numSamples, double sr, const PatternBank& bank,
                         const SeqConfig& cfg, FixedVec<SeqHit, 2048>& out) noexcept
{
    jumped = false;
    if (! tr.playing)
    {
        wasPlaying = false;
        if (queuedPattern >= 0) { currentPattern = queuedPattern; queuedPattern = -1; }
        for (auto& p : playPos) p = -1;
        return;
    }

    const double pps = tr.bpm / (60.0 * sr);
    const double p0 = tr.ppq, p1 = p0 + numSamples * pps;

    // Continuity is judged on the host's sample clock: a tempo change moves ppq away from the
    // previous block's prediction without being a seek. Without a sample clock, allow half a block.
    const bool started = ! wasPlaying;
    bool continuous = wasPlaying;
    if (continuous)
        continuous = (tr.hasSampleTime && hadSampleTime) ? tr.timeInSamples == lastSampleTime + lastBlockLen
                                                         : std::abs (p0 - expectedPpq) <= 0.5 * numSamples * pps + 1.0e-9;
    if (! continuous)
    {
        jumped = true;
        ++passCounter;
        patStart = 0.0;
        if (queuedPattern >= 0) { currentPattern = queuedPattern; queuedPattern = -1; }
        chainAdvance = false;
        if (cfg.chainLen >= 2) { chainPos = 0; currentPattern = cfg.chain[0]; } // play starts the chain from the top
        frontier = p0;
        // trig conditions count loops since play started; a jump (e.g. a REAPER loop wrap) keeps counting
        for (int t = 0; t < kNumTracks; ++t)
            cycleOffset[t] = (started ? 0 : lastCycle[t] + 1) - cycleAt (bank.patterns[currentPattern], t, p0);
    }
    wasPlaying = true;
    hadSampleTime = tr.hasSampleTime;
    lastSampleTime = tr.timeInSamples;
    lastBlockLen = numSamples;
    expectedPpq = p1;

    // This block owns pattern time [a0, b0): it starts where the last block stopped, and leaves the
    // final half sample to the next block so rounding can never put a hit one sample early.
    const double a0 = std::min (frontier, p1), b0 = p1 - 0.5 * pps;

    // chain mode: queue the next link to start exactly when this pattern's loop ends
    if (cfg.chainLen >= 2 && queuedPattern < 0)
    {
        if (chainPos < 0 || chainPos >= cfg.chainLen || cfg.chain[chainPos] != currentPattern)
        {
            chainPos = -1;
            for (int i = 0; i < cfg.chainLen && chainPos < 0; ++i)
                if (cfg.chain[i] == currentPattern) chainPos = i;
        }
        const double len = bank.patterns[currentPattern].lengthPpq (tr.barLen);
        // max(0, ...): during a pre-roll (negative ppq) the first link still plays its full length
        switchPpq = patStart + (std::max (0.0, std::floor ((a0 - patStart) / len + 1.0e-9)) + 1.0) * len;
        queuedPattern = cfg.chain[(chainPos + 1) % cfg.chainLen];
        resetOnSwitch = true;
        chainAdvance = true;
    }
    else if (cfg.chainLen < 2)
        chainAdvance = false;
    lastSeed = cfg.lock ? cfg.seed : hashMix (cfg.seed, passCounter);

    // looper: grab the slice of the grid we're in and keep replaying it
    if (cfg.loopLen <= 0) loopActive = 0;
    else if (jumped || loopActive <= 0 || loopActive != cfg.loopLen)
    {
        if (jumped || loopActive <= 0)
            loopStart = patStart + std::floor ((a0 - patStart) / cfg.loopLen + 1.0e-9) * cfg.loopLen;
        loopActive = cfg.loopLen;
    }

    if (queuedPattern >= 0 && switchPpq < b0)
    {
        const double s = std::max (switchPpq, a0);
        emit (a0, s, p0, pps, numSamples, bank.patterns[currentPattern], cfg, out);
        currentPattern = queuedPattern;
        queuedPattern = -1;
        if (resetOnSwitch)
        {
            patStart = s;
            for (auto& c : cycleOffset) c = 0; // a freshly started pattern is on its "First" loop
        }
        if (chainAdvance) { chainPos = (chainPos + 1) % std::max (1, cfg.chainLen); chainAdvance = false; }
        if (loopActive > 0) loopStart = s;
        emit (s, b0, p0, pps, numSamples, bank.patterns[currentPattern], cfg, out);
    }
    else
        emit (a0, b0, p0, pps, numSamples, bank.patterns[currentPattern], cfg, out);
    frontier = std::max (frontier, b0);

    const auto& pat = bank.patterns[currentPattern];
    const double vp0 = loopActive > 0 ? loopStart + std::fmod (std::max (0.0, p0 - loopStart), loopActive) : p0;
    for (int t = 0; t < kNumTracks; ++t)
    {
        lastCycle[t] = cycleAt (pat, t, b0) + cycleOffset[t];
        const auto lane = pat.lane (t);
        const auto n = (int64_t) std::floor ((vp0 - patStart) / divPpq (lane.div));
        playPos[t] = n < 0 ? -1 : stepIndex (n, lane.length, lane.dir, hashMix (lastSeed, (uint32_t) t + 1, (uint32_t) n) ^ 0xabcdefu);
    }
}

void Sequencer::emit (double a, double b, double p0, double pps, int numSamples, const Pattern& pat,
                      const SeqConfig& cfg, FixedVec<SeqHit, 2048>& out) noexcept
{
    if (loopActive <= 0)
    {
        generate (a, b, 0.0, p0, pps, numSamples, pat, cfg, lastSeed, out);
        return;
    }
    // real time t plays pattern time loopStart + ((t - loopStart) mod L): one generate per repeat
    const double L = loopActive;
    for (double k = std::floor ((a - loopStart) / L); loopStart + k * L < b; k += 1.0)
    {
        const double ra = std::max (a, loopStart + k * L), rb = std::min (b, loopStart + (k + 1.0) * L);
        if (rb > ra) generate (ra - k * L, rb - k * L, k * L, p0, pps, numSamples, pat, cfg, lastSeed, out);
    }
}

void Sequencer::generate (double a, double b, double shift, double p0, double pps, int numSamples, const Pattern& pat,
                          const SeqConfig& cfg, uint32_t seed, FixedVec<SeqHit, 2048>& out) noexcept
{
    if (b <= a) return;
    // [a, b) is pattern time owned by this block; offsets round to the nearest sample
    const auto offsetOf = [p0, pps, shift, numSamples] (double ppq) {
        return (int) std::clamp<int64_t> ((int64_t) std::floor ((ppq + shift - p0) / pps + 0.5), 0, numSamples - 1);
    };
    for (int t = 0; t < kNumTracks; ++t)
    {
        const Lane lane = pat.lane (t);
        const int len = lane.length;
        const double stepLen = divPpq (lane.div);
        const double sw = clampf ((cfg.swing + (float) lane.swing - 50.0f) / 50.0f, 0.0f, 0.5f);
        const float r = cfg.rndSeq[t], dens = cfg.density[t];
        const auto nLo = std::max<int64_t> (0, (int64_t) std::floor ((a - patStart) / stepLen) - 2);
        const auto nHi = (int64_t) std::floor ((b - patStart) / stepLen) + 1;
        const int64_t cyclePeriod = lane.dir == DirPingPong ? std::max (1, 2 * len - 2) : len;

        for (int64_t n = nLo; n <= nHi; ++n)
        {
            const uint32_t key = hashMix (seed, (uint32_t) t + 1, (uint32_t) n);
            const Step st = pat.step (t, stepIndex (n, len, lane.dir, key ^ 0xabcdefu));
            float vel, pitch = 0, gateFrac = 0.5f, nudge = 0;
            int ratchets = 1;
            if (st.on)
            {
                if (! condOk (st.cond, n / cyclePeriod + cycleOffset[t], cfg.fill)) continue;
                float prob = (float) st.prob * 0.01f * (1.0f - 0.5f * r);
                if (dens < 0) prob *= 1.0f + dens;
                if (prob < 1.0f && hash01 (key ^ 0x51u) >= prob) continue;
                vel = (float) st.vel / 127.0f;
                if (r > 0) vel = clampf (vel + hashBi (key ^ 0x52u) * r * 0.25f, 0.05f, 1.0f);
                ratchets = st.ratchet;
                if (r > 0 && ratchets == 1 && hash01 (key ^ 0x53u) < r * 0.12f) ratchets = 2 + (int) (hash32 (key) & 1);
                pitch = st.pitch;
                gateFrac = (float) st.gate / 16.0f;
                nudge = (float) st.nudge * 0.01f;
            }
            else
            {
                const float pg = r * r * 0.3f + std::max (0.0f, dens) * 0.35f;
                if (pg <= 0 || hash01 (key ^ 0x54u) >= pg) continue;
                vel = 0.2f + 0.3f * hash01 (key ^ 0x55u); // ghost note
            }

            double base = patStart + (double) n * stepLen;
            if (n & 1) base += sw * stepLen;
            base += nudge * stepLen;
            if (r > 0) base += hashBi (key ^ 0x56u) * r * 0.12f * stepLen;
            base = std::max (base, patStart);
            const double sub = stepLen / ratchets;
            const double gatePpq = ratchets > 1 ? std::min (gateFrac, 0.9f) * sub : gateFrac * stepLen;

            for (int k = 0; k < ratchets; ++k)
            {
                const double tk = base + k * sub;
                if (tk < a || tk >= b) continue;
                out.push ({ offsetOf (tk), t, vel, pitch, gatePpq / pps, hashMix (key, (uint32_t) k + 77) });
            }
        }
    }
}
} // namespace gm
