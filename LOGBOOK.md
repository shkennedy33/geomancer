# LOGBOOK — Geomancer (VST drum sampler + sequencer)

Situational awareness for future sessions. Newest entries at the bottom.

## Mission (from Sequoyah, 2026-10-02)
VST drum sampler + sequencer for REAPER (Linux, gandiva). Multi-sample loading, transform/warp,
DrumBrute / Beatstep Pro style sequencing (patterns, per-track length/division, gates, ratchets,
probability...). Plays nice with REAPER MIDI + audio recording. Per-track + global filter and
saturation. Per-track + global randomizer knobs. Stretch: MATHS-style emergent rhythm (cross-coupled
function generators).

## Layout
- `external/JUCE` — JUCE 9.0.3 (git clone --depth 1, 2026-09-28 commit). Not ours; don't edit.
- `samples/dirt` — sparse clone of tidalcycles/Dirt-Samples (808/909 kits etc.) for TEST ONLY.
- `Source/` — plugin code. `tools/` — offline test harness. `build/` — cmake build dir.

## Log
### 2026-10-02 — session 1
- Machine check: cmake 4.4, gcc 16, ninja, REAPER 7.78 at /usr/bin/reaper, Xvfb + imagemagick
  present. No JUCE was installed → cloned JUCE 9.0.3 into external/. JUCE 9 has NO native CLAP
  (formats: AAX AU AUv3 LV2 Standalone Unity VST3). VST3 is the REAPER target.
- Named it "Geomancer": geomancy = divination from random marks → 16 binary 4-row figures
  (16 figures × 4 rows = 64 bits = 64 steps). It doubles as a pattern generator easter egg.
- Architecture (Source/): DSP.h (SVF, ADAA saturators, crusher, hash RNG), Pattern (64-bit packed
  atomic steps; 16 patterns x 16 tracks x 64 steps; generators incl. euclid + geomancy), Params
  (~630 APVTS params; IDs "t<N>_<suffix>" are FROZEN — REAPER automation depends on them),
  Sample (refcounted pool; audio thread never frees), Voice (repitch + granular stretch, AHD env,
  pitch env, per-voice SVF), Track (voice alloc, choke, per-hit random, bus crush/sat/level/pan),
  Sequencer (STATELESS per step: time = patStart + n*stepLen; membership decided on rounded sample
  positions), Maths (2 cross-coupled function gens), PluginProcessor (transport, MIDI, routing),
  gui/ (Header, StepGrid, LaneEditor, SoundPanel+Waveform, MathsPanel+SetupPanel).
- Build: `cmake -B build -G Ninja && ninja -C build Geomancer_VST3 Geomancer_Standalone gm_test`.
  VST3 auto-copies to ~/.vst3/Geomancer.vst3. LTO makes plugin links slow (~2-4 min).
- Tests: `build/gm_test_artefacts/Release/gm_test engine samples/dirt` (30 checks: timing,
  polymeter, ratchet, swing, pattern switch, choke, multi-out, lock determinism, loop/seek,
  state round trip, maths, stress). `xvfb-run -a gm_test gui samples/dirt shots/ui.png 1` →
  editor PNGs. `gm_test vst3 ~/.vst3/Geomancer.vst3` smoke-tests the binary.
  pluginval 1.0.4 in external/pluginval: strictness 8 PASSES.
