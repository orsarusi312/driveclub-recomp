/* Included once by runtime_file.c through RpkTrace, after the prefix verifier.
 * One immutable verified-read snapshot; no lock held across backing reads or debugger stops. */
#pragma once
#include "runtime_rpk_clear.h"
#define RPK_CLEAR_CALL_LIMIT 262144u
static unsigned rpk_clear_target, rpk_clear_seen, rpk_clear_active, rpk_clear_calls;
static int rpk_clear_debug;
static RpkDebugRead rpk_clear_read;
typedef struct { int (*attached)(void); void (*pause)(uintptr_t,uintptr_t,uint64_t,int,uintptr_t); } RpkClearOps;

static void rpk_clear_init(void) {
    const char *value=BB_DIAGNOSTIC_ENV("BB_RPK_CLEAR_TRACE");
    rpk_clear_target=rpk_trace_config.prefix.id ? rpk_debug_target(value):0;
    const char *debug=BB_DIAGNOSTIC_ENV("BB_RPK_DEBUG_CLEAR");
    rpk_clear_debug=debug && !strcmp(debug,"1");
    if(value && !rpk_clear_target) fputs("RpkClear: invalid selection or missing v2 prefix; disabled (use 1 or 2)\n",stderr);
    if(rpk_clear_target) fprintf(stderr,"RpkClear: enabled selected=%u; one overlapping HLE memset, %u calls after seed; guest-address coverage only; debug=%d\n",
        rpk_clear_target,RPK_CLEAR_CALL_LIMIT,rpk_clear_debug);
}
static void rpk_clear_seed(const RpkDebugRead *read,const char *header,const char *prefix) {
    if(!BB_DIAGNOSTICS || !rpk_clear_target || strcmp(header,"verified") || strcmp(prefix,"verified")) return;
    if(__atomic_load_n(&rpk_clear_seen,__ATOMIC_RELAXED)>=rpk_clear_target) return;
    if(__atomic_fetch_add(&rpk_clear_seen,1,__ATOMIC_RELAXED)+1!=rpk_clear_target) return;
    rpk_clear_read=*read;
    fprintf(stderr,"RpkClear: armed selected=%u resource=%016llx prefix=0x%llx read-base=0x%llx read-bytes=%llu file-offset=%llu; awaits HLE overwrite, not a reader\n",
        rpk_clear_target,(unsigned long long)read->resource,(unsigned long long)read->prefix,
        (unsigned long long)read->read_base,(unsigned long long)read->read_bytes,(unsigned long long)read->file_offset);
    __atomic_store_n(&rpk_clear_active,1,__ATOMIC_RELEASE);
}
static void rpk_clear_pause(uintptr_t prefix,uintptr_t dst,uint64_t bytes,int value,uintptr_t caller) {
#ifdef _WIN32
    char line[384];
    snprintf(line,sizeof line,"RpkClear: STOP BEFORE memset prefix=0x%llx destination=0x%llx bytes=%llu value=%u guest-return=0x%llx; inspect caller; no watchpoint needed\n",
        (unsigned long long)prefix,(unsigned long long)dst,(unsigned long long)bytes,
        (unsigned)(unsigned char)value,(unsigned long long)caller);
    fputs(line,stderr); fflush(stderr); OutputDebugStringA(line); DebugBreak();
#else
    (void)prefix; (void)dst; (void)bytes; (void)value; (void)caller;
#endif
}
static void rpk_clear_observe(uintptr_t dst,int value,uint64_t bytes,uintptr_t caller,const RpkClearOps *ops) {
    if(!BB_DIAGNOSTICS || !__atomic_load_n(&rpk_clear_active,__ATOMIC_ACQUIRE)) return;
    const unsigned call=__atomic_fetch_add(&rpk_clear_calls,1,__ATOMIC_RELAXED);
    if(call>=RPK_CLEAR_CALL_LIMIT) {
        if(__atomic_exchange_n(&rpk_clear_active,0,__ATOMIC_ACQ_REL))
            fputs("RpkClear: call budget exhausted; no overlapping HLE memset identified; inline/alias writes unchecked\n",stderr);
        return;
    }
    if(!bytes || bytes>UINTPTR_MAX-dst) return;
    const RpkDebugRead *r=&rpk_clear_read;
    if(!r->samples || r->samples>8 || r->stride>4096 || !r->width || r->width>12 || r->stride<r->width) return;
    const uint64_t span=(r->samples-1)*r->stride+r->width;
    if(span>UINTPTR_MAX-r->prefix) return;
    int overlaps=0;
    for(uint64_t i=0;i<r->samples;++i) {
        const uintptr_t row=r->prefix+i*r->stride;
        if(dst<row+r->width && row<dst+bytes) overlaps=1;
    }
    if(!overlaps || !__atomic_exchange_n(&rpk_clear_active,0,__ATOMIC_ACQ_REL)) return;
    unsigned sample=0;
    const int result=rpk_copy_verify(r->prefix,&sample);
    fprintf(stderr,"RpkClear: BEFORE resource=%016llx prefix=0x%llx freshness=%s sample=%u destination=0x%llx bytes=%llu value=%u caller=0x%llx calls=%u same-read-base=%d same-read-size=%d; one-shot consumed, write not yet executed\n",
        (unsigned long long)r->resource,(unsigned long long)r->prefix,rpk_copy_reason(result),sample,
        (unsigned long long)dst,(unsigned long long)bytes,(unsigned)(unsigned char)value,
        (unsigned long long)caller,call+1,dst==r->read_base,bytes==r->read_bytes);
    if(result!=RPK_COPY_MATCH || !rpk_clear_debug) return;
    if(!ops->attached()) {
        fputs("RpkClear: stop skipped: no attached Windows debugger; observation consumed\n",stderr);
        return;
    }
    ops->pause(r->prefix,dst,bytes,value,caller);
    fputs("RpkClear: resumed; original memset will proceed unchanged; stop budget exhausted\n",stderr);
}
void runtime_rpk_clear_before(uintptr_t destination,int value,uint64_t bytes,uintptr_t caller) {
    if(!BB_DIAGNOSTICS || !__atomic_load_n(&rpk_clear_active,__ATOMIC_ACQUIRE)) return;
    const int saved_errno=errno;
#ifdef _WIN32
    const DWORD saved_error=GetLastError();
#endif
    const RpkClearOps ops={rpk_debug_attached,rpk_clear_pause};
    rpk_clear_observe(destination,value,bytes,caller,&ops);
#ifdef _WIN32
    SetLastError(saved_error);
#endif
    errno=saved_errno;
}
