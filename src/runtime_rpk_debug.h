/* One debugger-assisted stop at a verified RPK read. No guest hooks/protection changes. */
#pragma once
#include "../gpu/shim/bbport_diagnostics.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "runtime_rpk_reader.h"
extern int runtime_memory_read_backing(uintptr_t,void *,uint64_t);
#ifdef _WIN32
#include <windows.h>
extern uintptr_t runtime_memory_debug_backing_address(uintptr_t,uint64_t);
#endif

typedef struct {
    uint64_t resource, file_offset, read_bytes;
    uintptr_t read_base, prefix;
    uint64_t stride, width, samples;
} RpkDebugRead;
typedef struct { unsigned target, seen, consumed; } RpkDebugState;
typedef struct { int (*attached)(void); void (*pause)(const RpkDebugRead *); } RpkDebugOps;

/* Only the first or second fully verified read is selectable. Never retry a missed stop. */
static inline unsigned rpk_debug_target(const char *value) {
    return value && value[0]>='1' && value[0]<='2' && !value[1] ? (unsigned)(value[0]-'0'):0;
}
static inline int rpk_debug_dispatch(RpkDebugState *state,const RpkDebugRead *read,
                                     const char *header,const char *prefix,const RpkDebugOps *ops) {
    if(!state->target || strcmp(header,"verified") || strcmp(prefix,"verified")) return 0;
    if(__atomic_load_n(&state->seen,__ATOMIC_RELAXED)>=state->target) return 0;
    const unsigned ordinal=__atomic_fetch_add(&state->seen,1,__ATOMIC_RELAXED)+1;
    if(ordinal!=state->target || __atomic_exchange_n(&state->consumed,1,__ATOMIC_ACQ_REL)) return 0;
    if(!ops->attached()) {
        fprintf(stderr,"RpkDebugRead: selected=%u skipped: no attached Windows debugger; stop consumed, restart under debugger\n",ordinal);
        return 0;
    }
    fprintf(stderr,"RpkDebugRead: STOP selected=%u resource=%016llx prefix=0x%llx read-base=0x%llx read-bytes=%llu file-offset=%llu stride=%llu width=%llu samples=%llu; verified bytes, before read returns; install one hardware watchpoint now\n",
        ordinal,(unsigned long long)read->resource,(unsigned long long)read->prefix,
        (unsigned long long)read->read_base,(unsigned long long)read->read_bytes,
        (unsigned long long)read->file_offset,(unsigned long long)read->stride,
        (unsigned long long)read->width,(unsigned long long)read->samples);
    fflush(stderr);
    ops->pause(read);
    fputs("RpkDebugRead: resumed; no watchpoint installed by runtime; stop budget exhausted\n",stderr);
    return 1;
}
static inline int rpk_debug_attached(void) {
#ifdef _WIN32
    return IsDebuggerPresent()!=0;
#else
    return 0;
#endif
}
static inline void rpk_debug_pause(const RpkDebugRead *read) {
#ifdef _WIN32
    rpk_reader_note(read->resource,read->read_base,read->read_bytes,runtime_memory_read_backing);
    const uintptr_t backing=runtime_memory_debug_backing_address(read->prefix,1);
    char line[320];
    snprintf(line,sizeof line,"RpkDebugRead: WATCH resource=%016llx guest=0x%llx backing=0x%llx; BEFORE read returns. One-byte hardware watchpoints on available distinct aliases; no symbols required.\n",
        (unsigned long long)read->resource,(unsigned long long)read->prefix,(unsigned long long)backing);
    fputs(line,stderr); fflush(stderr);
    OutputDebugStringA(line);
    DebugBreak(); // Only dispatched after explicit opt-in and IsDebuggerPresent.
#else
    (void)read;
#endif
}
/* Called by RpkTrace's existing pthread_once initializer, before any observations. */
static RpkDebugState rpk_debug_state;
static inline void rpk_debug_init(void) {
    if(!BB_DIAGNOSTICS) return;
    rpk_reader_init();
    const char *setting=BB_DIAGNOSTIC_ENV("BB_RPK_DEBUG_READ");
    rpk_debug_state.target=rpk_debug_target(setting);
#ifndef _WIN32
    if(rpk_debug_state.target) {
        fputs("RpkDebugRead: debugger stop unsupported on this platform; disabled\n",stderr);
        rpk_debug_state.target=0;
    }
#endif
    if(setting && !rpk_debug_target(setting)) fputs("RpkDebugRead: invalid selection (use 1 or 2); disabled\n",stderr);
    if(rpk_debug_state.target) fprintf(stderr,"RpkDebugRead: enabled selected=%u; one stop, attached Windows debugger required\n",rpk_debug_state.target);
}
static inline void rpk_debug_read_note(const RpkDebugRead *read,const char *header,const char *prefix) {
    if(!BB_DIAGNOSTICS || !rpk_debug_state.target) return;
    const int saved_errno=errno;
#ifdef _WIN32
    const DWORD saved_error=GetLastError();
#endif
    const RpkDebugOps ops={rpk_debug_attached,rpk_debug_pause};
    rpk_debug_dispatch(&rpk_debug_state,read,header,prefix,&ops);
#ifdef _WIN32
    SetLastError(saved_error);
#endif
    errno=saved_errno;
}
