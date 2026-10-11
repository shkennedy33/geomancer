#include "Voice.h"

namespace gm
{
namespace
{
    struct HannTable
    {
        static constexpr int N = 1024;
        float t[N + 1];
        HannTable()
        {
            for (int i = 0; i <= N; ++i)
            {
                const float s = std::sin (kPi * (float) i / (float) N);
                t[i] = s * s;
            }
        }
        inline float at (int age, int len) const noexcept { return t[std::min (N, (int) ((int64_t) age * N / len))]; }
    };
    const HannTable hann;

    inline float coefFor (float ms, double sr, float target = 1.0e-4f) noexcept
    {
        return std::exp (std::log (target) / (float) std::max (1.0, ms * 0.001 * sr));
    }
}

void Voice::start (SampleData* s, const VoiceParams& p, const HitMods& m, float velocity, double gateSamples) noexcept
{
    smp = s;
    if (s == nullptr || s->length() < 2)
    {
        finish();
        return;
    }
    mods = m;
    const int len = s->length();
    stereo = s->buffer.getNumChannels() > 1;
    srcPerOut = s->sampleRate / hostSR;

    float st = clampf (p.start + m.startAdd, 0.0f, 1.0f), en = clampf (p.end, 0.0f, 1.0f);
    if (en < st) std::swap (st, en);
    if (en - st < 0.002f) en = std::min (1.0f, st + 0.002f);
    startPos = st * len;
    endPos = en * len;
    const bool rev = p.reverse != m.flipReverse;
    dirSign = rev ? -1 : 1;
    pos = rev ? endPos - 1.0 : startPos;
    declick = (st > 0.0005f || rev) ? kDeclick : 0;

    stretchMode = p.stretchMode;
    for (auto& g : grains) g.on = false;
    if (stretchMode)
    {
        grainLen = std::max (64, (int) (p.grainMs * 0.001 * hostSR));
        head = pos;
        grains[0] = { head, 0.0, grainLen / 2, grainLen, true }; // born at the window peak: keeps the transient
        lastGrain = 0;
        hop = 0;
        headDone = false;
    }

    gateMode = p.gateMode;
    held = gateSamples < 0;
    gateLeft = gateSamples;
    const float attackSamples = p.attackMs * 0.001f * (float) hostSR;
    if (attackSamples < 1.0f) { env = 1.0f; stage = gateMode ? Sustain : Hold; }
    else { env = 0.0f; attackInc = 1.0f / attackSamples; stage = Attack; }
    holdLeft = (int) (p.holdMs * 0.001f * (float) hostSR);
    decayCoef = p.decayInf ? 1.0f : coefFor (p.decayMs * m.decayMul, hostSR);
    releaseCoef = p.decayInf ? coefFor (15.0f, hostSR) : decayCoef;

    pEnv = p.pAmt;
    pEnvCoef16 = std::pow (0.01f, 16.0f / (float) std::max (1.0, p.pDecMs * 0.001 * hostSR));
    fEnvVal = 1.0f;
    fEnvCoef16 = std::pow (0.01f, 16.0f / (float) std::max (1.0, p.fDecMs * 0.001 * hostSR));

    killing = false;
    killGain = 1.0f;
    const float v = clampf (velocity, 0.0f, 1.0f);
    velGain = (1.0f - p.velSens * (1.0f - v * std::sqrt (v))) * m.gain;
    const float pan = clampf (m.pan, -1.0f, 1.0f);
    gainL = pan > 0 ? 1.0f - pan : 1.0f;
    gainR = pan < 0 ? 1.0f + pan : 1.0f;

    svf[0].reset();
    svf[1].reset();
    active = true;
    ctrl = 0;
}

void Voice::release() noexcept
{
    held = false;
    gateLeft = 0;
}

void Voice::kill (float ms) noexcept
{
    if (! active) return;
    const float step = 1.0f / std::max (1.0f, ms * 0.001f * (float) hostSR);
    killStep = killing ? std::max (killStep, step) : step;
    killing = true;
}

void Voice::finish() noexcept
{
    active = false;
    killing = false;
    stage = Done;
    smp = nullptr; // refcount drop only; the pool owns the memory
}

inline void Voice::readFrame (double p, float& l, float& r) const noexcept
{
    const int len = smp->length();
    l = readHermite (smp->chan (0), len, p);
    r = stereo ? readHermite (smp->chan (1), len, p) : l;
}

// WSOLA: start the new grain where the source best continues the grain that is fading out, searched
// within a few ms of where the stretch head says it should start. Without this, overlapping grains
// read the source out of phase and low tones flutter (an 808 boom at 2x pulsed by 11 dB).
double Voice::alignGrain (double target, const Grain& prev) const noexcept
{
    const int len = smp->length();
    const float* x = smp->chan (0);
    const double step = prev.inc;                          // both grains read at the same rate
    const int window = (int) std::min (0.006 * smp->sampleRate, grainLen * 0.25 * srcPerOut);
    constexpr int terms = 64, stride = 3;
    auto at = [x, len] (double p) { const int i = (int) p; return (i >= 0 && i < len) ? x[i] : 0.0f; };

    float ref[terms];
    for (int k = 0; k < terms; ++k) ref[k] = at (prev.pos + k * stride * step);

    double best = target, bestScore = -1.0e30;
    for (int d = -window; d <= window; d += 2)
    {
        const double c = target + d;
        if (c < startPos || c >= endPos) continue;
        float dot = 0, energy = 1.0e-9f;
        for (int k = 0; k < terms; ++k)
        {
            const float v = at (c + k * stride * step);
            dot += v * ref[k];
            energy += v * v;
        }
        const double score = dot / std::sqrt (energy) - 1.0e-4 * std::abs (d); // tie-break toward the true head
        if (score > bestScore) { bestScore = score; best = c; }
    }
    return best;
}

void Voice::updateControl (const VoiceParams& live) noexcept
{
    ratio = semitonesToRatio (live.tune + mods.pitch + pEnv);
    if (stretchMode)
    {
        headInc = dirSign * srcPerOut / std::max (0.05f, live.stretch * mods.stretchMul);
        const double ginc = dirSign * ratio * srcPerOut;
        for (auto& g : grains) g.inc = ginc;
    }
    else
        inc = dirSign * ratio * srcPerOut;

    fType = live.fType;
    if (fType != SVF::Off)
    {
        const float cutoff = live.cutoff * std::exp2 (mods.cutoffOct + live.fEnv * 8.0f * fEnvVal);
        svf[0].set (cutoff, live.reso, hostSR);
        svf[1].g = svf[0].g; svf[1].k = svf[0].k;
        svf[1].a1 = svf[0].a1; svf[1].a2 = svf[0].a2; svf[1].a3 = svf[0].a3;
    }
    pEnv *= pEnvCoef16;
    fEnvVal *= fEnvCoef16;
}

void Voice::render (float* L, float* R, int n, const VoiceParams& live) noexcept
{
    for (int i = 0; i < n && active; ++i)
    {
        if (--ctrl < 0) { updateControl (live); ctrl = 15; }

        float l = 0, r = 0;
        if (! stretchMode)
        {
            if ((dirSign > 0 && pos >= endPos) || (dirSign < 0 && pos < startPos)) { finish(); return; }
            readFrame (pos, l, r);
            const double dist = dirSign > 0 ? endPos - pos : pos - startPos;
            if (dist < 64.0) { const float g = (float) (dist / 64.0); l *= g; r *= g; }
            pos += inc;
        }
        else
        {
            bool any = false;
            for (auto& g : grains)
            {
                if (! g.on) continue;
                any = true;
                if (g.pos >= startPos && g.pos < endPos)
                {
                    float gl, gr;
                    readFrame (g.pos, gl, gr);
                    // fade as the grain approaches the marker it is travelling towards
                    const double dist = g.inc >= 0 ? endPos - g.pos : g.pos - startPos;
                    const float w = hann.at (g.age, g.len) * (dist < 64.0 ? (float) (dist / 64.0) : 1.0f);
                    l += gl * w;
                    r += gr * w;
                }
                g.pos += g.inc;
                if (++g.age >= g.len) g.on = false;
            }
            if (! headDone)
            {
                if (--hop <= 0)
                {
                    const double start = grains[lastGrain].on ? alignGrain (head, grains[lastGrain]) : head;
                    for (int gi = 0; gi < 4; ++gi)
                        if (! grains[gi].on)
                        {
                            grains[gi] = { start, dirSign * ratio * srcPerOut, 0, grainLen, true };
                            lastGrain = gi;
                            break;
                        }
                    hop = grainLen / 2;
                    any = true;
                }
                head += headInc;
                headDone = dirSign > 0 ? head >= endPos : head < startPos;
            }
            else if (! any) { finish(); return; }
        }

        switch (stage)
        {
            case Attack:
                env += attackInc;
                if (env >= 1.0f) { env = 1.0f; stage = gateMode ? Sustain : Hold; }
                if (gateMode && ! held && gateLeft <= 0) stage = Release;
                break;
            case Hold:    if (--holdLeft <= 0) stage = Decay; break;
            case Sustain: if (! held && gateLeft <= 0) stage = Release; break;
            case Decay:   env *= decayCoef; if (env < 1.0e-4f) { finish(); return; } break;
            case Release: env *= releaseCoef; if (env < 1.0e-4f) { finish(); return; } break;
            case Done:    return;
        }
        if (! held) gateLeft -= 1.0;

        float a = env * velGain;
        if (declick > 0) { a *= (float) (kDeclick - declick) / (float) kDeclick; --declick; }
        if (killing)
        {
            killGain -= killStep;
            if (killGain <= 0.0f) { finish(); return; }
            a *= killGain;
        }

        if (fType != SVF::Off)
        {
            l = svf[0].process (l, fType);
            r = stereo ? svf[1].process (r, fType) : l;
        }
        L[i] += l * a * gainL;
        R[i] += r * a * gainR;
    }
}
} // namespace gm
