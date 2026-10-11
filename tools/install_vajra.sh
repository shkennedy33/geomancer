#!/usr/bin/env bash
# Copy the built VST3 to the laptop (vajra) over tailscale. REAPER there picks it up on next launch.
set -euo pipefail
SRC="$HOME/.vst3/Geomancer.vst3"
HOST="${1:-vajra}"
ssh -o BatchMode=yes -o ConnectTimeout=8 "$HOST" 'mkdir -p ~/.vst3'
rsync -a --delete "$SRC/" "$HOST:.vst3/Geomancer.vst3/"
ssh -o BatchMode=yes "$HOST" 'ldd ~/.vst3/Geomancer.vst3/Contents/x86_64-linux/Geomancer.so | grep -c "not found" || true'
echo "installed on $HOST (0 above = no missing libraries)"