- Bug found+fixed: step offsets were truncated (71999 vs 72000) → round in sample domain.
- JUCE VST3 adds 2080 hidden MIDI-CC params by default → JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
  (consequence: VST3 program-change pattern select won't arrive; notes 0-15 do).
- Perf: 16 busy tracks (ratchets, stretch, drive, filters) = ~12% of one core @48k.
- REAPER e2e: `tools/reaper_test.sh` (8 s): throwaway -cfgfile under build/reaper-test/cfg, Lua
  (tools/reaper_build.lua) inserts plugin + injects state via vst_chunk, pattern-select MIDI notes,
  Out1→send→"Snare stem" track; `-renderproject`; tools/analyze_reaper.py → PASS (kick 40 onsets
  ≤9 smp, snare stem 16 ≤6 smp). REAPER 7.81 sees "VST3i: Geomancer (Sequoyah)", 2/34 out pins.
  Wiki: recipes/headless-reaper-plugin-test.md, projects/geomancer.md.
- Chain mode: shift-click pattern slots builds proc.chain (atomics, saved as "chain" attr);
  Sequencer advances at Pattern::lengthPpq (longest non-empty track, rounded up to bars).
  Any explicit pattern request (click/MIDI/param) clears the chain. Test [chain] passes.
- Built-in synth kit (Source/DrumSynth.cpp): 14 sounds rendered from code; fresh instances load it
  (tracks 1-14, hats choke group A by default). State stores "synth:<name>" sources.
  Prev/next on a synth sample cycles the built-in kit. Setup tab: BUILT-IN KIT button.
- Venv `.venv` (numpy, soundfile, matplotlib) for render analysis. renders/ holds demo wavs.
- Looper (beat repeat): param "looper" (Off,1/32..Bar). Sequencer::emit maps real time to pattern time
  loopStart + ((t-loopStart) mod L), one generate() per repeat with a `shift`. Header "Loop" chip.
- Reviewer agent audit (9 findings) — all fixed + regression-tested:
  1 HIGH tempo change looked like a seek (patStart reset, steps dropped/doubled). Now continuity =
    host sample clock (timeInSamples == last + n), fallback |Δppq| ≤ half block; block owns pattern
    time [frontier, p1 - 0.5·pps), frontier carried over → no gaps/doubles under ramps.
  2 HIGH MIDI-out note pairing assumed time order → note-ons collected per block, insertion-sorted.
  3 chain skipped link 1 in pre-roll → max(0, k). 4 bus saturation clicked (instant on/off, DC tail
    cut) → wet crossfades over first 3 dB of drive, busWarm while DC blocker rings. 5 stretch-mode
    grains cut hard at markers → 64-smp fade. 6 trig conditions counted from song start → cycleOffset
    per track: 0 at play start, continues across loop wraps, resets on quantized switch. 7 MIDI-out
    off left notes hanging → flushNoteOffs(n, !wantMidiOut). 8 midi.swapWith shrank our buffer →
    copy instead; skip SysEx before getMessage(). 9 DJ filter clicked engaging → runs warm as 20 kHz LP.
- Test count now 47 engine checks (+ tempo ramps/steps @512/2048, MIDI well-formedness, pre-roll
  chain, condition across loop wraps). Rig derives host sample time from ppq like REAPER does.
- GUI interaction tests: `xvfb-run -a gm_test guitest samples/dirt` synthesizes MouseEvents on the
  real editor (cell toggle, drag paint, M, LEN drag, lane click/ramp, pattern click, shift chain).
- `gm_test maths x renders/maths.csv` + plot → shots/maths_emergence.png, maths_ball.png. Findings:
  cross-coupling makes clustered non-repeating rhythms; B re-triggered by A's EOC mid-fall never
  completes (correct MATHS behaviour). The old "bouncing ball" recipe was wrong; the working version
  is A rise .25st fall 16st curve -0.6 cycling; B cycling rise .4 fall .8, X-MOD -0.85, trig = Other cycle,
  quantize on → B pulses accelerate through each bar.
- `~/.local/bin/geomancer` → standalone binary. `tools/install_vajra.sh` (vajra offline 2026-10-02;
  wiki thread open). README.md written.
- Checked: REAPER's libSwell.so calls gdk_set_allowed_backends + uses X11 atoms (XdndStatus,
  _NET_ACTIVE_WINDOW), i.e. REAPER pins itself to X11/XWayland → JUCE plugin GUIs embed fine under
  KDE/Hyprland Wayland even without vajra's GDK_BACKEND=x11 wrapper.
