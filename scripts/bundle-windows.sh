#!/usr/bin/env bash
set -euo pipefail

prefix="${MINGW_PREFIX:-/mingw64}"
bundle="build/bundle"
runtime="build/runtime/runtime/bin64"

rm -rf "$bundle"
mkdir -p "$bundle" "$bundle/assets"

cp build/kiwi.exe "$bundle/"
cp "$runtime"/*.dll "$bundle/"
find "$runtime" -maxdepth 1 -type d -name "*.resources" -exec cp -r {} "$bundle/" \; 2>/dev/null || true
cp assets/logo.png "$bundle/assets/"

ntldd -R "$bundle/kiwi.exe" | awk '$2 == "=>" {print $3}' | sort -u | while read -r dep; do
    if [ -z "$dep" ] || [ "$dep" = "not" ]; then continue; fi
    p=$(cygpath -u "$dep" 2>/dev/null || echo "$dep")
    case "$p" in
        ${prefix}/bin/*) cp "$p" "$bundle/" ;;
    esac
done

mkdir -p "$bundle/lib/gio/modules"
cp "$prefix"/lib/gio/modules/*.dll "$bundle/lib/gio/modules/" 2>/dev/null || true

mkdir -p "$bundle/lib/gdk-pixbuf-2.0/2.10.0/loaders"
cp "$prefix"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll "$bundle/lib/gdk-pixbuf-2.0/2.10.0/loaders/" 2>/dev/null || true
cache="$bundle/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"
GDK_PIXBUF_MODULEDIR="$(cygpath -m "$PWD/$bundle/lib/gdk-pixbuf-2.0/2.10.0/loaders")" \
    "$prefix/bin/gdk-pixbuf-query-loaders.exe" > "$cache" 2>/dev/null || true
sed -i "s|$(cygpath -m "$PWD/$bundle")/||g" "$cache" || true

mkdir -p "$bundle/share/glib-2.0/schemas"
cp "$prefix"/share/glib-2.0/schemas/gschemas.compiled "$bundle/share/glib-2.0/schemas/" 2>/dev/null || true

cat > "$bundle/kiwi.cmd" <<'LAUNCHER'
@echo off
setlocal
cd /d "%~dp0"
set GDK_PIXBUF_MODULE_FILE=%~dp0lib\gdk-pixbuf-2.0\2.10.0\loaders.cache
start "" kiwi.exe %*
LAUNCHER

echo "bundle contents:"
ls "$bundle" | head -30
echo "dll count: $(ls "$bundle" | grep -c '\.dll$')"
