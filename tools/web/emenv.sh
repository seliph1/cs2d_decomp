#!/bin/sh
# Puts emcc / node on PATH. Source it: `. tools/web/emenv.sh`.
# EMSDK defaults to the sibling checkout (../emsdk next to this repo); override with EMSDK=/path.
: "${EMSDK:=$(cd "$(dirname "$0")/../../.." 2>/dev/null && pwd)/emsdk}"
[ -d "$EMSDK/upstream/emscripten" ] || EMSDK=/c/Users/Gabriel/Documents/GitHub/emsdk
export EMSDK
export EM_CONFIG="$EMSDK/.emscripten"
for d in "$EMSDK"/node/*/ "$EMSDK"/python/*/; do PATH="${d%/}:$PATH"; done
export PATH="$EMSDK/upstream/emscripten:$PATH"
# The emcc shell wrapper looks for python3 first, which on Windows is the Store alias.
for p in "$EMSDK"/python/*/python.exe; do export EMSDK_PYTHON="$p"; done
