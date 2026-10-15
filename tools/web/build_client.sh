#!/bin/sh
# Web port: cs2d_client compiled by Emscripten (WebGL2 + SDL3), with the game data preloaded.
#   tools/web/build_client.sh [-g]     -> out/web/cs2d.html (+ .js .wasm .data); -g keeps debug info
#   tools/web/serve.sh                 serves out/web on http://localhost:8080/cs2d.html
set -e
cd "$(dirname "$0")/../.."
. tools/web/emenv.sh
mkdir -p out/web

OPT="-O2"
[ "$1" = "-g" ] && OPT="-O1 -g -sASSERTIONS=1"

# Same source list as the cs2d_client.exe line of build.bat
SRC=$(grep -- '-o cs2d_client.exe' build.bat | sed -e 's/.*-Ithird_party //' -e 's/ -lSDL3.*//')

# Data: what the client opens at run time. Asset names keep their Windows case (111 under gfx/ and 245 under
# sys/ have capitals) while the code asks in lower case, hence CASE_INSENSITIVE_FS. The CJK fonts (16 MB)
# and gfx/backgrounds (*.jpg, referenced nowhere) are left out.
emcc -std=gnu99 $OPT -Wall -Wextra -Wno-pointer-sign -Iinclude -Ithird_party \
    -sUSE_SDL=3 -sUSE_ZLIB=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB -sCASE_INSENSITIVE_FS=1 \
    --preload-file gfx --preload-file sys --preload-file maps --preload-file sfx --preload-file cs2d.ico \
    --exclude-file 'gfx/fonts/chinese.ttc' --exclude-file 'gfx/fonts/japanese.ttf' \
    --exclude-file 'gfx/fonts/korean.ttf' --exclude-file 'gfx/backgrounds' \
    --shell-file tools/web/shell.html \
    $SRC -o out/web/cs2d.html
ls -la out/web/cs2d.*
