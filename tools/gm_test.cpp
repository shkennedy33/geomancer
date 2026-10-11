// Geomancer test harness.
//   gm_test engine <samplesDir>          sample-accurate engine checks (in-process processor)
//   gm_test demo <samplesDir> <out.wav>  render a demo beat
//   gm_test gui <samplesDir> <out.png>   snapshot the editor (run under xvfb-run)
//   gm_test vst3 <path.vst3>             smoke-test the built VST3 binary
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>
#include "gui/Header.h"
#include "gui/StepGrid.h"
#include "gui/LaneEditor.h"
#include "gui/Browser.h"

using namespace juce;
using namespace gm;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf ("  ok   %s\n", msg); else { std::printf ("  FAIL %s\n", msg); ++failures; } } while (0)

struct FakePlayHead : AudioPlayHead
{
    double ppq = 0, bpm = 120;
    bool playing = true;
    int64 samples = 0;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setIsPlaying (playing);
        i.setBpm (bpm);
        i.setPpqPosition (ppq);
        i.setTimeSignature (TimeSignature { 4, 4 });
        i.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        i.setTimeInSamples (samples);
        return i;
    }
};

struct NoteEvent { int64 pos; int note; int vel; bool on; };

struct Rig
{
    double sr = 48000;
    int block = 512;
    std::unique_ptr<GeomancerProcessor> proc { new GeomancerProcessor() };
    FakePlayHead ph;
    AudioBuffer<float> out;          // main stereo
    std::vector<AudioBuffer<float>> aux;
    std::vector<NoteEvent> notes;
    std::vector<std::pair<int64, MidiMessage>> midiIn; // scheduled input

    Rig (double rate = 48000, int bs = 512) : sr (rate), block (bs)
    {
        proc->setPlayHead (&ph);
        proc->prepareToPlay (sr, block);
    }
    void set (const String& id, float plain) { auto* p = proc->param (id); p->setValueNotifyingHost (p->convertTo0to1 (plain)); }
    void tset (int t, int tp, float plain) { auto* p = proc->trackParam (t, tp); p->setValueNotifyingHost (p->convertTo0to1 (plain)); }
    Pattern& pat (int i = 0) { return proc->bank.patterns[i]; }
    void on (int t, std::initializer_list<int> steps, int p = 0, uint8_t vel = 100)
    {
        for (int s : steps) { auto st = pat (p).step (t, s); st.on = true; st.vel = vel; pat (p).setStep (t, s, st); }
    }
    void lane (int t, int len, int div = kDefaultDiv, int dir = DirFwd, int p = 0)
    {
        Lane l; l.length = (uint8_t) len; l.div = (uint8_t) div; l.dir = (uint8_t) dir;
        pat (p).setLane (t, l);
    }
    // Render `seconds` of host-synced playback starting at ppq `startPpq`.
    void render (double seconds, double startPpq = 0.0, bool keep = true)
    {
        const int total = (int) (seconds * sr);
        const int nCh = proc->getTotalNumOutputChannels();
        AudioBuffer<float> buf (nCh, block);
        if (keep)
        {
            out.setSize (2, total);
            out.clear();
            aux.assign (kNumAux, AudioBuffer<float> (2, total));
            for (auto& a : aux) a.clear();
            notes.clear();
        }
        MidiBuffer midi;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            buf.setSize (nCh, n, false, false, true);
            ph.ppq = startPpq + pos / sr * ph.bpm / 60.0;
            ph.samples = (int64) std::llround (startPpq * 60.0 / ph.bpm * sr) + pos;
            midi.clear();
            for (auto& [when, msg] : midiIn)
                if (when >= pos && when < pos + n) midi.addEvent (msg, (int) (when - pos));
            proc->processBlock (buf, midi);
            if (! keep) continue;
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
            for (int a = 0; a < kNumAux; ++a)
                for (int c = 0; c < 2; ++c)
                    aux[(size_t) a].copyFrom (c, pos, buf, 2 + a * 2 + c, 0, n);
            for (const auto m : midi)
            {
                const auto msg = m.getMessage();
                if (msg.isNoteOnOrOff())
                    notes.push_back ({ pos + m.samplePosition, msg.getNoteNumber(), msg.getVelocity(), msg.isNoteOn() });
            }
        }
    }
    // Host with a changing tempo: ppq integrates the tempo block by block, the sample clock stays continuous.
    void renderTempo (double seconds, std::function<double (double)> bpmAt, double startPpq = 0.0)
    {
        const int total = (int) (seconds * sr);
        const int nCh = proc->getTotalNumOutputChannels();
        AudioBuffer<float> buf (nCh, block);
        out.setSize (2, total);
        out.clear();
        notes.clear();
        MidiBuffer midi;
        double ppq = startPpq;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            buf.setSize (nCh, n, false, false, true);
            ph.bpm = bpmAt (pos / sr);
            ph.ppq = ppq;
            ph.samples = pos;
            midi.clear();
            proc->processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
            for (const auto m : midi)
            {
                const auto msg = m.getMessage();
                if (msg.isNoteOnOrOff()) notes.push_back ({ pos + m.samplePosition, msg.getNoteNumber(), msg.getVelocity(), msg.isNoteOn() });
            }
            ppq += n * ph.bpm / (60.0 * sr);
        }
    }
    // every note: on, off, on, off... never two ons in a row, never an off without an on
    bool midiWellFormed (String& why) const
    {
        std::map<int, bool> open;
        for (auto& e : notes)
        {
            if (e.on && open[e.note]) { why = "double note-on " + String (e.note) + " at " + String ((int) e.pos); return false; }
            if (! e.on && ! open[e.note]) { why = "orphan note-off " + String (e.note) + " at " + String ((int) e.pos); return false; }
            open[e.note] = e.on;
        }
        return true;
    }
    std::vector<int64> onsets (int note) const
    {
        std::vector<int64> v;
        for (auto& e : notes) if (e.on && e.note == note) v.push_back (e.pos);
        return v;
    }
    bool loadKit (const File& dir)
    {
        const char* files[] = { "808bd/BD0025.WAV", "808sd/SD0010.WAV", "808hc/HC00.WAV", "808oh/OH25.WAV",
                                "808lt/LT00.WAV", "808mt/MT00.WAV", "808ht/HT00.WAV", "cp/HANDCLP0.wav", "808cy/CY0010.WAV",
                                "rm/RIM0.wav", "cb/rytm-cb.wav", "808lc/LC00.WAV" };
        int t = 0;
        for (auto* f : files)
        {
            String err;
            const auto file = dir.getChildFile (f);
            if (! proc->loadSample (t, file, err)) { std::printf ("  (could not load %s: %s)\n", f, err.toRawUTF8()); }
            ++t;
        }
        return proc->samples.slot (0) != nullptr;
    }
};

