#!/usr/bin/env bash
# Records the ORIGINAL client (CS2D.exe) against the ORIGINAL dedicated server through a UDP proxy.
#
# usage: tools/agent/capture_original.sh <lua file in tools/agent/lua> <tag> "<drive_orig.ps1 steps>" [map] [seconds]
#   e.g. tools/agent/capture_original.sh break_probe.lua brk "mouse:525:240;click:5:250" prison_funst3r 60
#
# Output (tools/agent/out/): ded_<tag>.log (server console + Lua prints), proxy_<tag>.log (every datagram).
# Decode with: python -I tools/agent/dumpops.py tools/agent/out/proxy_<tag>.log 229,247,249,1
#
# The driver first joins a team ("tap:2" = CT, "tap:1" = class) and waits for the spawn; the steps
# you pass run after that. The window must stay in the foreground (keybd_event / mouse_event).
# Always -offline: without it the dedicated registers on the public U.S.G.N. serverlist.
# The dedicated rewrites sys/server.cfg (sv_map) and the client sys/recentlyjoined.lst: both are restored.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tools/agent/out"
mkdir -p "$OUT"
LUA="$1"; TAG="$2"; STEPS="$3"; MAP="${4:-prison_funst3r}"; T="${5:-60}"
cd "$ROOT"
cp sys/server.cfg "$OUT/server.cfg.bak"; cp sys/recentlyjoined.lst "$OUT/recentlyjoined.lst.bak"
taskkill //F //IM cs2d_dedicated.exe >/dev/null 2>&1; taskkill //F //IM CS2D.exe >/dev/null 2>&1
LUAARG=(); [ -n "$LUA" ] && LUAARG=(-lua "../../tools/agent/lua/$LUA")   # -lua is relative to sys/lua
(timeout $((T + 10)) ./cs2d_dedicated.exe -offline -map "$MAP" -port 36970 "${LUAARG[@]}" > "$OUT/ded_$TAG.log" 2>&1 &)
sleep 4
(python -I tools/agent/udp_proxy.py 36980 36970 "$OUT/proxy_$TAG.log" "$T" 2>/dev/null &)
sleep 1
(./CS2D.exe -join 127.0.0.1:36980 -nointro -offline > /dev/null 2>&1 &)
sleep 14
powershell -NoProfile -ExecutionPolicy Bypass -File tools/agent/drive_orig.ps1 \
    -Steps "focus;wait:500;tap:2;wait:800;tap:1;wait:3000;focus;$STEPS" | tail -1
sleep 1
taskkill //F //IM CS2D.exe >/dev/null 2>&1; sleep 1
taskkill //F //IM cs2d_dedicated.exe >/dev/null 2>&1
cp "$OUT/server.cfg.bak" sys/server.cfg; cp "$OUT/recentlyjoined.lst.bak" sys/recentlyjoined.lst
grep -E "^(BP|WP|PD|JL) |rror" "$OUT/ded_$TAG.log"
