#include "Pattern.h"

namespace gm
{
const char* condName (int c)
{
    static const char* names[] = { "Always", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4",
                                   "Fill", "!Fill", "First", "!First" };
    return names[std::clamp (c, 0, (int) CondCount - 1)];
}

const char* dirName (int d)
{
    static const char* names[] = { "Fwd", "Rev", "P-P", "Rnd" };
    return names[std::clamp (d, 0, (int) DirCount - 1)];
}

static const struct { const char* name; double ppq; } kDivs[kNumDivisions] = {
    { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/4T", 2.0 / 3.0 }, { "1/8D", 0.75 }, { "1/8", 0.5 },
    { "1/8T", 1.0 / 3.0 }, { "1/16D", 0.375 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 },
    { "1/32", 0.125 }, { "1/32T", 1.0 / 12.0 }, { "1/64", 0.0625 }
};
const char* divName (int d) { return kDivs[std::clamp (d, 0, kNumDivisions - 1)].name; }
double divPpq (int d) { return kDivs[std::clamp (d, 0, kNumDivisions - 1)].ppq; }

//==============================================================================
void Pattern::clear() noexcept
{
    for (int t = 0; t < kNumTracks; ++t)
        clearTrack (t);
}

void Pattern::clearTrack (int t) noexcept
{
    const auto empty = Step{}.pack();
    for (auto& s : steps[t])
        s.store (empty, std::memory_order_relaxed);
    lanes[t].store (Lane{}.pack(), std::memory_order_relaxed);
}

void Pattern::copyFrom (const Pattern& o) noexcept
{
    for (int t = 0; t < kNumTracks; ++t)
        copyTrackFrom (t, o, t);
}

void Pattern::copyTrackFrom (int t, const Pattern& o, int srcTrack) noexcept
{
    for (int s = 0; s < kMaxSteps; ++s)
        steps[t][s].store (o.steps[srcTrack][s].load (std::memory_order_relaxed), std::memory_order_relaxed);
    lanes[t].store (o.lanes[srcTrack].load (std::memory_order_relaxed), std::memory_order_relaxed);
}

double Pattern::lengthPpq (double barLen) const noexcept
{
    double longest = 0;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto l = lane (t);
        bool any = false;
        for (int s = 0; s < l.length && ! any; ++s)
            any = (steps[t][s].load (std::memory_order_relaxed) & 1) != 0;
        if (any) longest = std::max (longest, l.length * divPpq (l.div));
    }
    return std::max (1.0, std::ceil (longest / barLen - 1.0e-6)) * barLen;
}

bool Pattern::isEmpty() const noexcept
{
    for (auto& tr : steps)
        for (auto& s : tr)
            if (s.load (std::memory_order_relaxed) & 1) return false;
    return true;
}

void PatternSnapshot::take (const Pattern& p) noexcept
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        lanes[t] = p.lanes[t].load (std::memory_order_relaxed);
        for (int s = 0; s < kMaxSteps; ++s)
            steps[t][s] = p.steps[t][s].load (std::memory_order_relaxed);
    }
}

void PatternSnapshot::restore (Pattern& p) const noexcept
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        p.lanes[t].store (lanes[t], std::memory_order_relaxed);
        for (int s = 0; s < kMaxSteps; ++s)
            p.steps[t][s].store (steps[t][s], std::memory_order_relaxed);
    }
}

//==============================================================================
static constexpr int kBankMagic = 0x42504d47; // "GMPB"
static constexpr int kBankVersion = 1;

juce::MemoryBlock PatternBank::serialise() const
{
    juce::MemoryBlock raw;
    {
        juce::MemoryOutputStream out (raw, false);
        out.writeInt (kBankMagic);
        out.writeInt (kBankVersion);
        out.writeInt (kNumPatterns);
        out.writeInt (kNumTracks);
        out.writeInt (kMaxSteps);
        for (auto& p : patterns)
            for (int t = 0; t < kNumTracks; ++t)
            {
                out.writeInt ((int) p.lanes[t].load (std::memory_order_relaxed));
                for (int s = 0; s < kMaxSteps; ++s)
                    out.writeInt64 ((juce::int64) p.steps[t][s].load (std::memory_order_relaxed));
            }
    }
    juce::MemoryBlock zipped;
    {
        juce::MemoryOutputStream zout (zipped, false);
        juce::GZIPCompressorOutputStream gz (zout, 9);
        gz.write (raw.getData(), raw.getSize());
        gz.flush();
    }
    return zipped;
}