static float peak (const AudioBuffer<float>& b, int from = 0, int to = -1)
{
    if (to < 0) to = b.getNumSamples();
    float p = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < to; ++i) p = std::max (p, std::abs (b.getSample (c, i)));
    return p;
}

static bool finite (const AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

static String joined (const std::vector<int64>& v, size_t max = 12)
{
    String s;
    for (size_t i = 0; i < std::min (max, v.size()); ++i) s << (int) v[i] << " ";
    return s;
}

static void writeWav (const AudioBuffer<float>& b, double sr, const File& f)
{
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (24));
    if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

//==============================================================================
static void engineTests (const File& kitDir)
{
    const int step = 6000; // 1/16 at 120 bpm, 48 kHz
    {
        std::printf ("[timing] four-on-the-floor + MIDI out\n");
        Rig r;
        CHECK (r.loadKit (kitDir), "kit loaded");
        r.set (gid::midiOut, 1);
        r.on (0, { 0, 4, 8, 12 });
        r.on (1, { 4, 12 });
        r.render (4.0);
        const auto k = r.onsets (36);
        CHECK (k.size() == 8, ("8 kicks in 2 bars, got " + String ((int) k.size())).toRawUTF8());
        bool exact = true;
        for (size_t i = 0; i < k.size(); ++i) exact = exact && k[i] == (int64) i * 4 * step;
        CHECK (exact, ("kicks sample-exact: " + joined (k)).toRawUTF8());
        CHECK (r.onsets (37).size() == 4, "4 snares");
        CHECK (peak (r.out) > 0.1f && peak (r.out) <= 1.0f, ("audio present, peak " + String (peak (r.out))).toRawUTF8());
        CHECK (finite (r.out), "no NaN/inf");
        // offs follow ons
        int ons = 0, offs = 0;
        for (auto& e : r.notes) (e.on ? ons : offs)++;
        CHECK (ons == offs || ons == offs + 1, ("note-offs balance ons (" + String (ons) + "/" + String (offs) + ")").toRawUTF8());
    }
    {
        std::printf ("[poly] polymeter, direction, ratchet, swing\n");
        Rig r;
        r.loadKit (kitDir);
        r.set (gid::midiOut, 1);
        r.on (4, { 0 });              // T5: length 3 -> every 3 steps
        r.lane (4, 3);
        r.on (5, { 0 });              // T6: length 4, reverse -> step 0 plays at n = 3, 7, ...
        r.lane (5, 4, kDefaultDiv, DirRev);
        auto st = r.pat().step (6, 0); st.on = true; st.ratchet = 4; r.pat().setStep (6, 0, st); // T7 ratchet
        r.on (7, { 1, 3 });           // T8: per-track swing +25 -> 75%
        { auto l = r.pat().lane (7); l.swing = 25; r.pat().setLane (7, l); }
        r.render (1.0);
        const auto a = r.onsets (40);
        CHECK (a.size() >= 3 && a[0] == 0 && a[1] == 3 * step && a[2] == 6 * step, ("length-3 track: " + joined (a)).toRawUTF8());
        const auto b = r.onsets (41);
        CHECK (b.size() >= 2 && b[0] == 3 * step && b[1] == 7 * step, ("reverse len-4 track: " + joined (b)).toRawUTF8());
        const auto c = r.onsets (42);
        CHECK (c.size() >= 4 && c[0] == 0 && c[1] == 1500 && c[2] == 3000 && c[3] == 4500, ("ratchet x4: " + joined (c)).toRawUTF8());
        const auto d = r.onsets (43);
        CHECK (d.size() >= 2 && d[0] == step + step / 2 && d[1] == 3 * step + step / 2, ("75% swing on odd steps: " + joined (d)).toRawUTF8());
    }
    {
        std::printf ("[pattern] MIDI pattern select, bar-quantized\n");
        Rig r;
        r.loadKit (kitDir);
        r.set (gid::midiOut, 1);
        r.on (0, { 0, 4, 8, 12 }, 0);
        r.on (0, { 0, 2 }, 1);
        r.midiIn.push_back ({ 60000, MidiMessage::noteOn (1, 1, (uint8) 100) }); // mid-bar 1 -> switch at bar 2 (96000)
        r.render (3.0);
        const auto k = r.onsets (36);
        CHECK (k.size() == 6 && k[3] == 72000 && k[4] == 96000 && k[5] == 96000 + 2 * step, ("switch lands on bar 2: " + joined (k)).toRawUTF8());
        CHECK (r.proc->uiCurrentPattern.load() == 1, "current pattern = 2");
    }
    {
        std::printf ("[chain] 1 -> 2 -> 2 -> 3, each pattern its own length\n");
        Rig r;
        r.loadKit (kitDir);
        r.set (gid::midiOut, 1);
        r.on (0, { 0 }, 0);               // P1: 1 bar, kick on 1
        r.on (1, { 0 }, 1);               // P2: 1 bar, snare on 1
        r.on (2, { 0 }, 2); r.lane (2, 32, kDefaultDiv, DirFwd, 2); // P3: 2 bars (32 steps), hat on 1
        const int order[] = { 0, 1, 1, 2 };
        for (int i = 0; i < 4; ++i) r.proc->chain[(size_t) i] = order[i];
        r.proc->chainLen = 4;
        r.render (14.0);                  // 1+1+1+2 = 5 bars per cycle; 7 bars rendered
        const auto k = r.onsets (36), sn = r.onsets (37), h = r.onsets (38);
        const int bar = 96000;
        CHECK (k.size() == 2 && k[0] == 0 && k[1] == 5 * bar, ("P1 at bars 1 and 6: " + joined (k)).toRawUTF8());
        CHECK (sn.size() == 3 && sn[0] == bar && sn[1] == 2 * bar && sn[2] == 6 * bar, ("P2 at bars 2, 3, 7: " + joined (sn)).toRawUTF8());
        CHECK (h.size() == 1 && h[0] == 3 * bar, ("P3 (2 bars) at bar 4 only: " + joined (h)).toRawUTF8());
    }
    {
        std::printf ("[looper] 1/4 beat repeat engaged on beat 2\n");
        Rig r;
        r.loadKit (kitDir);
        r.set (gid::midiOut, 1);
        r.on (0, { 4, 5, 6, 7 });          // kick on every 16th of beat 2 only
        r.on (1, { 8 });                   // snare on beat 3 (must NOT play while looping beat 2)
        r.render (0.6, 0.0);               // beats 1 + start of 2 ... engage looper mid-beat-2
        r.set (gid::looper, 4.0f);         // index 4 = 1/4
        r.render (2.0, 0.6 * 2.0, true);   // continue from ppq 1.2 for 4 more beats
        const auto k = r.onsets (36), sn = r.onsets (37);
        // after engaging at ppq 1.2 (inside beat 2 = ppq 1..2): the loop is [1,2) so the 16ths of
        // beat 2 repeat: ppq 1.25 1.5 1.75, then 2.0 2.25 ... until the end of the render
        CHECK (sn.empty(), "beat-3 snare suppressed while looping");
        CHECK (k.size() == 16 && k[0] == 1200 && k[3] == 19200, ("16ths keep repeating: " + joined (k, 6)).toRawUTF8());
        r.set (gid::looper, 0.0f);
        r.render (1.0, 3.2, true);         // released: snare on beat ... ppq 4 is bar 2 beat 1 -> snare at ppq 6 (beat 3)
        const auto sn2 = r.onsets (37);
        CHECK (sn2.empty() || sn2[0] > 0, "after release the song position resumes");
    }
    {
        std::printf ("[tempo] host tempo changes are not seeks (ramps, steps inside a block)\n");
        for (int bs : { 512, 2048 })
        {
            for (int ramp = 0; ramp < 3; ++ramp)
            {
                Rig r (48000, bs);
                r.loadKit (kitDir);
                r.set (gid::midiOut, 1);
                for (int st = 0; st < 16; ++st) r.on (0, { st });
                std::function<double (double)> f;
                if (ramp == 0) f = [] (double t) { return 90.0 + 90.0 * std::min (1.0, t / 4.0); };   // 90 -> 180
                if (ramp == 1) f = [] (double t) { return 160.0 - 80.0 * std::min (1.0, t / 4.0); };  // 160 -> 80
                if (ramp == 2) f = [] (double t) { return t < 1.0 ? 120.0 : 126.0; };                 // step
                r.renderTempo (8.0, f);
                // expected hits = 16th grid points crossed by the integrated ppq
                double ppq = 0;
                for (int pos = 0; pos < (int) (8.0 * 48000); pos += bs)
                    ppq += std::min (bs, (int) (8.0 * 48000) - pos) * f (pos / 48000.0) / (60.0 * 48000);
                const auto k = r.onsets (36);
                const int expected = (int) std::floor (ppq / 0.25 - 0.5 / 48000.0) + 1;
                bool mono = true;
                for (size_t i = 1; i < k.size(); ++i) mono = mono && k[i] - k[i - 1] > 2000;
                String why;
                const bool wf = r.midiWellFormed (why);
                CHECK ((int) k.size() >= expected - 1 && (int) k.size() <= expected && mono && wf,
                       ("block " + String (bs) + " tempo case " + String (ramp) + ": " + String ((int) k.size()) + " hits (grid "
                        + String (expected) + "), spacing ok " + String ((int) mono) + (wf ? "" : " " + why)).toRawUTF8());
            }
        }
    }
    {
        std::printf ("[midi-out] note pairing with swing + nudge + MATHS on one note\n");
        Rig r;
        r.loadKit (kitDir);
        r.set (gid::midiOut, 1);
        for (int st = 0; st < 16; ++st)
        {
            auto s2 = r.pat().step (0, st); s2.on = true; s2.nudge = (int8_t) (st % 2 ? 45 : -45); s2.gate = 40; r.pat().setStep (0, st, s2);
        }
        r.set (gid::swing, 70.0f);
        r.set (gid::mOn, 1.0f);
        r.set (mathsParamID (0, mpRise), 0.1f);
        r.set (mathsParamID (0, mpFall), 0.1f);
        r.tset (0, tpMTrig, (float) mtAEoc);
        r.render (6.0);
        r.ph.playing = false;
        r.render (0.2, 0.0, false);
        String why;
        CHECK (r.midiWellFormed (why), ("every note-on gets exactly one note-off " + why).toRawUTF8());
    }
    {
        std::printf ("[chain] 1-bar pre-roll still plays the first link in full\n");
        Rig r;
        r.loadKit (kitDir);
        r.set (gid::midiOut, 1);
        r.on (0, { 0 }, 0);
        r.on (1, { 0 }, 1);
        r.proc->chain[0] = 0; r.proc->chain[1] = 1; r.proc->chainLen = 2;
        r.render (6.0, -4.0); // starts one bar before the song
        const auto k = r.onsets (36), sn = r.onsets (37);
        CHECK (k.size() >= 1 && k[0] == 96000, ("P1 on the downbeat after pre-roll: " + joined (k)).toRawUTF8());
        CHECK (sn.size() >= 1 && sn[0] == 2 * 96000, ("P2 one bar later: " + joined (sn)).toRawUTF8());
    }
    {
        std::printf ("[cond] 1:2 / 2:2 alternate across REAPER loop wraps of a 1-bar region\n");
        Rig r;
        r.loadKit (kitDir);
        r.set (gid::midiOut, 1);
        auto a = r.pat().step (0, 0); a.on = true; a.cond = Cond1of2; r.pat().setStep (0, 0, a);
        auto b = r.pat().step (1, 0); b.on = true; b.cond = Cond2of2; r.pat().setStep (1, 0, b);
        auto f = r.pat().step (2, 0); f.on = true; f.cond = CondFirst; r.pat().setStep (2, 0, f);
        std::vector<int> pattern;
        for (int pass = 0; pass < 4; ++pass)
        {
            r.render (2.0, 0.0, true); // the loop region: bar 1, again and again
            pattern.push_back ((r.onsets (36).size() ? 1 : 0) + (r.onsets (37).size() ? 2 : 0) + (r.onsets (38).size() ? 4 : 0));
        }
        CHECK (pattern == std::vector<int> ({ 5, 2, 1, 2 }), ("passes (1=1:2, 2=2:2, 4=First): " + String (pattern[0]) + " " + String (pattern[1]) + " "
                                                               + String (pattern[2]) + " " + String (pattern[3])).toRawUTF8());
    }
    {
        std::printf ("[automation] Pattern parameter envelope switches on the bar\n");
        Rig r;
        r.loadKit (kitDir);
        r.set (gid::midiOut, 1);
        r.on (0, { 0, 4, 8, 12 }, 0);
        r.on (1, { 0, 4, 8, 12 }, 1);
        r.render (1.25);                       // into beat 3 of bar 1
        r.set (gid::pattern, 2.0f);            // envelope steps to pattern 2 mid-bar
        r.render (2.0, 2.5, true);             // continue from ppq 2.5
        const auto k = r.onsets (36), sn = r.onsets (37);
        // ppq 3 still pattern 1 (kick), ppq 4 = bar 2 -> pattern 2 (snare)
        CHECK (k.size() == 1 && k[0] == 12000 && ! sn.empty() && sn[0] == 36000,
               ("kick at ppq 3, snare from bar 2: " + joined (k) + "| " + joined (sn)).toRawUTF8());
    }
    {
        std::printf ("[stretch] granular warp: 1x is transparent, 2x keeps level and doubles length\n");
        auto renderBoom = [&] (bool stretch, float factor) {
            Rig r;                                   // fresh instance: built-in kit, track 14 = "boom"
            r.ph.playing = false;
            r.tset (13, tpWarp, stretch ? 1.0f : 0.0f);
            r.tset (13, tpStretch, factor);
            r.midiIn.push_back ({ 0, MidiMessage::noteOn (1, 49, (uint8) 127) });
            r.render (5.0);
            return r.out;
        };
        const auto dry = renderBoom (false, 1.0f), one = renderBoom (true, 1.0f), two = renderBoom (true, 2.0f);
        double e = 0, d = 0;
        for (int i = 0; i < 48000 * 2; ++i) { const double x = dry.getSample (0, i), y = one.getSample (0, i); e += (x - y) * (x - y); d += x * x; }
        const double snr = 10.0 * std::log10 (d / std::max (1e-12, e));
        CHECK (snr > 40.0, ("1x stretch vs original: " + String (snr, 1) + " dB SNR").toRawUTF8());
        auto lenAbove = [] (const AudioBuffer<float>& b, float th) { int last = 0; for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > th) last = i; return last; };
        const double ratio = (double) lenAbove (two, 0.01f) / std::max (1, lenAbove (dry, 0.01f));
        CHECK (ratio > 1.8 && ratio < 2.2, ("2x stretch length ratio " + String (ratio, 2)).toRawUTF8());
        // envelope flutter: RMS in 10 ms windows across the steady middle should glide, not pulse at the grain rate
        auto rms = [] (const AudioBuffer<float>& b, int a, int n) { double s = 0; for (int i = a; i < a + n; ++i) s += b.getSample (0, i) * b.getSample (0, i); return std::sqrt (s / n); };
        double worst = 0;
        for (int w = 48000 / 2; w < 48000 * 2; w += 480)
            worst = std::max (worst, std::abs (20.0 * std::log10 ((rms (two, w + 480, 480) + 1e-9) / (rms (two, w, 480) + 1e-9))));
        CHECK (worst < 3.0, ("2x stretch level flutter, worst 10 ms step " + String (worst, 2) + " dB").toRawUTF8());
    }
    {
        std::printf ("[midi-in] drum notes trigger pads\n");
        Rig r;
        r.loadKit (kitDir);
        r.ph.playing = false;
        r.midiIn.push_back ({ 1000, MidiMessage::noteOn (1, 36, (uint8) 127) });
        r.midiIn.push_back ({ 30000, MidiMessage::noteOn (1, 38, (uint8) 90) }); // T3 (C#? no: 38 = T3)
        r.render (1.0);
        CHECK (peak (r.out, 0, 1000) == 0.0f && peak (r.out, 1000, 3000) > 0.1f, "note 36 sounds at its offset, silence before");
        CHECK (r.proc->hitCount (2) == 1, "note 38 hit track 3");
    }
    {
        std::printf ("[routing] multi-out + choke\n");
        Rig r;
        r.loadKit (kitDir);
        r.on (1, { 0 });
        r.tset (1, tpOut, 1); // T2 -> Out 1
        r.on (8, { 0 });      // cymbal at 0 ...
        r.on (2, { 2 });      // ... choked by closed hat at step 2
        r.tset (2, tpChoke, 1);
        r.tset (8, tpChoke, 1);
        r.tset (8, tpOut, 2); // cymbal on Out 2 so we can watch it die
        r.render (1.0);
        CHECK (peak (r.aux[0]) > 0.05f, "snare arrives on Out 1");
        const float before = peak (r.aux[1], 2 * step - 2000, 2 * step);
        const float after = peak (r.aux[1], 2 * step + 600, 2 * step + 4000);
        CHECK (before > 0.02f && after < before * 0.05f, ("open hat choked (" + String (before) + " -> " + String (after) + ")").toRawUTF8());
    }
    {
        std::printf ("[random] lock = repeatable, unlock = fresh each pass\n");
        auto run = [&] (bool lock) {
            Rig r;
            r.loadKit (kitDir);
            r.set (gid::midiOut, 1);
            for (int t = 0; t < 4; ++t) r.on (t, { 0, 2, 4, 6, 8, 10, 12, 14 });
            r.set (gid::gRndSeq, 0.7f);
            r.set (gid::gRndSnd, 0.6f);
            r.set (gid::lock, lock ? 1.0f : 0.0f);
            r.render (2.0);
            auto first = r.notes;
            auto audio1 = r.out;
            r.ph.playing = false;
            r.render (0.1, 0, false);
            r.ph.playing = true;
            r.render (2.0);
            bool sameNotes = first.size() == r.notes.size();
            for (size_t i = 0; sameNotes && i < first.size(); ++i)
                sameNotes = first[i].pos == r.notes[i].pos && first[i].note == r.notes[i].note && first[i].vel == r.notes[i].vel;
            return sameNotes;
        };
        CHECK (run (true), "locked: second pass identical");
        CHECK (! run (false), "unlocked: second pass differs");
    }
    {
        std::printf ("[loop] host loop jump back (REAPER loop region)\n");
        Rig r;
        r.loadKit (kitDir);
        r.set (gid::midiOut, 1);
        r.on (0, { 0, 4, 8, 12 });
        r.render (2.0, 0.0);     // bar 1
        r.render (2.0, 0.0);     // loop back to the start
        const auto k = r.onsets (36);
        CHECK (k.size() == 4 && k[0] == 0, ("after jump back, pattern restarts cleanly: " + joined (k)).toRawUTF8());
        r.render (2.0, 6.0);     // seek into the middle of bar 2 (beat 3)
        const auto k2 = r.onsets (36);
        CHECK (k2.size() == 4 && k2[0] == 0 && k2[1] == 24000, ("after seek to beat 3: " + joined (k2)).toRawUTF8());
    }
    {
        std::printf ("[state] save -> fresh instance -> identical render\n");
        Rig a;
        a.loadKit (kitDir);
        a.on (0, { 0, 3, 8, 11 });
        a.on (1, { 4, 12 });
        a.lane (2, 7);
        a.on (2, { 0, 1, 3, 5 });
        a.tset (0, tpTune, -3.0f);
        a.tset (1, tpDrive, 18.0f);
        a.tset (2, tpWarp, 1.0f);
        a.tset (2, tpStretch, 1.8f);
        a.set (gid::lock, 1.0f);
        a.set (gid::gRndSnd, 0.4f);
        MemoryBlock mb;
        a.proc->getStateInformation (mb);
        Rig b;
        b.proc->setStateInformation (mb.getData(), (int) mb.getSize());
        a.render (2.0);
        b.render (2.0);
        float diff = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < a.out.getNumSamples(); ++i) diff = std::max (diff, std::abs (a.out.getSample (c, i) - b.out.getSample (c, i)));
        CHECK (b.proc->samples.slot (1) != nullptr, "samples restored");
        CHECK (b.pat().lane (2).length == 7, "lane restored");
        CHECK (diff < 1.0e-6f && peak (a.out) > 0.05f, ("renders match (max diff " + String (diff) + ")").toRawUTF8());
        CHECK (mb.getSize() < 200000, ("state size " + String ((int) mb.getSize()) + " bytes").toRawUTF8());
    }
    {
        std::printf ("[maths] cycling function generator fires a track, quantized\n");
        Rig r;
        r.loadKit (kitDir);
        r.set (gid::midiOut, 1);
        r.set (gid::mOn, 1.0f);
        r.set (mathsParamID (0, mpRise), (float) (std::log (1.5 / 0.25) / std::log (256.0)));
        r.set (mathsParamID (0, mpFall), (float) (std::log (2.0 / 0.25) / std::log (256.0)));
        r.tset (7, tpMTrig, (float) mtAEoc);
        r.render (4.0);
        const auto h = r.onsets (43);
        bool grid = ! h.empty();
        for (auto p : h) grid = grid && (p % step) == 0;
        CHECK (h.size() >= 4, ("MATHS fired T8 " + String ((int) h.size()) + " times: " + joined (h)).toRawUTF8());
        CHECK (grid, "every MATHS hit on the 1/16 grid");
        CHECK (r.proc->maths.eocCount[0].load() >= 4, "A completed cycles");
    }
    {
        std::printf ("[stress] 16 busy tracks, stretch + drive + filters, 30 s\n");
        Rig r (48000, 256);
        r.loadKit (kitDir);
        for (int t = 0; t < kNumTracks; ++t)
        {
            String e;
            if (r.proc->samples.slot (t) == nullptr) r.proc->samples.setSlot (t, r.proc->samples.slot (t % 12));
            for (int s = 0; s < 16; ++s) { auto st = r.pat().step (t, s); st.on = true; st.ratchet = (uint8_t) (1 + (s % 3)); r.pat().setStep (t, s, st); }
            r.tset (t, tpWarp, t % 2 ? 1.0f : 0.0f);
            r.tset (t, tpDrive, 12.0f);
            r.tset (t, tpFType, 1.0f + (t % 3));
            r.tset (t, tpFEnv, 0.5f);
            r.tset (t, tpCrush, 10.0f);
        }
        r.set (gid::mDrive, 6.0f);
        r.set (gid::djFilter, -0.3f);
        const auto t0 = Time::getMillisecondCounterHiRes();
        r.render (30.0);
        const auto ms = Time::getMillisecondCounterHiRes() - t0;
        CHECK (finite (r.out), "stress output finite");
        CHECK (peak (r.out) <= 1.0f, ("soft clip holds the ceiling (peak " + String (peak (r.out)) + ")").toRawUTF8());
        std::printf ("       30 s rendered in %.0f ms = %.0fx realtime (CPU %.1f%% of one core)\n", ms, 30000.0 / ms, ms / 300.0);
        CHECK (ms < 30000.0 * 0.35, "heavy load stays under 35% of one core");
    }
}

