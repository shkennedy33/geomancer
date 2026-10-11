#!/usr/bin/env python3
"""Checks the REAPER integration render (see tools/reaper_test.sh).

Master.wav must hold only the kick: one per beat in bars 1-2, eighths in bars 3-4 (pattern 2,
selected by a MIDI note at bar 3), one per beat again from bar 5. 'Snare stem.wav' must hold the
snare on beats 2 and 4, arriving through Geomancer's Out 1 -> a REAPER send -> its own track.
"""
import sys
import numpy as np
import soundfile as sf

def onsets(path, thresh=0.05, refractory=0.08):
    x, sr = sf.read(path)
    x = np.abs(x).max(axis=1) if x.ndim > 1 else np.abs(x)
    hits, quiet_since = [], 0
    above = x > thresh
    last = -10**9
    for i in np.flatnonzero(above):
        if i - last > refractory * sr:
            hits.append(i)
        last = i
    return np.array(hits), sr, x

d = sys.argv[1] if len(sys.argv) > 1 else "build/reaper-test/render"
k, sr, xk = onsets(f"{d}/Master.wav")
s, _, xs = onsets(f"{d}/Snare stem.wav")
beat = sr * 0.5
exp_k = np.array(list(range(0, 8)) + [8 + 0.5 * i for i in range(16)] + list(range(16, 32))) * beat
exp_s = np.array([b * 4 + o for b in range(8) for o in (1, 3)]) * beat
ok = True
print(f"sr {sr}  master peak {xk.max():.3f}  snare-stem peak {xs.max():.3f}")
for name, got, exp in (("kick", k, exp_k), ("snare", s, exp_s)):
    if len(got) != len(exp):
        ok = False
        print(f"FAIL {name}: expected {len(exp)} onsets, got {len(got)}: {np.round(got / beat, 3).tolist()}")
        continue
    err = np.abs(got - exp).max()
    # threshold crossing lands a few samples after the true start of the sample's attack
    good = err <= 48
    ok &= bool(good)
    print(f"{'ok  ' if good else 'FAIL'} {name}: {len(got)} onsets, max timing error {int(err)} samples")
print("REAPER INTEGRATION OK" if ok else "REAPER INTEGRATION FAILED")
sys.exit(0 if ok else 1)
