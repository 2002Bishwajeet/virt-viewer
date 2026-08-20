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
# Stages:  deps | protocol | gtk | viewer | wxi | msi | all
set -euo pipefail

PREFIX=/usr/x86_64-w64-mingw32/sys-root/mingw
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
    # Native tooling. glib2-devel provides glib-compile-resources/schemas, which run
    # on the build host, and msitools brings wixl/wixl-heat plus the prebuilt mingw
    # .wxi component groups. hwdata owns the usb.ids that virt-viewer.wxs.in points at.
    dnf install -y \
        git meson ninja-build gcc make python3 \
        glib2-devel icoutils dos2unix perl-podlators \
        glibc-langpack-en msitools hwdata
    # Fedora already knows what spice-gtk needs to build; don't hand-maintain a list.
    # This also drags in most of what virt-viewer itself needs (mingw64-gcc, -gtk3,
    # -glib2, -pkg-config). Fedora retired mingw-virt-viewer, so there is no
    # builddep to lean on for the rest -- hence the explicit names below.
    dnf builddep -y --enablerepo='*-source' mingw64-spice-gtk3
    # virt-viewer's own cross-build deps not covered by the spice-gtk closure.
    # mingw64-spice-gtk3 is installed even though we build our own over the top of
    # it: msitools' spice-gtk3.wxi is written against this package's file list.
    dnf install -y mingw64-spice-gtk3 mingw64-libxml2 mingw64-gettext
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
    meson setup $MESON_OPTS --prefix="$PREFIX" spice-gtk/build spice-gtk \
        -Dgtk=enabled -Dbuiltin-mjpeg=false -Dopus=enabled \
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

stage_msi() {
    cd "$WORK/virt-viewer"; rm -rf "$VROOT"
    # Both DESTDIR lines matter: msitool.py hard-errors without DESTDIR and walks
    # that tree to build the wixl-heat manifest, so the install must land there first.
    DESTDIR="$VROOT" ninja -C build install
    DESTDIR="$VROOT" ninja -C build "data/virt-viewer-x64-11.0.msi"
    # Hand the artifact back to the checkout so CI's artifacts:paths can collect it.
    cp build/data/*.msi "$SRC/"
    ls -la "$SRC"/*.msi
}

case "${1:-all}" in
    deps)     stage_deps ;;
    protocol) stage_protocol ;;
    gtk)      stage_gtk ;;
    viewer)   stage_viewer ;;
    wxi)      stage_wxi ;;
    msi)      stage_msi ;;
    all)      stage_deps; stage_protocol; stage_gtk; stage_viewer; stage_wxi; stage_msi ;;
    *)        echo "unknown stage: $1" >&2; exit 1 ;;
esac
