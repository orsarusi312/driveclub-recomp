#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
mkdir -p out/tests
cc=${CC:-clang}; command -v "$cc" >/dev/null || cc=gcc
cxx=${CXX:-clang++}; command -v "$cxx" >/dev/null || cxx=g++
flags=(-O1 -g -Wall -Wextra -Werror -UNDEBUG -pthread)
if [[ ${SANITIZE:-0} == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie); fi
for mode in 0 1; do
    "$cc" -std=c11 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/rpk_open_debug_test.c -o out/tests/rpk-open-debug-$mode.exe
    out/tests/rpk-open-debug-$mode.exe
    "$cc" -std=c11 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/rpk_request_test.c -o out/tests/rpk-request-$mode.exe
    out/tests/rpk-request-$mode.exe
    "$cc" -std=c11 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/rpk_read_watch_test.c -o out/tests/rpk-read-watch-$mode.exe
    out/tests/rpk-read-watch-$mode.exe
    "$cc" -std=c11 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/rpk_reader_test.c -o out/tests/rpk-reader-$mode.exe
    out/tests/rpk-reader-$mode.exe
    if [[ ${OS:-} != Windows_NT ]]; then
        "$cc" -std=c11 -I. "${flags[@]}" -ffunction-sections -fdata-sections -Wl,--gc-sections -DBB_DIAGNOSTICS=$mode tools/runtime_memset_trace_test.c -o out/tests/runtime-memset-trace-$mode.exe
        out/tests/runtime-memset-trace-$mode.exe
    else
        echo "Guest memset wrapper harness: skipped (requires ELF section garbage collection); observer tests still run"
    fi
    "$cc" -std=c11 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/rpk_clear_test.c tools/rpk_clear_test_peer.c -o out/tests/rpk-clear-$mode.exe
    out/tests/rpk-clear-$mode.exe
    "$cc" -std=c11 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/rpk_debug_test.c -o out/tests/rpk-debug-$mode.exe
    out/tests/rpk-debug-$mode.exe
    "$cxx" -std=c++17 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/buffer_path_test.cpp tools/buffer_path_test_peer.cpp -o out/tests/buffer-path-$mode.exe
    out/tests/buffer-path-$mode.exe
    out/tests/buffer-path-$mode.exe --invalid
    out/tests/buffer-path-$mode.exe --early
    out/tests/buffer-path-$mode.exe --absent
    "$cc" -std=c11 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/rpk_trace_test.c -o out/tests/rpk-trace-$mode.exe
    out/tests/rpk-trace-$mode.exe
    "$cc" -std=c11 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/rpk_copy_test.c -o out/tests/rpk-copy-$mode.exe
    out/tests/rpk-copy-$mode.exe
    "$cxx" -std=c++17 "${flags[@]}" -DBB_DIAGNOSTICS=$mode tools/mesh_search_test.cpp -o out/tests/mesh-search-$mode.exe
    out/tests/mesh-search-$mode.exe
    out/tests/mesh-search-$mode.exe --targeted
    out/tests/mesh-search-$mode.exe --invalid-allow
    out/tests/mesh-search-$mode.exe --allow-no-target
done
