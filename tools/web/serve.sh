#!/bin/sh
# Serves out/web on http://localhost:8080/cs2d.html (local only; Ctrl+C to stop)
cd "$(dirname "$0")/../../out/web"
exec python -I -m http.server 8080 --bind 127.0.0.1