- Granular stretch: added WSOLA grain alignment (Voice::alignGrain: ±6 ms normalized-xcorr search
  against the fading grain's continuation). 808 boom @2x: flutter 11 dB → 2.5 dB; 1x still 43.7 dB
  SNR vs repitch. Test [stretch] guards it. Stress CPU ~11% → ~14% of one core.
- GUI reviewer agent (2 HIGH crash paths + 9 more) — all fixed:
  FileChoosers are now unique_ptr members of the launching component (StepGrid/SoundPanel/SetupPanel;
  destroying it cancels the zenity/kdialog child) + SafePointer guards; Euclid CallOutBox parented to
  the editor (dies with it) and edits the pattern captured at open; step/track/div/copy menus capture
  their pattern index; missingSample/lastSampleDir behind a SpinLock (missing()/setMissing()/
  sampleDir()); resyncPatternParam atomic flag after setState; undo stacks cleared on restore;
  editor timer re-syncs SoundPanel when selectedTrack changes (never mid knob-drag); Chip drag = one
  gesture; Waveform keeps its dragParam; grid ignores clicks past track length; markDirty once on
  mouse-up; StepGrid paints only dirty rows; Header repaints slots/meter regions only; laneKind/tab
  clamped on restore; Euclid rotate range ≥1.
- Recipe 3 "Bouncing ball" rewritten to the verified settings (see maths_ball.png).

### 2026-10-11 — session 2 (Sequoyah's first hands-on feedback)
- Feedback: (1) unclear how to change pattern length; wants to write long and keep steps past the end
  "for posterity"; (2) folder drag-and-drop didn't work; (3) native file picker showed only the OS drive
  (no /mnt), wants preview inside the importer. His library: /mnt/archive/Samples (808 Subs, Cymbals,
  Drum Machines, FX, HiHat, Instruments, Kick, Percussion, Snare...).
- kMaxSteps 64 → 128 (4 pages of 32). Old states load fine (bank format stores step count).
- Length UX: per-row draggable end bracket (Kind kEnd), ruler click = all tracks' length (shift = selected
  track), selected track's length highlighted in the ruler. Steps past the end are editable, drawn as
  hollow outlines, lane editor edits them dimmed. Generators (writeBits) no longer clear steps ≥ n.
- Folder drop bug: recursion only happened if the folder had NO files at all → any readme/.asd at the top
  meant nothing loaded. Shared collectAudio()/loadFiles() in StepGrid.cpp: recurse when no AUDIO at top,
  natural sort. Editor itself is a FileDragAndDropTarget (drop anywhere → selected track); Waveform too.
- New BROWSE tab (gui/Browser.{h,cpp}): places = Home, /, mount points parsed from /proc/mounts (NOT by
  stat-ing /mnt/* — /mnt/laptop is a CIFS autofs that hangs when daemon is offline; labelled "network"),
  + favourites; path box (type a path + Enter), filter, natural-sorted folders/audio, auto-preview on
  select, Enter/double-click loads, drag "gmfile:<path>" onto grid rows/waveform (editor is a
  DragAndDropContainer), FOLDER → KIT. Settings in ~/.config/Geomancer/Geomancer.settings (PropertiesFile).
  LOAD button / double-click name / track-menu Load now open the browser instead of the native chooser.
- Preview engine: SamplePool slot kPreviewSlot (= 16, GC covers it); processor.preview(file) loads ≤20 s,
  bumps previewTrigger; audio thread alternates 2 Voices (4 ms fade on the old one), renders after master
  FX straight to main out. Leaving the BROWSE tab stops it.
- Tests: guitest now covers hidden steps, bracket drag, ruler lengths, folder-with-readme drop, editor
  drop, browser listing/audition/Enter-load. All suites + REAPER e2e + pluginval 8 green.
- Git: repo initialised at /mnt/fast/projects/VST (branch main) → private github.com/shkennedy33/geomancer.
  JUCE is a submodule (external/JUCE @ be29c81, 9.0.3). Ignored: build/, .venv/, renders/, samples/dirt/,
  external/pluginval/, and aeromancer/ + aero-stable/ (a separate plugin with its own repo).
- 2026-10-11: CI (.github/workflows/build.yml) builds Windows (MSVC), macOS (universal, + AU) and Linux
  (ubuntu-22.04 for old glibc) zips; tag v* → GitHub Release. All 3 built first try; the Ubuntu-built
  Linux VST3 passes `gm_test vst3` on gandiva. CMake options GEOMANCER_TESTS / GEOMANCER_COPY_AFTER_BUILD.
  Browser lists findFileSystemRoots + /Volumes on non-Linux. README rewritten noob-first (install table,
  macOS xattr -cr / SmartScreen notes, "first five minutes"); dev stuff moved to the bottom.
  Builds are unsigned (no Apple notarization / Windows code signing).
