#!/usr/bin/env bash
# Records the ORIGINAL client (CS2D.exe) against OUR dedicated server (cs2d_server.exe) through a UDP proxy:
# the server-side counterpart of capture_original.sh (same steps, same output names with "srv_").
#
# usage: tools/agent/capture_server.sh <tag> "<drive_orig.ps1 steps>" [map] [seconds]
# Output (tools/agent/out/): srv_<tag>.log (our server's console), proxy_srv_<tag>.log (every datagram).
# Compare with the original: python -I tools/agent/dumpops.py tools/agent/out/proxy_srv_<tag>.log 229,247,249,1
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tools/agent/out"
mkdir -p "$OUT"
TAG="$1"; STEPS="$2"; MAP="${3:-prison_funst3r}"; T="${4:-60}"
cd "$ROOT"
cp sys/recentlyjoined.lst "$OUT/recentlyjoined.lst.bak"
taskkill //F //IM cs2d_server.exe >/dev/null 2>&1; taskkill //F //IM CS2D.exe >/dev/null 2>&1
(timeout $((T + 10)) ./cs2d_server.exe -offline -map "$MAP" -port 36970 > "$OUT/srv_$TAG.log" 2>&1 &)
sleep 2
(python -I tools/agent/udp_proxy.py 36980 36970 "$OUT/proxy_srv_$TAG.log" "$T" 2>/dev/null &)
sleep 1
(./CS2D.exe -join 127.0.0.1:36980 -nointro -offline > /dev/null 2>&1 &)
sleep 14
powershell -NoProfile -ExecutionPolicy Bypass -File tools/agent/drive_orig.ps1 \
    -Steps "focus;wait:500;tap:2;wait:800;tap:1;wait:3000;focus;$STEPS" | tail -1
sleep 1
taskkill //F //IM CS2D.exe >/dev/null 2>&1; sleep 1
taskkill //F //IM cs2d_server.exe >/dev/null 2>&1
cp "$OUT/recentlyjoined.lst.bak" sys/recentlyjoined.lst
cat "$OUT/srv_$TAG.log"
