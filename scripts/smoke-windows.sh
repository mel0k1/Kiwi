#!/usr/bin/env bash
set -euo pipefail

# Real-Windows smoke pass: the browser starts with a working WebKit content
# process, then --self-test drives tabs, the start page, omnibox navigation,
# tab closing and a window resize. Any crash, GTK critical or missing helper
# process fails the build; diagnostics land in %LocalAppData%\kiwi.

cd build/bundle
export GDK_PIXBUF_MODULE_FILE="$(cygpath -w "$PWD/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache")"

./kiwi.exe --self-test &
pid=$!

# --self-test quits on its own after ~15s; the 180s ceiling is a runaway guard.
for _ in $(seq 1 180); do
    if ! kill -0 "$pid" 2>/dev/null; then
        break
    fi
    sleep 1
done

if kill -0 "$pid" 2>/dev/null; then
    echo "kiwi smoke: self-test did not finish in 180s — killing" >&2
    kill "$pid" || true
    exit 1
fi
wait "$pid"
code=$?

logbase="$(cygpath -u "${LOCALAPPDATA:-$USERPROFILE/AppData/Local}")/kiwi"
if [ -f "$logbase/crash.log" ]; then
    echo "kiwi smoke: crash log written during self-test:" >&2
    cat "$logbase/crash.log" >&2
    code=1
fi
if [ -f "$logbase/kiwi.log" ]; then
    echo "kiwi smoke: GTK criticals during self-test:" >&2
    cat "$logbase/kiwi.log" >&2
fi

if [ "$code" -ne 0 ]; then
    echo "kiwi smoke: exited with code $code" >&2
    exit "$code"
fi
echo "kiwi smoke: self-test passed, no crash log"