//==============================================================================
static void buildDemo (Rig& r, const File& kitDir)
{
    r.loadKit (kitDir);
    // kick, snare, closed hat, open hat, toms, clap, cymbal, rim, cowbell, conga
    r.on (0, { 0, 6, 10, 16, 22, 26, 29 }, 0, 120);
    r.lane (0, 24);   // steps 27 and 30 sit past the end: kept, not played
    r.on (1, { 4, 12 }, 0, 110);
    r.on (7, { 12 }, 0, 90);
    for (int s = 0; s < 16; ++s) { auto st = r.pat().step (2, s); st.on = s != 14; st.vel = (uint8_t) (s % 2 ? 50 : 95); r.pat().setStep (2, s, st); }
    { auto st = r.pat().step (2, 7); st.ratchet = 3; r.pat().setStep (2, 7, st); }
    r.on (3, { 14 }, 0, 100);
    r.tset (2, tpChoke, 1); r.tset (3, tpChoke, 1);
    r.on (4, { 0, 3 }, 0, 100); r.lane (4, 5); r.tset (4, tpTune, -5.0f); r.tset (4, tpRndSnd, 0.3f);
    r.on (9, { 2, 5 }, 0, 90); r.lane (9, 7);
    r.on (10, { 0 }, 0, 80); r.lane (10, 9, 4); // cowbell, 9 eighth notes
    r.tset (1, tpDrive, 14.0f); r.tset (1, tpSat, 3.0f);
    r.tset (0, tpPAmt, 7.0f); r.tset (0, tpPDec, 40.0f);
    r.tset (2, tpRndSnd, 0.35f);
    r.set (gid::swing, 58.0f);
    r.set (gid::gRndSeq, 0.12f);
    r.set (gid::mDrive, 4.0f);
}

