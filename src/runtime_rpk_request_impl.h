/* Included after the existing position-prefix verifier. Selection/state is shared
 * with runtime_file.c; existing HLE copy begin/end calls carry the ticket. */
#pragma once
#define RPK_REQUEST_LIMIT 4096u
extern int runtime_memory_debug_stack_read(uintptr_t,void *,uint64_t);
static unsigned rpk_request_target,rpk_request_seen,rpk_request_active,rpk_request_calls,rpk_request_completed;
static int rpk_request_debug;
static RpkDebugRead rpk_request_read;
static unsigned char rpk_request_original[8];
typedef struct { int (*attached)(void); void (*pause)(const RpkRequestTicket *,uintptr_t); } RpkRequestOps;
static void rpk_request_init(void) {
    const char *value=BB_DIAGNOSTIC_ENV("BB_RPK_REQUEST");
    rpk_request_target=rpk_trace_config.prefix.id ? rpk_debug_target(value):0;
    const char *debug=BB_DIAGNOSTIC_ENV("BB_RPK_DEBUG_REQUEST");
    rpk_request_debug=debug && !strcmp(debug,"1");
    if(value && !rpk_request_target) fputs("RpkRequest: invalid selection or missing prefix; disabled (use 1 or 2)\n",stderr);
    if(rpk_request_target) fprintf(stderr,"RpkRequest: enabled selected=%u; one exact 8-byte read-start copy, 4096 HLE-call budget; debug=%d\n",rpk_request_target,rpk_request_debug);
}
static void rpk_request_seed_impl(const RpkDebugRead *r,const char *header,const char *prefix) {
    if(!BB_DIAGNOSTICS || !rpk_request_target || strcmp(header,"verified") || strcmp(prefix,"verified")) return;
    if(__atomic_load_n(&rpk_request_seen,__ATOMIC_RELAXED)>=rpk_request_target) return;
    if(__atomic_fetch_add(&rpk_request_seen,1,__ATOMIC_RELAXED)+1!=rpk_request_target) return;
    if(r->read_bytes<8 || r->read_bytes>UINTPTR_MAX-r->read_base ||
       !runtime_memory_read_backing(r->read_base,rpk_request_original,8)) {
        fputs("RpkRequest: selected read-start unreadable/invalid; consumed without arming\n",stderr); return;
    }
    rpk_request_read=*r;
    fprintf(stderr,"RpkRequest: armed selected=%u context-resource=%016llx source=0x%llx file-offset=%llu bytes=8 vertex-prefix=0x%llx; request bytes are NOT the vertex signature\n",
        rpk_request_target,(unsigned long long)r->resource,(unsigned long long)r->read_base,
        (unsigned long long)r->file_offset,(unsigned long long)r->prefix);
    __atomic_store_n(&rpk_request_active,1,__ATOMIC_RELEASE);
}
static void rpk_request_seed(const RpkDebugRead *r,const char *header,const char *prefix) {
    const int saved=errno;
#ifdef _WIN32
    const DWORD error=GetLastError();
#endif
    rpk_request_seed_impl(r,header,prefix);
#ifdef _WIN32
    SetLastError(error);
#endif
    errno=saved;
}
static RpkRequestTicket rpk_request_begin(uintptr_t source,uintptr_t destination,uint64_t bytes) {
    RpkRequestTicket t={0};
    if(!BB_DIAGNOSTICS || !__atomic_load_n(&rpk_request_active,__ATOMIC_ACQUIRE)) return t;
    if(__atomic_fetch_add(&rpk_request_calls,1,__ATOMIC_RELAXED)>=RPK_REQUEST_LIMIT) {
        if(__atomic_exchange_n(&rpk_request_active,0,__ATOMIC_ACQ_REL)) fputs("RpkRequest: call budget exhausted; request not identified\n",stderr);
        return t;
    }
    if(source!=rpk_request_read.read_base || bytes!=8 || !destination || bytes>UINTPTR_MAX-destination ||
       (destination<source+8 && source<destination+8)) return t;
    if(!__atomic_exchange_n(&rpk_request_active,0,__ATOMIC_ACQ_REL)) return t;
    unsigned sample=0;
    const int original=rpk_copy_verify(rpk_request_read.prefix,&sample);
    const int readable=runtime_memory_read_backing(source,t.before,8);
    if(original!=RPK_COPY_MATCH || !readable || memcmp(t.before,rpk_request_original,8)) {
        fprintf(stderr,"RpkRequest: rejected before-copy vertex-prefix=%s read-start=%s; consumed, no copy claim\n",
            rpk_copy_reason(original),!readable ? "unreadable":memcmp(t.before,rpk_request_original,8) ? "changed":"matches-seed"); return t;
    }
    t.source=source; t.destination=destination; t.valid=1; return t;
}
static void rpk_request_pause(const RpkRequestTicket *t,uintptr_t caller) {
#ifdef _WIN32
    char line[320];
    snprintf(line,sizeof line,"RpkRequest: STOP AFTER verified 8-byte copy source=0x%llx destination=0x%llx caller-return=0x%llx vertex-prefix=0x%llx; request bytes only, NOT vertex transfer\n",
        (unsigned long long)t->source,(unsigned long long)t->destination,(unsigned long long)caller,
        (unsigned long long)rpk_request_read.prefix);
    fputs(line,stderr); fflush(stderr); OutputDebugStringA(line); DebugBreak();
#else
    (void)t; (void)caller;
#endif
}
static int rpk_request_end(const RpkRequestTicket *t,uintptr_t caller,const RpkRequestOps *ops) {
    if(!BB_DIAGNOSTICS || !t->valid || __atomic_exchange_n(&rpk_request_completed,1,__ATOMIC_ACQ_REL)) return 0;
    unsigned char after[8]={0};
    int readable=runtime_memory_debug_read(t->destination,after,8);
    const char *path="guest-mapping";
    if(!readable) { readable=runtime_memory_debug_stack_read(t->destination,after,8); path="current-thread-stack"; }
    const int match=readable && !memcmp(t->before,after,8);
    unsigned sample=0;
    const int fresh=rpk_copy_verify(rpk_request_read.prefix,&sample);
    fprintf(stderr,"RpkRequest: AFTER source=0x%llx destination=0x%llx bytes=8 caller-return=0x%llx destination=%s read-path=%s vertex-prefix-now=%s before=",
        (unsigned long long)t->source,(unsigned long long)t->destination,(unsigned long long)caller,
        !readable ? "unreadable":match ? "verified":"mismatch",path,rpk_copy_reason(fresh));
    for(unsigned i=0;i<8;++i) fprintf(stderr,"%02x",t->before[i]);
    fputs(" after=",stderr);
    if(readable) for(unsigned i=0;i<8;++i) fprintf(stderr,"%02x",after[i]); else fputs("unreadable",stderr);
    fputs("; scope=read-start-request-not-vertex; no allocation lifetime claim\n",stderr);
    if(match && fresh==RPK_COPY_MATCH && rpk_request_debug && caller) {
        if(ops->attached()) { ops->pause(t,caller); fputs("RpkRequest: resumed; observation/stop consumed\n",stderr); }
        else fputs("RpkRequest: no attached Windows debugger; stop consumed\n",stderr);
    }
    return match;
}
