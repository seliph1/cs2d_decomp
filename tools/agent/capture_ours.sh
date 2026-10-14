#!/usr/bin/env bash
# Runs OUR client (cs2d_client.exe --capture) against the ORIGINAL dedicated server through a UDP proxy.
#
# usage: tools/agent/capture_ours.sh <lua file in tools/agent/lua | ""> <tag> "<capture steps>" [map] [seconds]
#   e.g. tools/agent/capture_ours.sh weapon_probe.lua wo "!wait 150;!press R;!wait 100;inv" prison_funst3r 40
#
# Capture steps are ';'-separated console commands or harness actions (see src/main_client.c):
#   !wait <frames>  !press <key>  !mouse <x> <y> [down|up]  !shot <file.bmp>  !key ...  !text ...
# Console commands worth knowing: inv, where, entstate <x> <y>, entdump, walkto <tx> <ty>, net_send <hex>.
# Output (tools/agent/out/): ded_<tag>.log, proxy_<tag>.log, cli_<tag>.log (CS2D_NET_LOG trace + console).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tools/agent/out"
mkdir -p "$OUT"
LUA="$1"; TAG="$2"; STEPS="$3"; MAP="${4:-prison_funst3r}"; T="${5:-40}"
cd "$ROOT"
cp sys/server.cfg "$OUT/server.cfg.bak"; cp sys/recentlyjoined.lst "$OUT/recentlyjoined.lst.bak"
taskkill //F //IM cs2d_dedicated.exe >/dev/null 2>&1
LUAARG=(); [ -n "$LUA" ] && LUAARG=(-lua "../../tools/agent/lua/$LUA")
(timeout $((T + 10)) ./cs2d_dedicated.exe -offline -map "$MAP" -port 36970 "${LUAARG[@]}" > "$OUT/ded_$TAG.log" 2>&1 &)
sleep 4
(python -I tools/agent/udp_proxy.py 36980 36970 "$OUT/proxy_$TAG.log" $((T + 2)) 2>/dev/null &)
sleep 1
CS2D_NET_LOG=1 timeout "$T" ./cs2d_client.exe "--capture:connect 127.0.0.1 36980;$STEPS" > "$OUT/cli_$TAG.log" 2>&1
sleep 2
taskkill //F //IM cs2d_dedicated.exe >/dev/null 2>&1
cp "$OUT/server.cfg.bak" sys/server.cfg; cp "$OUT/recentlyjoined.lst.bak" sys/recentlyjoined.lst
grep -E "^(BP|WP|PD|JL|CP) |rror" "$OUT/ded_$TAG.log"
grep "\[console\]" "$OUT/cli_$TAG.log" | tail -8
