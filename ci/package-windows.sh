#!/usr/bin/env bash
# Package remote-viewer.exe + full runtime into a portable, double-clickable
# folder. Run from the MSYS2 UCRT64 shell. Re-runnable (wipes DEST first).
# NB: no `set -e`/`pipefail` — the ldd|grep pipelines legitimately return
# non-zero for DLLs with no ucrt64 deps, which must not abort the run.
#
# Paths are env-overridable so this works both locally and in CI:
#   UCRT  (default /ucrt64)      MSYS2 prefix
#   EXE   (default build-win/…)  the built remote-viewer.exe
#   DEST  (default ./remote-viewer-portable)   output folder
set -u

UCRT="${UCRT:-/ucrt64}"
EXE="${EXE:-$PWD/build-win/src/remote-viewer.exe}"
DEST="${DEST:-$PWD/remote-viewer-portable}"

echo ">> Resetting $DEST"
rm -rf "$DEST"
mkdir -p "$DEST/bin" "$DEST/lib/gstreamer-1.0" "$DEST/share"

cp "$EXE" "$DEST/bin/"

# --- GStreamer plugins (all of them; 27M) + the plugin scanner ---------------
echo ">> Copying GStreamer plugins"
cp "$UCRT"/lib/gstreamer-1.0/*.dll "$DEST/lib/gstreamer-1.0/"
scanner=$(find "$UCRT" -name 'gst-plugin-scanner.exe' | head -1 || true)
[ -n "$scanner" ] && cp "$scanner" "$DEST/bin/"

# helper exes glib may spawn
cp "$UCRT"/bin/gspawn-win64-helper*.exe "$DEST/bin/" 2>/dev/null || true

# --- Recursive DLL dependency closure ----------------------------------------
# Seed the queue with the exe + every plugin, then pull in every /ucrt64/bin
# DLL they (transitively) import.
echo ">> Resolving DLL dependency closure"
resolve() {
  local f="$1"
  ldd "$f" 2>/dev/null | grep -ioE "$UCRT/bin/[^ ]+\.dll" | while read -r dll; do
    local base; base=$(basename "$dll")
    if [ ! -f "$DEST/bin/$base" ]; then
      cp "$dll" "$DEST/bin/"
      resolve "$DEST/bin/$base"
    fi
  done
}
resolve "$DEST/bin/remote-viewer.exe"
for p in "$DEST"/lib/gstreamer-1.0/*.dll; do resolve "$p"; done
[ -n "$scanner" ] && resolve "$DEST/bin/gst-plugin-scanner.exe"

# --- GDK-Pixbuf loaders (+ regenerated cache for this layout) ----------------
echo ">> Copying gdk-pixbuf loaders"
mkdir -p "$DEST/lib/gdk-pixbuf-2.0/2.10.0/loaders"
cp "$UCRT"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll "$DEST/lib/gdk-pixbuf-2.0/2.10.0/loaders/"
for l in "$DEST"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll; do resolve "$l"; done
GDK_PIXBUF_MODULEDIR="$DEST/lib/gdk-pixbuf-2.0/2.10.0/loaders" \
  "$UCRT"/bin/gdk-pixbuf-query-loaders.exe \
  > "$DEST/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"

# --- GSettings schemas -------------------------------------------------------
echo ">> Copying GLib schemas"
mkdir -p "$DEST/share/glib-2.0/schemas"
cp "$UCRT"/share/glib-2.0/schemas/gschemas.compiled "$DEST/share/glib-2.0/schemas/"

# --- Icon themes (Adwaita + hicolor) so the GTK UI has its icons -------------
echo ">> Copying icon themes"
mkdir -p "$DEST/share/icons"
cp -r "$UCRT"/share/icons/Adwaita "$DEST/share/icons/" 2>/dev/null || true
cp -r "$UCRT"/share/icons/hicolor "$DEST/share/icons/" 2>/dev/null || true
# GTK needs the compiled icon cache; regenerate best-effort
"$UCRT"/bin/gtk-update-icon-cache.exe -q -t -f "$DEST/share/icons/Adwaita" 2>/dev/null || true

# --- CA bundle for TLS SPICE connections -------------------------------------
echo ">> Copying CA bundle"
mkdir -p "$DEST/ssl/certs"
cp "$UCRT"/etc/ssl/certs/ca-bundle.crt "$DEST/ssl/certs/" 2>/dev/null || true

# --- Launcher ----------------------------------------------------------------
echo ">> Writing launcher"
cat > "$DEST/remote-viewer.bat" <<'BAT'
@echo off
set "HERE=%~dp0"
set "PATH=%HERE%bin;%PATH%"
set "GST_PLUGIN_PATH=%HERE%lib\gstreamer-1.0"
set "GST_PLUGIN_SYSTEM_PATH=%HERE%lib\gstreamer-1.0"
set "GST_PLUGIN_SCANNER=%HERE%bin\gst-plugin-scanner.exe"
set "GDK_PIXBUF_MODULE_FILE=%HERE%lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"
set "GSETTINGS_SCHEMA_DIR=%HERE%share\glib-2.0\schemas"
set "XDG_DATA_DIRS=%HERE%share"
set "SSL_CERT_FILE=%HERE%ssl\certs\ca-bundle.crt"
start "" "%HERE%bin\remote-viewer.exe" %*
BAT

echo ">> DONE"
echo ">> DLLs bundled: $(ls "$DEST"/bin/*.dll | wc -l)"
du -sh "$DEST"
