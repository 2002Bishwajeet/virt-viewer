#!/usr/bin/python3

# Render a plain text licence as RTF: the MSI licence page's ScrollableText
# control renders nothing else.

import sys

if len(sys.argv) != 3:
    print("syntax: %s INPUT OUTPUT" % sys.argv[0], file=sys.stderr)
    sys.exit(1)

with open(sys.argv[1], encoding="utf-8") as fh:
    text = fh.read()

# Backslash first, or it would escape the escapes added for the braces.
for char in ("\\", "{", "}"):
    text = text.replace(char, "\\" + char)

# COPYING is ASCII; anything else would need \'hh escapes, so substitute instead.
with open(sys.argv[2], "w", encoding="ascii", errors="replace") as fh:
    fh.write(r"{\rtf1\ansi\deff0{\fonttbl{\f0\fnil\fcharset0 Courier New;}}\fs16 ")
    fh.write(text.replace("\n", "\\par\n"))
    fh.write("}")
