#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
mkdir -p out/tests
cxx=${CXX:-clang++}
command -v "$cxx" >/dev/null || cxx=g++
flags=(-std=c++17 -O1 -g -Wall -Wextra -Werror -UNDEBUG -pthread)
if [[ ${SANITIZE:-0} == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie); fi
for mode in 0 1; do
    "$cxx" "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/mesh_probe_test.cpp -o out/tests/mesh-probe-$mode.exe
    out/tests/mesh-probe-$mode.exe
    out/tests/mesh-probe-$mode.exe --control-only
done
