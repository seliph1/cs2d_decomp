#!/usr/bin/env bash
# Renders a known number of frames in the client's --capture harness, whose pacing is
# SDL_Delay(16) per frame. A Windows GPU counter can then be divided by the frame count to get
# ms of GPU time per frame; the FPS this reports is the harness's, never the game's.
#   bash tools/agent/perf_frames.sh [frames] [map]     map "-" = no map (main menu)
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FRAMES="${1:-600}"
MAP="${2:-fun_roleplay}"
cd "$ROOT"
# No space after ';': the capture scheduler matches the step text exactly ("!wait 600"), and a
# leading space makes it fall through to the console instead of the frame scheduler.
if [ "$MAP" = "-" ]; then
    exec ./cs2d_client.exe "--capture:!wait $FRAMES"
else
    exec ./cs2d_client.exe "--capture:map $MAP;!wait $FRAMES"
fi
