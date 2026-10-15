#!/bin/sh
# Builds and runs tools/web/font_compare.c (GDI+ vs stb_truetype). Windows / MSYS2 only.
set -e
cd "$(dirname "$0")/../.."
export PATH=/c/msys64/mingw64/bin:$PATH
mkdir -p out/font_compare
gcc -O2 -Wall -Iinclude -Ithird_party tools/web/font_compare.c src/cs2d_font.c src/cs2d_platform.c -lws2_32 -lm -o out/font_compare.exe
if [ $# -eq 0 ]; then set -- sys/language/EN-English.txt "sys/language/PT-BR-Portuguese (Portugues BR).txt"; fi
out/font_compare.exe "$@"
