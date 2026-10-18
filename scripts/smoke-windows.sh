#!/usr/bin/env bash
set -euo pipefail

cd build/bundle
export GDK_PIXBUF_MODULE_FILE="$(cygpath -w "$PWD/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache")"
./kiwi.exe about:blank &
pid=$!
sleep 20
if kill -0 "$pid" 2>/dev/null; then
    kill "$pid"
    echo "kiwi smoke: process ran and was stopped"
    exit 0
fi
code=0
wait "$pid" || code=$?
echo "kiwi smoke: exited early with code $code"
exit "$code"
