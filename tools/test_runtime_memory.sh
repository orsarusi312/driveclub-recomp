#!/usr/bin/env bash
# Standalone backend regression; no game files or Vulkan dependencies.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
mkdir -p out/tests
cc=${CC:-clang}
command -v "$cc" >/dev/null || cc=gcc
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -pthread -UNDEBUG)
libs=()
if [[ ${OS:-} == Windows_NT ]]; then
    libs=(-lonecore)
    flags+=(-D_WIN32_WINNT=0x0a00 -DNTDDI_VERSION=0x0a000006 -Wl,--disable-high-entropy-va)
fi
if [[ ${SANITIZE:-0} == 1 ]]; then flags+=(-fsanitize=undefined -fno-sanitize-recover=all); fi
"$cc" "${flags[@]}" tools/runtime_memory_test.c "${libs[@]}" -o out/tests/runtime-memory-test.exe
BB_DMEM_MB=5056 BB_MEM_LOCK=atomic out/tests/runtime-memory-test.exe
BB_DMEM_MB=5056 BB_MEM_LOCK=pthread out/tests/runtime-memory-test.exe
"$cc" "${flags[@]}" tools/runtime_memory_profile_test.c -o out/tests/runtime-memory-profile-test.exe
profile_exe="$PWD/out/tests/runtime-memory-profile-test.exe"
if [[ ${OS:-} == Windows_NT ]]; then profile_exe=$(cygpath -w "$profile_exe"); fi
MEMORY_PROFILE_TEST="$profile_exe" python3 tools/test_runtime_memory_profile.py -v
