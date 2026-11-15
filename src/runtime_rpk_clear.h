/* Diagnostic observation before the existing guest memset. Never performs the write. */
#pragma once
#include <stdint.h>
void runtime_rpk_clear_before(uintptr_t destination, int value, uint64_t bytes, uintptr_t caller);
