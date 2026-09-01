#!/bin/bash
# Cross-compile virt-viewer + the forked SPICE stack for Windows and emit the MSI.
# Stages:  deps | protocol | gtk | viewer | wxi | caches | msi | all
set -euo pipefail

PREFIX=/usr/x86_64-w64-mingw32/sys-root/mingw
# Must match meson.build's wixl_arch; the MSI filename is built from it.
MSI_ARCH=${MSI_ARCH:-x64}
SRC=${SRC:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
WORK=${WORK:-/work}
VROOT="$WORK/vroot"

MESON_OPTS=${MESON_OPTS:---cross-file=/usr/share/mingw/toolchain-mingw64.meson}

SPICE_PROTOCOL_REPO=${SPICE_PROTOCOL_REPO:-https://gitlab.uni-freiburg.de/opensourcevdi/spice-protocol.git}
SPICE_GTK_REPO=${SPICE_GTK_REPO:-https://gitlab.uni-freiburg.de/opensourcevdi/spice-gtk.git}
SPICE_PROTOCOL_REF=${SPICE_PROTOCOL_REF:-new_video_codecs}
SPICE_GTK_REF=${SPICE_GTK_REF:-queueu_remove_experiment}

mkdir -p "$WORK"

stage_deps() {
    # Fedora images skip non-English %lang() files; wixl fails on the .mo files
    # msitools' .wxi groups then cannot find. Must precede the mingw64 installs.
    rm -f /etc/rpm/macros.image-language-conf

    dnf install -y 'dnf-command(builddep)'
    # Last version shipping the gdk-pixbuf SVG loader that loaders.cache advertises.
    dnf install -y mingw64-librsvg2-2.57.1-7.fc44
    dnf install -y \
        git meson ninja-build gcc make python3 \
        glib2-devel icoutils dos2unix perl-podlators \
        glibc-langpack-en msitools hwdata gtk-update-icon-cache
    dnf builddep -y --enablerepo='*-source' mingw64-spice-gtk3
    # spice-gtk3 is installed though we build over it: spice-gtk3.wxi lists its files.
    dnf install -y mingw64-spice-gtk3 mingw64-libxml2 mingw64-gettext \
        mingw64-libjpeg-turbo
    dnf install -y \
        mingw64-gstreamer1-plugins-base mingw64-gstreamer1-plugins-good \
        mingw64-gstreamer1-plugins-bad-free \
        mingw64-adwaita-icon-theme mingw64-hicolor-icon-theme \
        mingw64-glib-networking mingw64-filesystem mingw64-libusbx \
        mingw64-readline mingw64-usbredir
}

stage_protocol() {
    cd "$WORK"; rm -rf spice-protocol
    git clone --depth 1 -b "$SPICE_PROTOCOL_REF" "$SPICE_PROTOCOL_REPO"
    meson setup $MESON_OPTS --prefix="$PREFIX" spice-protocol/build spice-protocol
    ninja -C spice-protocol/build install
}

stage_gtk() {
    cd "$WORK"; rm -rf spice-gtk
    git clone --depth 1 -b "$SPICE_GTK_REF" "$SPICE_GTK_REPO"
    cd spice-gtk
    git submodule update --init --recursive --depth 1
    # --depth 1 carries no tags; 0.43 matches the sonames spice-gtk3.wxi hardcodes.
    echo 0.43 > .tarball-version
    cd "$WORK"
    # Fedora ships no mingw64 software decoder, so builtin-mjpeg is the only one.
    meson setup $MESON_OPTS --prefix="$PREFIX" spice-gtk/build spice-gtk \
        -Dgtk=enabled -Dbuiltin-mjpeg=true -Dopus=enabled \
        -Dwayland-protocols=disabled -Dintrospection=disabled -Dvapi=disabled
    ninja -C spice-gtk/build install
}

stage_viewer() {
    rm -rf "$WORK/virt-viewer"
    cp -a "$SRC" "$WORK/virt-viewer"
    cd "$WORK/virt-viewer"; rm -rf build
    # The prefix becomes wixl's SourceDir, so it must be the mingw sysroot.
    meson setup $MESON_OPTS build \
        --prefix="$PREFIX" --libdir=lib --bindir=bin \
        -Dspice=enabled -Dlibvirt=disabled -Dovirt=disabled \
        -Dvnc=disabled -Dvte=disabled -Dbash_completion=disabled
    ninja -C build
}

stage_wxi() { "$SRC/ci/gen-missing-wxi.sh"; }

stage_caches() {
    # No package owns these caches, so wixl-heat misses them; gio aborts without
    # gschemas.compiled. msitool.py walks the vroot, so writing them there is enough.
    install -d "$VROOT$PREFIX/share/glib-2.0/schemas"
    glib-compile-schemas --targetdir="$VROOT$PREFIX/share/glib-2.0/schemas" \
                         "$PREFIX/share/glib-2.0/schemas"

    # Likewise the SVG loader: loaders.cache lists it, no .wxi group ships it.
    install -Dm755 "$PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.dll" \
                   "$VROOT$PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.dll"

    for theme in Adwaita hicolor; do
        d="$PREFIX/share/icons/$theme"
        [ -f "$d/index.theme" ] || continue
        gtk-update-icon-cache -qtf "$d" || true
        [ -f "$d/icon-theme.cache" ] || continue
        install -Dm644 "$d/icon-theme.cache" "$VROOT$d/icon-theme.cache"
    done
}

stage_msi() {
    cd "$WORK/virt-viewer"; rm -rf "$VROOT"
    # msitool.py walks $DESTDIR to build the wixl-heat manifest; install there first.
    DESTDIR="$VROOT" ninja -C build install
    stage_caches
    # build_by_default:false, so the target has to be named exactly, version and all.
    local version msi
    version=$(meson introspect build --projectinfo |
              python3 -c 'import json,sys; print(json.load(sys.stdin)["version"])')
    msi="virt-viewer-$MSI_ARCH-$version.msi"
    echo "building data/$msi"
    DESTDIR="$VROOT" ninja -C build "data/$msi"
    # Hand the artifact back to the checkout so CI's artifacts:paths can collect it.
    cp "build/data/$msi" "$SRC/"
    ls -la "$SRC/$msi"
}

case "${1:-all}" in
    deps)     stage_deps ;;
    protocol) stage_protocol ;;
    gtk)      stage_gtk ;;
    viewer)   stage_viewer ;;
    wxi)      stage_wxi ;;
    caches)   stage_caches ;;
    msi)      stage_msi ;;
    all)      stage_deps; stage_protocol; stage_gtk; stage_viewer; stage_wxi; stage_msi ;;
    *)        echo "unknown stage: $1" >&2; exit 1 ;;
esac
