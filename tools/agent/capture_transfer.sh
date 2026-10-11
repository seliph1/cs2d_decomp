#!/usr/bin/env bash
# Join-time file transfer test: OUR client, run from a fresh copy of the repo's game data, joins a
# dedicated server that lives in ANOTHER folder (e.g. a modded server with Lua and custom files), through
# the UDP proxy. Whatever the server transfers lands in the copy, never in the repo.
#
# usage: tools/agent/capture_transfer.sh <dedicated dir> <tag> "<capture steps>" [map] [seconds] [password]
#   e.g. tools/agent/capture_transfer.sh /c/C4 c4 "!wait 600" fun_roleplay 90 132
#
# CLIENT=orig runs the ORIGINAL CS2D.exe instead (-join, -offline); the steps are then drive_orig.ps1 steps,
#   e.g. "wait:4000;focus;tap:1;tap:3;tap:2;vk:0d;vk:0d:up;wait:120000" types the password into its prompt.
# KEEP=1 reuses game_<tag>/ from the previous run (only the client binary is refreshed): a second join.
# Output (tools/agent/out/): ded_<tag>.log, proxy_<tag>.log, cli_<tag>.log, game_<tag>/ (the client copy).
# The dedicated runs with -offline and its sys/server.cfg is backed up and restored.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tools/agent/out"
mkdir -p "$OUT"
DED="$1"; TAG="$2"; STEPS="$3"; MAP="${4:-}"; T="${5:-60}"; PASS="${6:-}"
GAME="$OUT/game_$TAG"

taskkill //F //IM cs2d_dedicated.exe >/dev/null 2>&1
taskkill //F //IM cs2d_client.exe >/dev/null 2>&1
if [ "${KEEP:-0}" != 1 ] || [ ! -d "$GAME" ]; then
  rm -rf "$GAME"; mkdir -p "$GAME"
  for d in gfx sfx maps sys incbindata logos lighting bots help mods; do cp -r "$ROOT/$d" "$GAME/"; done
fi
cp "$ROOT/CS2D.exe" "$ROOT/steam_api64.dll" "$GAME/"
cp "$ROOT/cs2d_client.exe" "$ROOT/SDL3.dll" "$ROOT/cs2d.ico" "$GAME/"
find "$GAME" -type f | sed "s|^$GAME/||" | sort > "$OUT/files_before_$TAG.txt"

cp "$DED/sys/server.cfg" "$OUT/server.cfg.$TAG.bak"
MAPARG=(); [ -n "$MAP" ] && MAPARG=(-map "$MAP")
(cd "$DED" && timeout $((T + 10)) ./cs2d_dedicated.exe -offline "${MAPARG[@]}" -port 36970 > "$OUT/ded_$TAG.log" 2>&1 &)
sleep 6
(python -I "$ROOT/tools/agent/udp_proxy.py" 36980 36970 "$OUT/proxy_$TAG.log" $((T + 2)) 2>/dev/null &)
sleep 1
if [ "${CLIENT:-ours}" = orig ]; then
  (cd "$GAME" && ./CS2D.exe -join 127.0.0.1:36980 -nointro -offline > /dev/null 2>&1 &)
  powershell -NoProfile -ExecutionPolicy Bypass -File "$ROOT/tools/agent/drive_orig.ps1" -Steps "$STEPS" | tail -1
  taskkill //F //IM CS2D.exe >/dev/null 2>&1
else
  (cd "$GAME" && CS2D_NET_LOG=1 timeout "$T" ./cs2d_client.exe "--capture:connect 127.0.0.1 36980 $PASS;$STEPS" > "$OUT/cli_$TAG.log" 2>&1)
fi
sleep 2
taskkill //F //IM cs2d_dedicated.exe >/dev/null 2>&1
cp "$OUT/server.cfg.$TAG.bak" "$DED/sys/server.cfg"

find "$GAME" -type f | sed "s|^$GAME/||" | sort > "$OUT/files_after_$TAG.txt"
echo "new files in game_$TAG:"
comm -13 "$OUT/files_before_$TAG.txt" "$OUT/files_after_$TAG.txt" | head -40
grep "\[console\]" "$OUT/cli_$TAG.log" | tail -15
