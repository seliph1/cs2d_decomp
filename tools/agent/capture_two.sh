#!/usr/bin/env bash
# Records a TWO-client session on ONE original dedicated server: the ORIGINAL client (CS2D.exe) through
# its own UDP proxy and OUR client (cs2d_client.exe) through a second one, so both opcode streams can be
# compared (multi-client verification against the original).
#
# usage: tools/agent/capture_two.sh <lua file in tools/agent/lua | ""> <tag> "<drive_orig.ps1 steps>" "<our capture steps>" [map] [seconds]
#   e.g. (a mutual-fire session, `twoclient_fire_probe.lua` arms and places both players)
#     OSTEPS='focus;mouse:320:240;down:W;wait:700;up:W;wait:200'
#     for i in $(seq 1 6); do OSTEPS="$OSTEPS;click:3:400;wait:300"; done     # 6 bursts of 3 clicks
#     CSTEPS='!wait 400;team ct;!wait 60;!hold W down;!wait 10;!hold W up'    # our client: ~29 fps here
#     for i in $(seq 1 20); do CSTEPS="$CSTEPS;!mouse 320 240 down;!wait 4;!mouse 320 240 up;!wait 6"; done
#     tools/agent/capture_two.sh twoclient_fire_probe.lua two "$OSTEPS" "$CSTEPS" prison_funst3r 45
#
# Timing (the drivers must overlap): the original joins ~6 s after launch and is driven 14 s after it, the
# pre-roll in drive_orig.ps1 adds ~3 s, so its action window is script t=22..31. Our client starts at t=10
# and its steps run on the frame scheduler (~29 fps), so its window lands at t=26..38 - hence the sleeps
# below. Each step list should therefore last about ten seconds.
#
# Each client gets its own proxy (36980 -> 36970 for the original, 36981 -> 36970 for ours) because
# udp_proxy.py forwards every reply to a single client address; the dedicated therefore sees two source
# ports of 127.0.0.1, like two clients on one machine.
# Output (tools/agent/out/): ded_two_<tag>.log, proxy_two_<tag>_orig.log, proxy_two_<tag>_our.log,
# cli_two_<tag>.log. Decode one side with: python -I tools/agent/dumpops.py <log> 229,247,249,1
# Always -offline: without it the dedicated registers on the public U.S.G.N. serverlist. The dedicated
# rewrites sys/server.cfg (sv_map) and the original client sys/recentlyjoined.lst: both are restored.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tools/agent/out"
mkdir -p "$OUT"
LUA="$1"; TAG="$2"; OSTEPS="$3"; CSTEPS="$4"; MAP="${5:-prison_funst3r}"; T="${6:-60}"
cd "$ROOT"
cp sys/server.cfg "$OUT/server.cfg.bak"; cp sys/recentlyjoined.lst "$OUT/recentlyjoined.lst.bak"
taskkill //F //IM cs2d_dedicated.exe >/dev/null 2>&1; taskkill //F //IM CS2D.exe >/dev/null 2>&1
taskkill //F //IM cs2d_client.exe >/dev/null 2>&1
LUAARG=(); [ -n "$LUA" ] && LUAARG=(-lua "../../tools/agent/lua/$LUA")   # -lua is relative to sys/lua
(timeout $((T + 20)) ./cs2d_dedicated.exe -offline -map "$MAP" -port 36970 "${LUAARG[@]}" > "$OUT/ded_two_$TAG.log" 2>&1 &)
sleep 4
(python -I tools/agent/udp_proxy.py 36980 36970 "$OUT/proxy_two_${TAG}_orig.log" "$T" 2>/dev/null &)
(python -I tools/agent/udp_proxy.py 36981 36970 "$OUT/proxy_two_${TAG}_our.log"  "$T" 2>/dev/null &)
sleep 1
(./CS2D.exe -join 127.0.0.1:36980 -nointro -offline > /dev/null 2>&1 &)
sleep 5
# Our client joins the same server through the second proxy; its steps run on the frame scheduler.
(CS2D_NET_LOG=1 timeout "$T" ./cs2d_client.exe "--capture:connect 127.0.0.1 36981;$CSTEPS" > "$OUT/cli_two_$TAG.log" 2>&1 &)
sleep 9
powershell -NoProfile -ExecutionPolicy Bypass -File tools/agent/drive_orig.ps1 \
    -Steps "focus;wait:500;tap:2;wait:800;tap:1;wait:3000;focus;$OSTEPS" | tail -1
sleep 4
taskkill //F //IM CS2D.exe >/dev/null 2>&1; sleep 1
taskkill //F //IM cs2d_client.exe >/dev/null 2>&1
taskkill //F //IM cs2d_dedicated.exe >/dev/null 2>&1
cp "$OUT/server.cfg.bak" sys/server.cfg; cp "$OUT/recentlyjoined.lst.bak" sys/recentlyjoined.lst
echo "--- dedicated (Lua) ---"
grep -E "^(BP|WP|PD|JL|CP) |rror" "$OUT/ded_two_$TAG.log"
echo "--- our client console tail ---"
grep "\[console\]" "$OUT/cli_two_$TAG.log" | tail -6
echo "--- opcodes the ORIGINAL sent (C>S) ---"
python -I tools/agent/dumpops.py "$OUT/proxy_two_${TAG}_orig.log" | awk '$2=="C>S"{print $4}' | sort | uniq -c | sort -rn
echo "--- opcodes OUR client sent (C>S) ---"
python -I tools/agent/dumpops.py "$OUT/proxy_two_${TAG}_our.log" | awk '$2=="C>S"{print $4}' | sort | uniq -c | sort -rn
echo "--- opcodes the dedicated sent to the ORIGINAL (S>C) ---"
python -I tools/agent/dumpops.py "$OUT/proxy_two_${TAG}_orig.log" | awk '$2=="S>C"{print $4}' | sort | uniq -c | sort -rn
echo "--- opcodes the dedicated sent to OURS (S>C) ---"
python -I tools/agent/dumpops.py "$OUT/proxy_two_${TAG}_our.log" | awk '$2=="S>C"{print $4}' | sort | uniq -c | sort -rn