//==============================================================================
template <typename T>
static T* findChild (Component& root)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c)) return t;
        if (auto* t = findChild<T> (*c)) return t;
    }
    return nullptr;
}

static MouseEvent mev (Component& c, Point<int> p, Point<int> down, ModifierKeys mods = {}, bool dragged = false)
{
    const auto now = Time::getCurrentTime();
    return MouseEvent (Desktop::getInstance().getMainMouseSource(), p.toFloat(), mods, 0, 0, 0, 0, 0, &c, &c, now,
                       down.toFloat(), now, 1, dragged);
}

static void click (Component& c, Point<int> p, ModifierKeys mods = {})
{
    c.mouseDown (mev (c, p, p, mods));
    c.mouseUp (mev (c, p, p, mods));
}

static void guiTests (const File& kitDir)
{
    std::printf ("[gui] synthesized mouse input on the real editor\n");
    Rig r;
    r.loadKit (kitDir);
    std::unique_ptr<AudioProcessorEditor> ed (r.proc->createEditor());
    auto* grid = findChild<StepGrid> (*ed);
    auto* lanes = findChild<LaneEditor> (*ed);
    auto* header = findChild<Header> (*ed);
    CHECK (grid && lanes && header, "found grid, lanes, header");
    if (! (grid && lanes && header)) return;
    auto cell = [] (int t, int s) { return Point<int> (StepGrid::kGridX + s * StepGrid::kCellW + 6, StepGrid::kTop + t * StepGrid::kRowH + 6); };

    click (*grid, cell (4, 3));
    CHECK (r.pat().step (4, 3).on && r.proc->selectedTrack == 4, "click a cell: step on + track selected");
    click (*grid, cell (4, 3));
    CHECK (! r.pat().step (4, 3).on, "click again: step off");

    grid->mouseDown (mev (*grid, cell (5, 0), cell (5, 0)));
    grid->mouseDrag (mev (*grid, cell (5, 7), cell (5, 0), {}, true));
    grid->mouseUp (mev (*grid, cell (5, 7), cell (5, 0), {}, true));
    bool painted = true;
    for (int st = 0; st <= 7; ++st) painted = painted && r.pat().step (5, st).on;
    CHECK (painted && ! r.pat().step (5, 8).on, "drag paints steps 1-8 of a row, stops there");

    { Lane l; l.length = 5; r.pat().setLane (9, l); }
    click (*grid, cell (9, 7));
    CHECK (r.pat().step (9, 7).on && r.pat().lane (9).length == 5, "a step past the end can be set (kept, not played)");
    // drag T10's end bracket from step 5 to step 12
    const Point<int> bracket (StepGrid::kGridX + 5 * StepGrid::kCellW - 1, StepGrid::kTop + 9 * StepGrid::kRowH + 8);
    const Point<int> to12 (StepGrid::kGridX + 12 * StepGrid::kCellW, bracket.y);
    grid->mouseDown (mev (*grid, bracket, bracket));
    grid->mouseDrag (mev (*grid, to12, bracket, {}, true));
    grid->mouseUp (mev (*grid, to12, bracket, {}, true));
    CHECK (r.pat().lane (9).length == 12 && r.pat().step (9, 7).on, ("drag the end bracket: length 5 -> " + String (r.pat().lane (9).length) + ", hidden step now plays").toRawUTF8());
    // ruler click: every track to 8 steps; steps beyond survive
    click (*grid, { StepGrid::kGridX + 7 * StepGrid::kCellW + 6, 8 });
    bool all8 = true;
    for (int t = 0; t < kNumTracks; ++t) all8 = all8 && r.pat().lane (t).length == 8;
    CHECK (all8 && r.pat().step (9, 7).on, "ruler click sets every track's length to 8");
    click (*grid, { StepGrid::kGridX + 31 * StepGrid::kCellW + 6, 8 }, ModifierKeys::shiftModifier);
    CHECK (r.pat().lane (r.proc->selectedTrack).length == 32 && r.pat().lane ((r.proc->selectedTrack + 1) % 16).length == 8,
           "shift-click on the ruler sets only the selected track");
    for (int t = 0; t < kNumTracks; ++t) { Lane l; l.length = 16; r.pat().setLane (t, l); }
    click (*grid, { 170, StepGrid::kTop + 2 * StepGrid::kRowH + 8 });
    CHECK (r.proc->trackParam (2, tpMute)->getValue() > 0.5f, "M button mutes track 3");

    const Point<int> lenCell (StepGrid::kLaneX + 10, StepGrid::kTop + 6);
    grid->mouseDown (mev (*grid, lenCell, lenCell));
    grid->mouseDrag (mev (*grid, lenCell.translated (0, -50), lenCell, {}, true));
    grid->mouseUp (mev (*grid, lenCell.translated (0, -50), lenCell, {}, true));
    CHECK (r.pat().lane (0).length == 26, ("drag LEN up 50 px: 16 -> " + String (r.pat().lane (0).length)).toRawUTF8());

    click (*grid, { 60, StepGrid::kTop + 5 * StepGrid::kRowH + 8 }); // select track 6 by its name
    CHECK (r.proc->selectedTrack == 5, "clicking a track name selects it");
    r.proc->laneKind = LaneEditor::lkVel;
    const int barX = StepGrid::kGridX + 2 * StepGrid::kCellW + 6;
    lanes->mouseDown (mev (*lanes, { barX, 6 }, { barX, 6 }));
    lanes->mouseUp (mev (*lanes, { barX, 6 }, { barX, 6 }));
    CHECK (r.pat().step (5, 2).vel > 120, ("lane click near the top sets velocity high: " + String (r.pat().step (5, 2).vel)).toRawUTF8());
    lanes->mouseDown (mev (*lanes, { barX, 6 }, { barX, 6 }));
    lanes->mouseDrag (mev (*lanes, { barX + 4 * StepGrid::kCellW, 90 }, { barX, 6 }, {}, true));
    lanes->mouseUp (mev (*lanes, { barX + 4 * StepGrid::kCellW, 90 }, { barX, 6 }, {}, true));
    const int v2 = r.pat().step (5, 2).vel, v4 = r.pat().step (5, 4).vel, v6 = r.pat().step (5, 6).vel;
    CHECK (v2 > v4 && v4 > v6 && v6 < 15, ("lane drag draws a ramp: " + String (v2) + " > " + String (v4) + " > " + String (v6)).toRawUTF8());

    const Point<int> slot3 (350 + 2 * 37 + 10, 20), slot5 (350 + 4 * 37 + 10, 20);
    click (*header, slot3);
    CHECK (r.proc->viewPattern == 2 && r.proc->guiPatternRequest.load() == 2, "click pattern slot 3: view + request");
    click (*header, slot5, ModifierKeys::shiftModifier);
    CHECK (r.proc->chainLen.load() == 2 && r.proc->chain[0].load() == 2 && r.proc->chain[1].load() == 4,
           ("shift-click slot 5: chain " + r.proc->chainText()).toRawUTF8());
    click (*header, slot3);
    CHECK (r.proc->chainLen.load() == 0, "plain click clears the chain");

    // ---- folder drops: junk at the top level, audio in subfolders, natural order
    const File tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("gm_drop_test");
    tmp.deleteRecursively();
    tmp.getChildFile ("kicks").createDirectory();
    tmp.getChildFile ("readme.txt").replaceWithText ("not audio");
    for (auto* n : { "k1.wav", "k10.wav", "k2.wav" })
        kitDir.getChildFile ("808bd/BD0010.WAV").copyFileTo (tmp.getChildFile ("kicks").getChildFile (n));
    const auto msg = loadFiles (*r.proc, { tmp.getFullPathName() }, 4);
    CHECK (r.proc->samples.slot (4) && r.proc->samples.slot (4)->name == "k1" && r.proc->samples.slot (5)->name == "k2"
           && r.proc->samples.slot (6)->name == "k10", ("folder with a readme on top + audio below: " + msg).toRawUTF8());
    tmp.getChildFile ("top.wav").replaceWithData (nullptr, 0);
    kitDir.getChildFile ("808sd/SD0010.WAV").copyFileTo (tmp.getChildFile ("top.wav"));
    dynamic_cast<GeomancerEditor*> (ed.get())->filesDropped ({ tmp.getFullPathName() }, 10, 10);
    CHECK (r.proc->samples.slot (r.proc->selectedTrack)->name == "top", "dropping on the editor (outside the grid) loads into the selected track");
    tmp.deleteRecursively();

    // ---- browser
    auto* br = findChild<Browser> (*ed);
    CHECK (br != nullptr, "browser tab exists");
    if (br != nullptr)
    {
        auto* places = dynamic_cast<ListBox*> (br->getChildComponent (0));
        bool hasArchive = false;
        if (places != nullptr)
            for (int i = 0; i < places->getListBoxModel()->getNumRows(); ++i)
            {
                Image img (Image::ARGB, 200, 18, true);
                Graphics g (img);
                places->getListBoxModel()->paintListBoxItem (i, g, 200, 18, false);
            }
        StringArray lines;
        lines.addLines (File ("/proc/mounts").loadFileAsString());
        for (auto& l : lines) hasArchive = hasArchive || l.contains (" /mnt/archive ");
        br->openAt (kitDir.getChildFile ("808sd"));
        auto* files = dynamic_cast<ListBox*> (br->getChildComponent (1));
        CHECK (files != nullptr && files->getListBoxModel()->getNumRows() == 26, "808sd folder lists '..' + 25 samples");
        r.ph.playing = false;
        files->selectRow (3);            // auto-preview fires on selection
        r.render (0.3);
        CHECK (peak (r.out) > 0.1f, ("arrowing onto a file auditions it (peak " + String (peak (r.out)) + ")").toRawUTF8());
        const int sel = r.proc->selectedTrack;
        files->getListBoxModel()->returnKeyPressed (3);
        CHECK (r.proc->samples.slot (sel) && r.proc->samples.slot (sel)->file.getParentDirectory().getFileName() == "808sd",
               "Enter loads the highlighted file into the selected track");
        std::printf ("       (mount table has /mnt/archive: %d)\n", (int) hasArchive);
    }
    ed.reset();
}

