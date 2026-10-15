#!/bin/sh
# Phase 0 of the web port: the regression suite compiled by Emscripten and run under Node.
# NODERAWFS gives the Wasm the real file system, so the suite reads gfx/ maps/ sys/ tests/data like on Windows.
#   tools/web/build_tests.sh          build out/web/test_cs2d.js and run it
set -e
cd "$(dirname "$0")/../.."
. tools/web/emenv.sh
mkdir -p out/web
# Same source list as the test_cs2d.exe line of build.bat
SRC=$(grep -- '-o test_cs2d.exe' build.bat | sed -e 's/.*-Ithird_party //' -e 's/ tests\/test_cs2d\.c.*//')
emcc -std=gnu99 -O2 -Wall -Wextra -Wno-pointer-sign -Iinclude -Ithird_party \
    -sUSE_SDL=3 -sUSE_ZLIB=1 -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB -sEXIT_RUNTIME=1 \
    $SRC tests/test_cs2d.c -o out/web/test_cs2d.js
node out/web/test_cs2d.js
