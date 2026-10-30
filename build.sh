#!/bin/sh
# Builds bin/browser9300.sis (ARMI, for the Nokia 9300 / 9500, Series 80 2.0).
# Needs the EKA1 toolchain + Series 80 SDK in $SYM (default ~/sym): bash s80-build-kit/install.sh
# The version is the one in inc/browser.h (KBrVersion "0.N").
set -e
SYM=${SYM:-$HOME/sym}
HERE=$(cd "$(dirname "$0")" && pwd)
export EPOCROOT=$SYM/s80_20/ PATH=$SYM/wrap:$SYM/gcc539/bin:$PATH
MINOR=$(sed -n 's/.*KBrVersion, "0\.\([0-9]*\)".*/\1/p' "$HERE/inc/browser.h")
[ -n "$MINOR" ] || { echo "no version in inc/browser.h"; exit 1; }
cd "$HERE/group"
bldmake bldfiles >/dev/null 2>&1 || true   # ("Platform \"\" not supported" is harmless)
abld reallyclean armi urel >/dev/null 2>&1 || true
abld build armi urel 2>&1 | grep -v "uninitialized value" | tee /tmp/browser9300_build.log
if grep -q -E "Error [0-9]|error:|RCOMP failed|MISSING|Error:" /tmp/browser9300_build.log; then echo "BUILD FAILED"; exit 1; fi
E=$SYM/s80_20/epoc32
[ -f "$E/release/armi/urel/browser9300.app" ] || { echo "BUILD FAILED (no .app)"; exit 1; }
petran -v "$E/release/armi/urel/browser9300.app" | grep -i "data size"
mkdir -p "$HERE/bin"
sed -e "s|EPOC32|$E|g" -e "s|VERSION_MINOR|$MINOR|" "$HERE/sis/browser9300.pkg.in" > "$HERE/bin/browser9300.pkg"
cd "$HERE/bin" && makesis browser9300.pkg browser9300.sis && rm browser9300.pkg
ls -l "$HERE/bin/browser9300.sis"