bool PatternBank::deserialise (const juce::MemoryBlock& mb)
{
    juce::MemoryInputStream zin (mb, false);
    juce::GZIPDecompressorInputStream gz (zin);
    juce::MemoryBlock raw;
    gz.readIntoMemoryBlock (raw);
    juce::MemoryInputStream in (raw, false);
    if (in.readInt() != kBankMagic || in.readInt() > kBankVersion) return false;
    const int np = in.readInt(), nt = in.readInt(), ns = in.readInt();
    if (np <= 0 || nt <= 0 || ns <= 0 || np > 256 || nt > 256 || ns > 1024) return false;
    for (int pi = 0; pi < np; ++pi)
        for (int t = 0; t < nt; ++t)
        {
            const auto lane = (uint32_t) in.readInt();
            const bool keep = pi < kNumPatterns && t < kNumTracks;
            if (keep) patterns[pi].setLane (t, Lane::unpack (lane));
            for (int s = 0; s < ns; ++s)
            {
                const auto v = (uint64_t) in.readInt64();
                if (keep && s < kMaxSteps) patterns[pi].setStep (t, s, Step::unpack (v));
            }
        }
    return true;
}

//==============================================================================
namespace gen
{
static void writeBits (Pattern& p, int track, const bool* bits, int n, const uint8_t* vels)
{
    for (int s = 0; s < n; ++s) // steps past n are left alone: they're kept for later
    {
        auto st = p.step (track, s);
        st.on = bits[s];
        if (st.on) { st.vel = vels ? vels[s] : 100; st.prob = 100; st.ratchet = 1; st.cond = CondAlways; }
        p.setStep (track, s, st);
    }
}

void euclid (Pattern& p, int track, int hits, int steps, int rotate, uint8_t vel)
{
    steps = std::clamp (steps, 1, kMaxSteps);
    hits = std::clamp (hits, 0, steps);
    bool bits[kMaxSteps] {};
    uint8_t vels[kMaxSteps] {};
    for (int i = 0; i < steps; ++i)
    {
        const int src = ((i - rotate) % steps + steps) % steps;
        bits[i] = ((src * hits) % steps) < hits;
        vels[i] = vel;
    }
    writeBits (p, track, bits, steps, vels);
    auto l = p.lane (track);
    l.length = (uint8_t) steps;
    p.setLane (track, l);
}

void random (Pattern& p, int track, float density, Rng& rng, bool accents)
{
    const int n = p.lane (track).length;
    bool bits[kMaxSteps] {};
    uint8_t vels[kMaxSteps] {};
    for (int s = 0; s < n; ++s)
    {
        const bool onBeat = (s % 4) == 0;
        const float d = clampf (density * (onBeat ? 1.4f : 0.85f), 0, 1);
        bits[s] = rng.uni() < d;
        vels[s] = (uint8_t) (accents ? (onBeat ? 105 + rng.uni() * 22 : 55 + rng.uni() * 55) : 100);
    }
    writeBits (p, track, bits, n, vels);
}

// 1 = a single dot (odd/active), 0 = two dots. Rows: head, neck, body, feet (MSB first).
const char* figureName (uint8_t b)
{
    static const char* names[16] = {
        "Populus", "Tristitia", "Albus", "Fortuna Major", "Rubeus", "Acquisitio", "Conjunctio", "Caput Draconis",
        "Laetitia", "Carcer", "Amissio", "Puella", "Fortuna Minor", "Puer", "Cauda Draconis", "Via" };
    return names[b & 15];
}

// Builds the 16 figures of a shield chart from four Mothers.
static void castChart (Rng& rng, uint8_t fig[16])
{
    uint8_t m[4], d[4];
    for (auto& x : m) x = (uint8_t) (rng.next() & 15);
    for (int i = 0; i < 4; ++i) // Daughters: row i of each mother, read across
    {
        d[i] = 0;
        for (int j = 0; j < 4; ++j)
            d[i] |= (uint8_t) (((m[j] >> (3 - i)) & 1) << (3 - j));
    }
    // Adding figures = odd/even dot parity per row = XOR.
    const uint8_t n1 = m[0] ^ m[1], n2 = m[2] ^ m[3], n3 = d[0] ^ d[1], n4 = d[2] ^ d[3];
    const uint8_t rw = n1 ^ n2, lw = n3 ^ n4, judge = rw ^ lw, reconciler = judge ^ m[0];
    const uint8_t all[16] = { m[0], m[1], m[2], m[3], d[0], d[1], d[2], d[3], n1, n2, n3, n4, rw, lw, judge, reconciler };
    std::copy (all, all + 16, fig);
}

static const uint8_t kRowVel[4] = { 120, 78, 100, 68 }; // head, neck, body, feet

juce::String geomancy (Pattern& p, int track, Rng& rng)
{
    uint8_t fig[16];
    castChart (rng, fig);
    bool bits[kMaxSteps] {};
    uint8_t vels[kMaxSteps] {};
    for (int f = 0; f < 16; ++f)
        for (int r = 0; r < 4; ++r)
        {
            bits[f * 4 + r] = (fig[f] >> (3 - r)) & 1;
            vels[f * 4 + r] = kRowVel[r];
        }
    writeBits (p, track, bits, 64, vels);
    return figureName (fig[14]);
}

juce::String geomancyKit (Pattern& p, Rng& rng)
{
    uint8_t fig[16];
    castChart (rng, fig);
    for (int t = 0; t < kNumTracks; ++t)
    {
        bool bits[kMaxSteps] {};
        uint8_t vels[kMaxSteps] {};
        const int len = p.lane (t).length;
        for (int s = 0; s < len; ++s)
        {
            const int f = ((s / 4) * 4 + t) % 16, r = s % 4;
            bool b = (fig[f] >> (3 - r)) & 1;
            if (t >= 4) b = b && ((fig[(f + 7) % 16] >> (3 - r)) & 1); // thin out the upper voices
            bits[s] = b;
            vels[s] = kRowVel[r];
        }
        writeBits (p, t, bits, len, vels);
    }
    return figureName (fig[14]);
}

void mutate (Pattern& p, int track, float amount, Rng& rng)
{
    const int n = p.lane (track).length;
    for (int s = 0; s < n; ++s)
    {
        auto st = p.step (track, s);
        if (rng.uni() < amount * 0.2f) st.on = ! st.on;
        if (st.on && rng.uni() < amount * 0.4f) st.vel = (uint8_t) std::clamp<int> (st.vel + (int) (rng.bi() * 40), 20, 127);
        if (st.on && rng.uni() < amount * 0.08f) st.ratchet = (uint8_t) (1 + (rng.next() % 3));
        p.setStep (track, s, st);
    }
}

void shift (Pattern& p, int track, int by)
{
    const int n = p.lane (track).length;
    uint64_t tmp[kMaxSteps];
    for (int s = 0; s < n; ++s) tmp[s] = p.steps[track][s].load (std::memory_order_relaxed);
    for (int s = 0; s < n; ++s)
        p.steps[track][((s + by) % n + n) % n].store (tmp[s], std::memory_order_relaxed);
}

void reverse (Pattern& p, int track)
{
    const int n = p.lane (track).length;
    uint64_t tmp[kMaxSteps];
    for (int s = 0; s < n; ++s) tmp[s] = p.steps[track][s].load (std::memory_order_relaxed);
    for (int s = 0; s < n; ++s)
        p.steps[track][n - 1 - s].store (tmp[s], std::memory_order_relaxed);
}
} // namespace gen
} // namespace gm
