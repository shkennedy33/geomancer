#!/usr/bin/env bash
# End-to-end REAPER integration test (headless, throwaway REAPER config; never touches ~/.config/REAPER).
#  1. gm_test writes a Geomancer state (kit + 2 patterns, snare routed to Out 1)
#  2. REAPER runs tools/reaper_build.lua: inserts the VST3, injects that state, adds pattern-select
#     MIDI notes, routes plugin outputs 3/4 to a "Snare stem" track, saves geomancer_test.rpp
#  3. REAPER renders the project offline (master + stem)
#  4. tools/analyze_reaper.py checks onset counts + timing in both renders
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$ROOT/build/reaper-test"
mkdir -p "$T/cfg"
cd "$ROOT"

./build/gm_test_artefacts/Release/gm_test reaperstate samples/dirt "$T/state.bin" | head -1
python3 - "$T" <<'EOF'
import base64, struct, sys
t = sys.argv[1]
comp = open(f"{t}/state.bin", "rb").read()
chunk = struct.pack("<II", len(comp), 1) + comp + b"\0" * 8   # REAPER's vst_chunk layout for VST3
open(f"{t}/state.b64", "w").write(base64.b64encode(chunk).decode())
EOF

rm -f "$T/build.log" "$T/geomancer_test.rpp"
rm -rf "$T/render"
xvfb-run -a -s "-screen 0 1600x900x24" /usr/lib/REAPER/reaper -cfgfile "$T/cfg/reaper.ini" -nosplash -new \
    -ignoreerrors "$ROOT/tools/reaper_build.lua" </dev/null >/dev/null 2>&1 &
XPID=$!
for _ in $(seq 1 60); do
    grep -q saved "$T/build.log" 2>/dev/null && break
    sleep 0.5
done
sleep 0.5
# xvfb-run's child is REAPER; kill REAPER by its parent PID (never pkill -f: it matches this shell)
for p in $(pgrep -P "$XPID" 2>/dev/null); do pkill -P "$p" 2>/dev/null || true; kill "$p" 2>/dev/null || true; done
kill "$XPID" 2>/dev/null || true
wait "$XPID" 2>/dev/null || true
cat "$T/build.log"
[ -f "$T/geomancer_test.rpp" ] || { echo "project was not built"; exit 1; }

timeout 120 xvfb-run -a -s "-screen 0 1600x900x24" /usr/lib/REAPER/reaper -cfgfile "$T/cfg/reaper.ini" -nosplash \
    -ignoreerrors -renderproject "$T/geomancer_test.rpp" </dev/null >/dev/null 2>&1
ls "$T/render"
"$ROOT/.venv/bin/python" "$ROOT/tools/analyze_reaper.py" "$T/render"
