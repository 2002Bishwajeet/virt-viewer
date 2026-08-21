#!/usr/bin/python3

# Render a plain text licence as the RTF the MSI's licence page needs. The
# control that displays it is a ScrollableText, which understands RTF only --
# handed plain text it shows the file verbatim, control words and all.

import sys

if len(sys.argv) != 3:
    print("syntax: %s INPUT OUTPUT" % sys.argv[0], file=sys.stderr)
    sys.exit(1)

with open(sys.argv[1], encoding="utf-8") as fh:
    text = fh.read()

# Backslash first: doing it after the braces would escape their escapes too.
for char in ("\\", "{", "}"):
    text = text.replace(char, "\\" + char)

# COPYING is plain ASCII; anything else would need \'hh escapes rather than a
# straight write, so substitute instead of emitting bytes RTF would misread.
with open(sys.argv[2], "w", encoding="ascii", errors="replace") as fh:
    fh.write(r"{\rtf1\ansi\deff0{\fonttbl{\f0\fnil\fcharset0 Courier New;}}\fs16 ")
    fh.write(text.replace("\n", "\\par\n"))
    fh.write("}")