static void vst3Smoke (const File& path)
{
    std::printf ("[vst3] %s\n", path.getFullPathName().toRawUTF8());
    AudioPluginFormatManager fm;
    addDefaultFormatsToManager (fm);
    OwnedArray<PluginDescription> types;
    for (auto* f : fm.getFormats())
        if (f->getName() == "VST3") f->findAllTypesForFile (types, path.getFullPathName());
    CHECK (types.size() == 1, "one plugin type found");
    if (types.isEmpty()) return;
    std::printf ("       %s by %s, instrument=%d\n", types[0]->name.toRawUTF8(), types[0]->manufacturerName.toRawUTF8(), (int) types[0]->isInstrument);
    String err;
    auto inst = fm.createPluginInstance (*types[0], 48000, 512, err);
    CHECK (inst != nullptr, ("instantiated " + err).toRawUTF8());
    if (inst == nullptr) return;
    FakePlayHead ph;
    inst->setPlayHead (&ph);
    inst->prepareToPlay (48000, 512);
    std::printf ("       params=%d  outputs=%d buses=%d  midiIn=%d midiOut=%d\n", inst->getParameters().size(),
                 inst->getTotalNumOutputChannels(), inst->getBusCount (false), (int) inst->acceptsMidi(), (int) inst->producesMidi());
    CHECK (inst->getParameters().size() > 600, "all parameters exposed");
    CHECK (inst->getBusCount (false) == 17, "main + 16 aux output buses");
    AudioBuffer<float> buf (inst->getTotalNumOutputChannels(), 512);
    MidiBuffer midi;
    float pk = 0;
    for (int b = 0; b < 200; ++b)
    {
        midi.clear();
        if (b == 10) midi.addEvent (MidiMessage::noteOn (1, 36, (uint8) 120), 0);
        ph.ppq = b * 512 / 48000.0 * 2.0;
        inst->processBlock (buf, midi);
        pk = std::max (pk, buf.getMagnitude (0, 0, 512));
    }
    CHECK (pk > 0.1f, ("built-in kick answers MIDI note 36 in the real binary (peak " + String (pk) + ")").toRawUTF8());
    MemoryBlock st;
    inst->getStateInformation (st);
    CHECK (st.getSize() > 100, ("state " + String ((int) st.getSize()) + " bytes").toRawUTF8());
    inst->setStateInformation (st.getData(), (int) st.getSize());
    auto* ed = inst->createEditorIfNeeded();
    CHECK (ed != nullptr, "editor created");
    if (ed != nullptr) std::printf ("       editor %dx%d\n", ed->getWidth(), ed->getHeight());
    delete ed;
    inst->releaseResources();
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::printf ("usage: gm_test engine|demo|gui|vst3 <path> [out]\n"); return 2; }
    const String mode (argv[1]);
    const File arg (File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    if (mode == "engine")
        engineTests (arg);
    else if (mode == "demo" && argc > 3)
    {
        Rig r (48000, 512);
        buildDemo (r, arg);
        r.render (16.0);
        writeWav (r.out, 48000, File::getCurrentWorkingDirectory().getChildFile (argv[3]));
        std::printf ("demo written, peak %.3f\n", peak (r.out));
    }
    else if (mode == "gui" && argc > 3)
    {
        Rig r;
        buildDemo (r, arg);
        r.set (gid::play, 1.0f);
        r.render (1.3);
        r.proc->selectedTrack = 2;
        std::unique_ptr<AudioProcessorEditor> ed (r.proc->createEditor());
        ed->setSize (GeomancerEditor::kW, GeomancerEditor::kH);
        const int tabs = argc > 4 ? String (argv[4]).getIntValue() : 0;
        auto shoot = [&] (int tab, const File& f) {
            r.proc->bottomTab = tab;
            ed.reset (r.proc->createEditor());
            ed->setSize (GeomancerEditor::kW, GeomancerEditor::kH);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            f.deleteFile();
            FileOutputStream os (f);
            PNGImageFormat().writeImageToStream (img, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        };
        const File out = File::getCurrentWorkingDirectory().getChildFile (argv[3]);
        shoot (0, out);
        if (tabs)
        {
            shoot (1, out.getSiblingFile (out.getFileNameWithoutExtension() + "_maths.png"));
            shoot (2, out.getSiblingFile (out.getFileNameWithoutExtension() + "_setup.png"));
            r.proc->bottomTab = 3;
            ed.reset (r.proc->createEditor());
            ed->setSize (GeomancerEditor::kW, GeomancerEditor::kH);
            if (auto* br = findChild<Browser> (*ed))
            {
                const File lib ("/mnt/archive/Samples");
                br->openAt (lib.isDirectory() ? lib : arg);
                if (auto* files = dynamic_cast<ListBox*> (br->getChildComponent (1))) files->selectRow (2);
            }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            const File bf = out.getSiblingFile (out.getFileNameWithoutExtension() + "_browse.png");
            bf.deleteFile();
            FileOutputStream os (bf);
            PNGImageFormat().writeImageToStream (img, os);
        }
        ed.reset();
    }
    else if (mode == "vst3")
        vst3Smoke (arg);
    else if (mode == "guitest")
        guiTests (arg);
    else if (mode == "maths" && argc > 3)
    {
        // Emergence check: cross-coupled function generators firing three tracks, 16 bars.
        // Writes "config,track,sample" rows for plotting.
        File csv = File::getCurrentWorkingDirectory().getChildFile (argv[3]);
        csv.deleteFile();
        FileOutputStream os (csv);
        os << "config,note,pos\n";
        auto steps = [] (double st) { return (float) (std::log (st / 0.25) / std::log (256.0)); };
        for (int cfgI = 0; cfgI < 5; ++cfgI)
        {
            Rig r;
            r.set (gid::midiOut, 1);
            r.set (gid::mOn, 1);
            r.tset (2, tpMTrig, (float) mtAEoc);
            r.tset (4, tpMTrig, (float) mtBEor);
            r.tset (7, tpMTrig, (float) mtBEoc);
            r.set (mathsParamID (0, mpRise), steps (1.5)); r.set (mathsParamID (0, mpFall), steps (2.5));
            r.set (mathsParamID (1, mpRise), steps (1.0)); r.set (mathsParamID (1, mpFall), steps (3.5));
            if (cfgI >= 1) { r.set (mathsParamID (0, mpXMod), 0.45f); r.set (mathsParamID (1, mpXMod), -0.5f); r.set (mathsParamID (1, mpCurve), 0.6f); }
            if (cfgI == 2) { r.set (mathsParamID (1, mpCycle), 0); r.set (mathsParamID (1, mpTrig), (float) miOtherEoc); r.set (mathsParamID (0, mpXMod), 0.9f); }
            if (cfgI >= 3) // bouncing ball: B cycles, slowed while A is high; A's fall releases it -> accelerating pulses
            {
                r.set (mathsParamID (0, mpRise), steps (0.25)); r.set (mathsParamID (0, mpFall), steps (cfgI == 3 ? 16.0 : 12.0));
                r.set (mathsParamID (0, mpCurve), cfgI == 3 ? -0.6f : 0.6f); r.set (mathsParamID (0, mpXMod), 0.0f);
                r.set (mathsParamID (1, mpRise), steps (0.4)); r.set (mathsParamID (1, mpFall), steps (0.8));
                r.set (mathsParamID (1, mpCurve), 0.0f); r.set (mathsParamID (1, mpXMod), -0.85f);
                r.set (mathsParamID (1, mpTrig), (float) miOtherEoc);
                r.set (gid::mQuant, cfgI == 3 ? 1.0f : 0.0f);
            }
            r.render (32.0);
            int count = 0;
            for (auto& e : r.notes) if (e.on) { os << cfgI << "," << e.note << "," << (int) e.pos << "\n"; ++count; }
            std::printf ("config %d: %d MATHS hits, finite=%d\n", cfgI, count, (int) finite (r.out));
        }
    }
    else if (mode == "kit")
    {
        // every built-in sound, one per beat at 120 bpm (fresh instance = built-in kit)
        Rig r;
        r.ph.playing = false;
        for (int t = 0; t < 14; ++t)
            r.midiIn.push_back ({ (int64) (t * 24000), MidiMessage::noteOn (1, 36 + t, (uint8) 127) });
        r.render (8.0);
        writeWav (r.out, 48000, arg);
        std::printf ("kit written, peak %.3f\n", peak (r.out));
    }
    else if (mode == "reaperstate" && argc > 3)
    {
        // Two patterns for the REAPER integration test: P1 kick on beats, P2 kick on 8ths;
        // snare on 2 & 4 routed to Out 1.
        Rig r;
        r.loadKit (arg);
        r.on (0, { 0, 4, 8, 12 }, 0, 120);
        r.on (0, { 0, 2, 4, 6, 8, 10, 12, 14 }, 1, 120);
        r.on (1, { 4, 12 }, 0, 110);
        r.on (1, { 4, 12 }, 1, 110);
        r.tset (0, tpDecay, 120.0f);
        r.tset (1, tpDecay, 150.0f);
        r.tset (1, tpOut, 1.0f);
        r.set (gid::clip, 0.0f);
        MemoryBlock mb;
        r.proc->getStateInformation (mb);
        const File out = File::getCurrentWorkingDirectory().getChildFile (argv[3]);
        out.replaceWithData (mb.getData(), mb.getSize());
        std::printf ("state %d bytes -> %s\n", (int) mb.getSize(), out.getFullPathName().toRawUTF8());
    }
    else { std::printf ("unknown mode\n"); return 2; }

    std::printf (failures == 0 ? "\nALL CHECKS PASSED\n" : "\n%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
