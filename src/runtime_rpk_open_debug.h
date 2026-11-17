/* Diagnostic-only arming boundary before the first matching host open. */
#pragma once
#include "../gpu/shim/bbport_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

typedef struct { pthread_mutex_t lock; unsigned consumed; } RpkOpenDebugState;
#define RPK_OPEN_DEBUG_STATE {PTHREAD_MUTEX_INITIALIZER,0}
typedef struct { int (*attached)(void); void (*pause)(const char *); } RpkOpenDebugOps;
static inline int rpk_open_debug_enabled(const char *setting) {
    return BB_DIAGNOSTICS && setting && !strcmp(setting,"1");
}
static inline int rpk_open_debug_dispatch(RpkOpenDebugState *state,int enabled,
        const char *expected,const char *path,const RpkOpenDebugOps *ops) {
    if(!BB_DIAGNOSTICS || !enabled || !expected || !*expected || !path || strcmp(expected,path)) return 0;
    const int saved_errno=errno;
#ifdef _WIN32
    const DWORD saved_error=GetLastError();
#endif
    int paused=0;
    /* Before host open/fd publication, with no runtime file-table lock held.
     * Matching concurrent opens cannot pass the selected pause. No GPU locks. */
    pthread_mutex_lock(&state->lock);
    if(!state->consumed) {
        state->consumed=1;
        if(ops->attached()) {
            ops->pause(expected); paused=1; /* Immutable validated manifest path. */
        } else fputs("RpkOpenDebug: no attached debugger; first-open stop consumed\n",stderr);
    }
    pthread_mutex_unlock(&state->lock);
#ifdef _WIN32
    SetLastError(saved_error);
#endif
    errno=saved_errno;
    return paused;
}
static inline void rpk_open_debug_pause(const char *path) {
#ifdef _WIN32
    char line[768];
    snprintf(line,sizeof line,"RpkOpenDebug: STOP before-first-host-open path=%s; no resource bytes read by this open; install bounded parser breakpoint now\n",path);
    fputs(line,stderr); fflush(stderr); OutputDebugStringA(line);
    DebugBreak();
    fputs("RpkOpenDebug: resumed; stop consumed; host open may now proceed\n",stderr);
#else
    (void)path;
#endif
}
