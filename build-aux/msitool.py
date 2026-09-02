#!/usr/bin/python3

import os
import subprocess
import sys
import tempfile

if len(sys.argv) != 13:
    print("syntax: %s BUILD-DIR PREFIX WIXL-ARCH MSIFILE WXS "
          "BUILDENV WIXL-HEAT-PATH WIXL-PATH "
          "HAVE_SPICE HAVE_VNC HAVE_LIBVIRT HAVE_OVIRT" % sys.argv[0])
    sys.exit(1)

builddir = sys.argv[1]
prefix = sys.argv[2]
arch = sys.argv[3]
msifile = sys.argv[4]
wxs = sys.argv[5]
buildenv = sys.argv[6]

wixl_heat = sys.argv[7]
wixl = sys.argv[8]

have_spice = sys.argv[9]
have_vnc = sys.argv[10]
have_libvirt = sys.argv[11]
have_ovirt = sys.argv[12]

def build_msi():
    if "DESTDIR" not in os.environ:
        print("$DESTDIR environment variable missing. "
              "Please run 'ninja install' before attempting to "
              "build the MSI binary, and set DESTDIR to point "
              "to the installation virtual root.", file=sys.stderr)
        sys.exit(1)

    if "MANUFACTURER" not in os.environ:
        os.environ["MANUFACTURER"] = "The Virt Viewer Project"

    vroot = os.environ["DESTDIR"]

    manifest = []
    for root, subFolder, files in os.walk(vroot):
        for item in files:
            path = str(os.path.join(root,item))
            manifest.append(path)

    wxsfiles = subprocess.run(
        [
            wixl_heat,
            "-p", vroot + prefix + "/",
            "--component-group", "CG.virt-viewer",
            "--var", "var.DESTDIR",
            "--directory-ref", "INSTALLDIR",
        ],
        input="\n".join(manifest),
        encoding="utf8",
        check=True,
        capture_output=True)

    wxsfilelist = os.path.join(builddir, "data", "virt-viewer-files.wxs")
    with open(wxsfilelist, "w") as fh:
        print(wxsfiles.stdout, file=fh)

    wixlenv = os.environ
    wixlenv["MANUFACTURER"] = "Virt Viewer Project"

    subprocess.run(
        [
            wixl,
            "-D", "SourceDir=" + prefix,
            "-D", "DESTDIR=" + vroot + prefix,
            "-D", "HaveSpiceGtk=" + have_spice,
            "-D", "HaveGtkVnc=" + have_vnc,
            "-D", "HaveLibvirt=" + have_libvirt,
            "-D", "HaveOVirt=" + have_ovirt,
            "--ext", "ui",
            "--arch", arch,
            "-o", msifile,
            wxs, wxsfilelist,
        ],
        check=True,
        env=wixlenv)

build_msi()
