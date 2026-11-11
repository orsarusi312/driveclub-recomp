#!/usr/bin/env bash
# Synthetic end-to-end checks, no game assets. Run from any directory.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
mkdir -p work out
bash tools/modkit/build.sh
cxx=${CXX:-clang++}
command -v "$cxx" >/dev/null || cxx=g++
"$cxx" -std=c++17 -O1 -Wall -Wextra -o out/track-fixture tools/track_import/tests/make_fixture.cpp tools/modkit/src/rpk.cpp
out/track-fixture work/track-fixture.rpk
exe="$PWD/out/dcmod"
[[ -f out/dcmod.exe ]] && exe="$PWD/out/dcmod.exe"
fixture="$PWD/work/track-fixture.rpk"
if [[ ${OS:-} == Windows_NT ]]; then
    exe=$(cygpath -w "$exe")
    fixture=$(cygpath -w "$fixture")
fi
DCMOD="$exe" TRACK_FIXTURE="$fixture" python3 -m unittest discover -s tools/track_import/tests -v
cc=${CC:-clang}
command -v "$cc" >/dev/null || cc=gcc
"$cc" -std=c11 -O1 -Wall -Wextra -Werror tools/track_probe_test.c -lm -o out/track-probe-test
out/track-probe-test
if command -v blender >/dev/null; then
    blender --background --python-exit-code 1 --python tools/track_import/tests/blender_smoke.py
else
    echo 'Blender smoke check not run: blender is not on PATH'
fi
