/* Diagnostic-only observation of existing HLE memcpy/memmove calls. */
#pragma once
#include <stdint.h>
#include "runtime_rpk_request.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uintptr_t source, destination; uint64_t bytes, id; int valid; uintptr_t source_base, destination_base, caller; RpkRequestTicket request; } RpkCopyTicket;
int runtime_rpk_copy_begin(uintptr_t source,uintptr_t destination,uint64_t bytes,RpkCopyTicket *ticket);
void runtime_rpk_copy_end(const RpkCopyTicket *ticket);
int runtime_rpk_copy_find(uintptr_t address,uint64_t bytes,uintptr_t *prefix,uint64_t *id);
#ifdef __cplusplus
}
#endif
