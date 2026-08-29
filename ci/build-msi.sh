#!/bin/bash
# Cross-compile virt-viewer + the forked SPICE stack for Windows and emit the MSI.
#
# Upstream's mingw64 CI jobs declare a .msi artifact but never build one:
# .cross_build_job only runs "meson build && ninja -C build", and the msi target is
# build_by_default:false (data/meson.build). The MSIs upstream actually publishes
# come from the RPM build (mingw-virt-viewer.spec.in). This script is that missing
# piece, plus the fork-specific bits.
#
# Self-contained on a bare Fedora image: stage_deps installs everything, so the
# same script drives ci/msi.yml and a local "podman run fedora:44" reproduction.
#
# Stages:  deps | protocol | gtk | viewer | wxi | caches | msi | all
set -euo pipefail

PREFIX=/usr/x86_64-w64-mingw32/sys-root/mingw
# Must match meson.build's wixl_arch; the MSI filename is built from it.
MSI_ARCH=${MSI_ARCH:-x64}
# Default to the checkout this script lives in, so CI (where the clone is the cwd)
# and a bind-mounted local run both work without being told.
SRC=${SRC:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
WORK=${WORK:-/work}
VROOT="$WORK/vroot"

# The only sane value; the mingw64 RPM macros cross-compile with the same file.
MESON_OPTS=${MESON_OPTS:---cross-file=/usr/share/mingw/toolchain-mingw64.meson}

SPICE_PROTOCOL_REPO=${SPICE_PROTOCOL_REPO:-https://gitlab.uni-freiburg.de/opensourcevdi/spice-protocol.git}
SPICE_GTK_REPO=${SPICE_GTK_REPO:-https://gitlab.uni-freiburg.de/opensourcevdi/spice-gtk.git}
SPICE_PROTOCOL_REF=${SPICE_PROTOCOL_REF:-new_video_codecs}
SPICE_GTK_REF=${SPICE_GTK_REF:-queueu_remove_experiment}

mkdir -p "$WORK"

stage_deps() {
    # The Fedora container images set %_install_langs to en_US, so rpm skips every
    # non-English %lang() file. msitools' glib2.wxi (and friends) reference ~101
    # locale .mo files by path, and wixl hard-fails on the first one missing. This
    # has to happen before the mingw64 packages are installed.
    rm -f /etc/rpm/macros.image-language-conf

    dnf install -y 'dnf-command(builddep)'
    # librsvg dropped its gdk-pixbuf loader in 2.59 and the updates repo carries
    # 2.62, but mingw64-gdk-pixbuf's loaders.cache still advertises it. GTK then
    # believes SVG is supported, prefers Adwaita's .svg icons over its handful of
    # .png ones, and the failed LoadLibrary becomes a fatal assertion in
    # gtkiconhelper.c -- remote-viewer.exe aborts before drawing a window. The GA
    # repo still has 2.57, which ships the loader; pin it before anything else
    # can pull in 2.62. Ships from stage_caches, which see.
    dnf install -y mingw64-librsvg2-2.57.1-7.fc44
    # Native tooling. glib2-devel provides glib-compile-resources/schemas, which run
    # on the build host, and msitools brings wixl/wixl-heat plus the prebuilt mingw
    # .wxi component groups. hwdata owns the usb.ids that virt-viewer.wxs.in points at.
    dnf install -y \
        git meson ninja-build gcc make python3 \
        glib2-devel icoutils dos2unix perl-podlators \
        glibc-langpack-en msitools hwdata gtk-update-icon-cache
    # Fedora already knows what spice-gtk needs to build; don't hand-maintain a list.
    # This also drags in most of what virt-viewer itself needs (mingw64-gcc, -gtk3,
    # -glib2, -pkg-config). Fedora retired mingw-virt-viewer, so there is no
    # builddep to lean on for the rest -- hence the explicit names below.
    dnf builddep -y --enablerepo='*-source' mingw64-spice-gtk3
    # virt-viewer's own cross-build deps not covered by the spice-gtk closure.
    # mingw64-spice-gtk3 is installed even though we build our own over the top of
    # it: msitools' spice-gtk3.wxi is written against this package's file list.
    # mingw64-libjpeg-turbo backs spice-gtk's builtin MJPEG decoder (see stage_gtk).
    dnf install -y mingw64-spice-gtk3 mingw64-libxml2 mingw64-gettext \
        mingw64-libjpeg-turbo
    # Runtime pieces the MSI's .wxi require-closure pulls in. These are listed in
    # mingw-virt-viewer.spec.in BuildRequires but are absent from the lcitool CI
    # image, because that image was only ever used to compile, never to package.
    # The GStreamer plugin packages are what carry the D3D11/D3D12 decoders.
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
    # --depth 1 carries no tags, so git-version-gen yields UNKNOWN and fails
    # virt-viewer's spice-gtk >= 0.35 check; it reads .tarball-version verbatim.
    # 0.43 matches Fedora's package, so the DLL sonames line up with the ones
    # msitools' spice-gtk3.wxi hardcodes (libspice-client-gtk-3.0-5.dll et al).
    echo 0.43 > .tarball-version
    cd "$WORK"
    # Fedora ships no software video decoder for mingw64 (no libav, openh264,
    # libvpx, dav1d), so every GStreamer decode path on Windows is hardware-only.
    # builtin-mjpeg needs just libjpeg and is the one decoder always available.
    meson setup $MESON_OPTS --prefix="$PREFIX" spice-gtk/build spice-gtk \
        -Dgtk=enabled -Dbuiltin-mjpeg=true -Dopus=enabled \
        -Dwayland-protocols=disabled -Dintrospection=disabled -Dvapi=disabled
    ninja -C spice-gtk/build install
}

stage_viewer() {
    rm -rf "$WORK/virt-viewer"
    cp -a "$SRC" "$WORK/virt-viewer"
    cd "$WORK/virt-viewer"; rm -rf build
    # The prefix is not cosmetic: build-aux/msitool.py hands meson's prefix to wixl
    # as SourceDir, and the .wxi component groups resolve every bundled system DLL
    # as $(var.SourceDir)/bin/*.dll. It must be the mingw sysroot or wixl goes
    # looking under /usr/local. This is what %mingw_meson does for the RPM build.
    meson setup $MESON_OPTS build \
        --prefix="$PREFIX" --libdir=lib --bindir=bin \
        -Dspice=enabled -Dlibvirt=disabled -Dovirt=disabled \
        -Dvnc=disabled -Dvte=disabled -Dbash_completion=disabled
    ninja -C build
}

stage_wxi() { "$SRC/ci/gen-missing-wxi.sh"; }

stage_caches() {
    # Both caches come from rpm file triggers, so no package owns them and
    # wixl-heat never sees them. msitool.py walks the vroot, so writing them
    # there is enough. gschemas.compiled is the fatal one: GSettings reads only
    # the compiled blob, and GTK3 and gio g_error() (abort, no message box) on a
    # lookup miss -- that is why remote-viewer.exe died before drawing a window.
    install -d "$VROOT$PREFIX/share/glib-2.0/schemas"
    glib-compile-schemas --targetdir="$VROOT$PREFIX/share/glib-2.0/schemas" \
                         "$PREFIX/share/glib-2.0/schemas"

    # No .wxi group ships the SVG pixbuf loader even though loaders.cache lists
    # it, so hand it over the same way. Its imports -- gdk-pixbuf, glib, gobject
    # and librsvg itself -- are all in the MSI already.
    install -Dm755 "$PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.dll" \
                   "$VROOT$PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.dll"

    # The icon caches are only a startup-speed win. Built in place and copied out
    # one file at a time, because copying the theme trees in would duplicate files
    # the .wxi groups already ship, and libmsi rejects that.
    for theme in Adwaita hicolor; do
        d="$PREFIX/share/icons/$theme"
        [ -f "$d/index.theme" ] || continue
        # An empty theme still exits 0 without writing a cache, so test for it.
        gtk-update-icon-cache -qtf "$d" || true
        [ -f "$d/icon-theme.cache" ] || continue
        install -Dm644 "$d/icon-theme.cache" "$VROOT$d/icon-theme.cache"
    done
}

stage_msi() {
    cd "$WORK/virt-viewer"; rm -rf "$VROOT"
    # Both DESTDIR lines matter: msitool.py hard-errors without DESTDIR and walks
    # that tree to build the wixl-heat manifest, so the install must land there first.
    DESTDIR="$VROOT" ninja -C build install
    stage_caches
    # The msi target is build_by_default:false, so it has to be named exactly.
    # Read the version from meson; hardcoding it turns a version bump into an
    # "unknown target" failure far from the change that caused it.
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
