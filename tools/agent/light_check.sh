#!/usr/bin/env bash
# Compares our light engine with the original (run in Unicorn) on one map and camera.
# usage: tools/agent/light_check.sh <map> <cam x> <cam y> [main|floor|color ...]   (default: all three)
#        LIGHT_CFG="<light> <mapshadows> <shadows>" (default 5 4 50), DAYLIGHT=<sun angle>
# Rebuilds tools/agent/out/light_trace.exe; writes out/<map>_<part>_{ours,orig}.txt and out/<map>.scene.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"
export PATH=/c/msys64/mingw64/bin:$PATH
O=tools/agent/out; mkdir -p $O
MAP="$1"; CX="$2"; CY="$3"; shift 3
PARTS="${*:-main floor color}"
gcc -Iinclude tools/agent/light_trace.c $(ls src/*.c | grep -v main_client.c) -lSDL3 -lopengl32 -lgdi32 -lws2_32 \
    -l:libz.a -lm -o $O/light_trace.exe 2>&1 | grep -i error
N=$(basename "$MAP" .map); RC=0
for P in $PARTS; do
  $O/light_trace.exe "$MAP" "$CX" "$CY" "$P" "$O/$N.scene" ${LIGHT_CFG:-} > "$O/${N}_${P}_ours.txt"
  python -I tools/agent/emu_light.py CS2D.exe "$O/$N.scene" "$P" > "$O/${N}_${P}_orig.txt"
  echo "== $N $P ($CX, $CY)"
  python -I tools/agent/light_diff.py "$O/${N}_${P}_ours.txt" "$O/${N}_${P}_orig.txt" 3 || RC=1
done
exit $RC
