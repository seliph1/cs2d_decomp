#!/usr/bin/env bash
# Looks up functions in the Ghidra dump of CS2D.exe (docs/dump, git-ignored, client only).
#
# usage: tools/agent/dumpgrep.sh <address | Class::method | FUN_address>   print the whole function
#        tools/agent/dumpgrep.sh -l <regex>                                list functions whose body matches
#        tools/agent/dumpgrep.sh -f <Class>                                list the methods of a class
#   e.g. tools/agent/dumpgrep.sh 1401f7600          (also 0x1401f7600 or FUN_1401f7600)
#        tools/agent/dumpgrep.sh Tpl::move
#        tools/agent/dumpgrep.sh -l 'FUN_1402563a0\('   (callers of a function)
#
# Every function starts with a header line "// <address>  <name>"; addresses match REA/Ghidra on CS2D.exe.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DUMP="$ROOT/docs/dump"
[ -d "$DUMP" ] || { echo "docs/dump not found" >&2; exit 1; }
[ $# -ge 1 ] || { sed -n '2,10p' "$0"; exit 1; }

case "$1" in
  -l)
    # Print "file:address name" of each function whose body matches the regex.
    grep -rlE --include=*.c -- "$2" "$DUMP" | while read -r f; do
      RE="$2" awk -v file="${f#$ROOT/}" '
        /^\/\/ [0-9a-f]+  / { hdr = substr($0, 4); next }
        hdr != "" && $0 ~ ENVIRON["RE"] { print file ": " hdr; hdr = "" }' "$f"
    done
    ;;
  -f)
    f="$(find "$DUMP/cs2d" -name "$2.c" | head -1)"
    [ -n "$f" ] || { echo "no class file $2.c" >&2; exit 1; }
    echo "${f#$ROOT/}"
    grep -E '^// [0-9a-f]+  ' "$f" | sed 's|^// ||'
    ;;
  *)
    key="${1#0x}"; key="${key#FUN_}"
    if [[ "$key" =~ ^[0-9a-fA-F]+$ ]]; then
      pat="^// $(echo "$key" | tr 'A-F' 'a-f')  "
    else
      pat="^// [0-9a-f]+  $(printf '%s' "$1" | sed 's/[.[\*^$]/\\&/g')\$"
    fi
    f="$(grep -rlE --include=*.c -- "$pat" "$DUMP" | head -1)"
    [ -n "$f" ] || { echo "not found: $1 (the dump holds function starts only; for an address inside a function use REA resolve_containing_procedure)" >&2; exit 1; }
    echo "// file: ${f#$ROOT/}"
    PAT="$pat" awk '
      $0 ~ /^\/\/ [0-9a-f]+  / { if (on) exit; if ($0 ~ ENVIRON["PAT"]) on = 1 }
      on { print }' "$f"
    ;;
esac
