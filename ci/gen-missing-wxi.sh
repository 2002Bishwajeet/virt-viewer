#!/bin/bash
# msitools ships prebuilt .wxi component groups for the mingw stack, but they are
# generated at msitools release time and drift from whatever Fedora currently ships.
# Three failure modes, all fatal to wixl:
#   1. a .wxi is referenced but not shipped at all           (angleproject, giflib)
#   2. a shipped .wxi lists files the package no longer has  (gstreamer1-plugins-good
#                                                             still lists libgsty4menc)
#   3. a shipped .wxi references a CG whose package was renamed
#      (openal-soft.wxi wants CG.SDL2; Fedora now ships sdl2-compat)
# None of this bit before because virt-viewer's MSI never pulled in the GStreamer
# plugin groups. Regenerate the broken ones with msitools' own wixl-heat, keeping
# their <?require?> edges so the rest of the closure still gets referenced.
set -euo pipefail

S=/usr/x86_64-w64-mingw32/sys-root/mingw
W=$(rpm -ql msitools | grep -m1 'spice-gtk3.wxi' | xargs dirname)

roots=(spice-gtk3 gstreamer1-plugins-base gstreamer1-plugins-good
       gstreamer1-plugins-bad-free adwaita-icon-theme libxml2)

sources_of() { grep -ho 'Source="$(var.SourceDir)[^"]*"' "$W/$1.wxi" 2>/dev/null |
               sed 's|Source="$(var.SourceDir)||; s|"$||'; }
requires_of() { grep -ho '<?require [^?]*?>' "$W/$1.wxi" 2>/dev/null |
                sed 's/<?require //; s/?>//; s/\.wxi//' | tr -d ' '; }

owner_pkg() {                       # ask rpm about a file the wxi lists that exists
    local n=$1 f
    while read -r f; do
        [ -e "$S$f" ] || continue
        rpm -qf --queryformat '%{NAME}\n' "$S$f" 2>/dev/null | head -1 && return 0
    done < <(sources_of "$n")
    rpm -q --whatprovides --queryformat '%{NAME}\n' "mingw64-$n" 2>/dev/null | head -1 ||
        echo "mingw64-$n"
}

is_stale() { local f; while read -r f; do [ -e "$S$f" ] || return 0
             done < <(sources_of "$1"); return 1; }

regen() {
    local n=$1 pkg=$2; shift 2
    local reqargs=() r
    for r in "$@"; do reqargs+=(--require "$r"); done
    rpm -ql "$pkg" | grep "^$S/" | while read -r f; do [ -f "$f" ] && echo "$f"; done |
        # -i emits an <Include> root; without it wixl aborts with
        # "unhandled child Wix node Wix" when the file is included.
        wixl-heat -i --var var.SourceDir -p "$S/" --directory-ref INSTALLDIR \
                  --win64 --component-group "CG.$n" "${reqargs[@]}" > "$W/$n.wxi"
}

ensure() {                          # generate-if-missing / resync-if-stale
    local n=$1 pkg
    if [ ! -e "$W/$n.wxi" ]; then
        pkg=$(rpm -q --whatprovides --queryformat '%{NAME}\n' "mingw64-$n" 2>/dev/null | head -1) ||
            pkg="mingw64-$n"
        [ -n "$pkg" ] || pkg="mingw64-$n"
        dnf install -y "$pkg" >/dev/null 2>&1 || {
            dnf install -y "mingw64-$n" >/dev/null 2>&1 || {
                echo "!! nothing provides mingw64-$n for missing $n.wxi" >&2; exit 1; }
            pkg="mingw64-$n"; }
        pkg=$(rpm -q --whatprovides --queryformat '%{NAME}\n' "$pkg" | head -1)
        regen "$n" "$pkg"; echo "generated $n.wxi from $pkg"
    elif is_stale "$n"; then
        pkg=$(owner_pkg "$n")
        mapfile -t reqs < <(requires_of "$n")
        regen "$n" "$pkg" "${reqs[@]}"
        echo "resynced  $n.wxi from $pkg"
    fi
}

