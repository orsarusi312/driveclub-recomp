#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
mkdir -p out/tests
cc=${CC:-clang}; command -v "$cc" >/dev/null || cc=gcc
cxx=${CXX:-clang++}; command -v "$cxx" >/dev/null || cxx=g++
flags=(-O1 -g -Wall -Wextra -Werror -UNDEBUG -pthread)
if [[ ${SANITIZE:-0} == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie); fi
for mode in 0 1; do
    "$cc" -std=c11 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/rpk_trace_test.c -o out/tests/rpk-trace-$mode.exe
    out/tests/rpk-trace-$mode.exe
    "$cxx" -std=c++17 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/mesh_search_test.cpp -o out/tests/mesh-search-$mode.exe
    out/tests/mesh-search-$mode.exe
done