declare -A inclosure
walk() {                            # <?require?> closure from a seed
    local queue=("$@") n dep
    while [ ${#queue[@]} -gt 0 ]; do
        n="${queue[0]}"; queue=("${queue[@]:1}")
        [ -n "${inclosure[$n]:-}" ] && continue
        ensure "$n"; inclosure[$n]=1
        while read -r dep; do [ -n "$dep" ] && queue+=("$dep"); done < <(requires_of "$n")
    done
}

walk "${roots[@]}"

# Mode 4: a .wxi must only list files its own package owns. msitools' spice-glib.wxi
# inlines a hand-picked subset of GStreamer plugins (libgstapp, libgstaudioconvert,
# ...) rather than requiring the plugin packages. Once the real plugin groups are
# referenced those files appear twice and libmsi fails the duplicate insert. Resync
# any such wxi from its own package.
pkg_of_name() { rpm -q --whatprovides --queryformat '%{NAME}\n' "mingw64-$1" 2>/dev/null | head -1; }

files=(); for n in "${!inclosure[@]}"; do files+=("$W/$n.wxi"); done
mapfile -t dups < <(grep -h 'Source="$(var.SourceDir)' "${files[@]}" |
                    sed 's|.*Source="$(var.SourceDir)||; s|".*||' | sort | uniq -d)
declare -A resync
for p in "${dups[@]}"; do
    owner=$(rpm -qf --queryformat '%{NAME}\n' "$S$p" 2>/dev/null | head -1) || continue
    for n in "${!inclosure[@]}"; do
        grep -q "SourceDir)$p\"" "$W/$n.wxi" || continue
        own=$(pkg_of_name "$n")
        [ -n "$own" ] && [ "$own" != "$owner" ] && resync[$n]=$own
    done
done
for n in "${!resync[@]}"; do
    mapfile -t reqs < <(requires_of "$n")
    regen "$n" "${resync[$n]}" "${reqs[@]}"
    echo "deduped   $n.wxi from ${resync[$n]} (was carrying other packages' files)"
done

# Fixpoint on ComponentGroupRefs that nothing in the closure defines (mode 3).
# A standalone <name>.wxi would not help: wixl only pulls in files something
# <?require?>s. So alias the group inside the file that IS required, picking it by
# name similarity (sdl2-compat <-> SDL2).
norm() { echo "$1" | tr 'A-Z' 'a-z' | tr -cd 'a-z0-9'; }

add_alias() {                       # $1 = provider wxi, $2 = alias group name
    python3 - "$W/$1.wxi" "$2" "$1" <<'PY'
import io, sys
path, alias, provider = sys.argv[1], sys.argv[2], sys.argv[3]
s = io.open(path, encoding='utf8').read()
frag = ('  <Fragment>\n'
        '    <ComponentGroup Id="CG.%s">\n'
        '      <ComponentGroupRef Id="CG.%s"/>\n'
        '    </ComponentGroup>\n'
        '  </Fragment>\n</Include>' % (alias, provider))
assert s.count('</Include>') == 1, path
io.open(path, 'w', encoding='utf8').write(s.replace('</Include>', frag))
PY
}

while :; do
    files=(); for n in "${!inclosure[@]}"; do files+=("$W/$n.wxi"); done
    defined=$(grep -ho '<ComponentGroup Id="CG\.[^"]*"' "${files[@]}" |
              sed 's/.*Id="CG\.//; s/"//' | sort -u)
    referenced=$(grep -ho 'ComponentGroupRef Id="CG\.[^"]*"' "${files[@]}" |
                 sed 's/.*Id="CG\.//; s/"//' | sort -u)
    missing=$(comm -13 <(echo "$defined") <(echo "$referenced"))
    [ -z "$missing" ] && break
    progress=0
    for m in $missing; do
        for n in "${!inclosure[@]}"; do
            grep -q "ComponentGroupRef Id=\"CG\.$m\"" "$W/$n.wxi" || continue
            for r in $(requires_of "$n"); do
                [ -n "${inclosure[$r]:-}" ] || continue
                case "$(norm "$r")" in "$(norm "$m")"*) ;; *)
                    case "$(norm "$m")" in "$(norm "$r")"*) ;; *) continue ;; esac ;;
                esac
                add_alias "$r" "$m"
                echo "aliased   CG.$m -> CG.$r (in $r.wxi, for $n.wxi)"
                progress=1; break 2
            done
        done
    done
    [ "$progress" = 1 ] || { echo "!! cannot resolve: $(echo $missing)" >&2; exit 1; }
done

echo "--- wxi closure clean (${#inclosure[@]} groups) ---"
