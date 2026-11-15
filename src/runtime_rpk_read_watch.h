/* One host-read boundary after one fully verified RPK read. No file or guest writes. */
#pragma once
#define RPK_READ_WATCH_LIMIT 4096u
static unsigned rpk_read_target,rpk_read_seen,rpk_read_active,rpk_read_calls;
static RpkDebugRead rpk_read_seed;
typedef uintptr_t (*RpkReadAlias)(uintptr_t,uint64_t);
typedef struct {
    int valid,before_match,before_read,alias_overlap;
    uintptr_t destination,alias;
    uint64_t requested,covered_at;
    unsigned char before[12];
    RpkReadAlias map;
} RpkReadTicket;
typedef struct { int status,first_read; unsigned char first[12]; } RpkReadSnapshot;
static inline RpkReadSnapshot rpk_read_snapshot(uintptr_t address) {
    RpkReadSnapshot s={RPK_COPY_BOUNDS,0,{0}};
    const RpkTracePrefix *p=&rpk_trace_config.prefix;
    const uint64_t span=(p->samples-1)*p->stride+p->width;
    if(span>UINTPTR_MAX-address) return s;
    for(uint64_t i=0;i<p->samples;++i) {
        unsigned char row[12];
        if(!runtime_memory_read_backing(address+i*p->stride,row,p->width)) { s.status=RPK_COPY_UNREADABLE; return s; }
        if(!i) { s.first_read=1; memcpy(s.first,row,(size_t)p->width); }
        if(memcmp(row,p->bytes+i*p->width,(size_t)p->width)) { s.status=RPK_COPY_MISMATCH; return s; }
    }
    s.status=RPK_COPY_MATCH; return s; // bounded samples, not an atomic memory snapshot
}
static inline int rpk_read_intersects(uintptr_t a,uint64_t n,uintptr_t b,uint64_t m) {
    return n && m && n<=UINTPTR_MAX-a && m<=UINTPTR_MAX-b && a<b+m && b<a+n;
}
static inline void rpk_read_watch_init(void) {
    const char *value=BB_DIAGNOSTIC_ENV("BB_RPK_READ_WATCH");
    rpk_read_target=rpk_trace_config.prefix.id ? rpk_debug_target(value):0;
    if(value && !rpk_read_target) fputs("RpkReadWatch: invalid selection or missing prefix; disabled (use 1 or 2)\n",stderr);
    if(rpk_read_target) fprintf(stderr,"RpkReadWatch: enabled selected=%u; first overlapping host read, 4096 read-call budget, no debugger stop\n",rpk_read_target);
}
static inline void rpk_read_watch_seed(const RpkDebugRead *r,const char *header,const char *prefix) {
    if(!BB_DIAGNOSTICS || !rpk_read_target || strcmp(header,"verified") || strcmp(prefix,"verified")) return;
    if(__atomic_load_n(&rpk_read_seen,__ATOMIC_RELAXED)>=rpk_read_target) return;
    if(__atomic_fetch_add(&rpk_read_seen,1,__ATOMIC_RELAXED)+1!=rpk_read_target) return;
    rpk_read_seed=*r;
    fprintf(stderr,"RpkReadWatch: armed selected=%u id=%016llx prefix=0x%llx original-file-offset=%llu\n",rpk_read_target,
        (unsigned long long)r->resource,(unsigned long long)r->prefix,(unsigned long long)r->file_offset);
    __atomic_store_n(&rpk_read_active,1,__ATOMIC_RELEASE);
}
static inline RpkReadTicket rpk_read_watch_begin(uintptr_t dst,uint64_t bytes,RpkReadAlias map) {
    RpkReadTicket t={0};
    if(!BB_DIAGNOSTICS || !__atomic_load_n(&rpk_read_active,__ATOMIC_ACQUIRE)) return t;
    if(__atomic_fetch_add(&rpk_read_calls,1,__ATOMIC_RELAXED)>=RPK_READ_WATCH_LIMIT) {
        const int saved=errno;
#ifdef _WIN32
        const DWORD error=GetLastError();
#endif
        if(__atomic_exchange_n(&rpk_read_active,0,__ATOMIC_ACQ_REL)) fputs("RpkReadWatch: call budget exhausted; other writes unchecked\n",stderr);
        errno=saved;
#ifdef _WIN32
        SetLastError(error);
#endif
        return t;
    }
    if(!bytes || bytes>UINTPTR_MAX-dst) return t;
    const int saved_errno=errno;
#ifdef _WIN32
    const DWORD saved_error=GetLastError();
#endif
    const RpkDebugRead *r=&rpk_read_seed;
    const uint64_t span=(r->samples-1)*r->stride+r->width; // validated manifest
    const uintptr_t alias=map(r->prefix,span), target=map(dst,bytes);
    int overlap=0;
    uint64_t covered_at=UINT64_MAX;
    for(uint64_t i=0;i<r->samples;++i) {
        const uintptr_t row=r->prefix+i*r->stride;
        if(rpk_read_intersects(dst,bytes,row,r->width)) {
            overlap=1;
            const uint64_t at=row>dst ? row-dst:0;
            if(at<covered_at) covered_at=at;
        } else if(alias && target && rpk_read_intersects(target,bytes,alias+i*r->stride,r->width)) {
            overlap=1; t.alias_overlap=1;
            const uint64_t at=alias+i*r->stride>target ? alias+i*r->stride-target:0;
            if(at<covered_at) covered_at=at;
        }
    }
    if(overlap && __atomic_exchange_n(&rpk_read_active,0,__ATOMIC_ACQ_REL)) {
        t.valid=1; t.destination=dst; t.requested=bytes; t.alias=alias; t.covered_at=covered_at; t.map=map;
        const RpkReadSnapshot before=rpk_read_snapshot(r->prefix);
        t.before_match=before.status==RPK_COPY_MATCH; t.before_read=before.first_read;
        memcpy(t.before,before.first,sizeof t.before);
    }
#ifdef _WIN32
    SetLastError(saved_error);
#endif
    errno=saved_errno;
    return t;
}
static inline int rpk_read_watch_end(const RpkReadTicket *t,int64_t actual,const char *path,int64_t offset) {
    if(!t->valid) return 0;
    const int saved_errno=errno;
#ifdef _WIN32
    const DWORD saved_error=GetLastError();
#endif
    const RpkDebugRead *r=&rpk_read_seed;
    const RpkReadSnapshot after=rpk_read_snapshot(r->prefix);
    const int after_status=after.status;
    const int after_match=after_status==RPK_COPY_MATCH;
    const uintptr_t alias=t->map(r->prefix,(r->samples-1)*r->stride+r->width);
    const int stable=alias && alias==t->alias;
    const int covers=actual>0 && (uint64_t)actual<=t->requested && (uint64_t)actual>t->covered_at;
    fprintf(stderr,"RpkReadWatch: AFTER id=%016llx path=%s offset=%lld destination=0x%llx requested=%llu actual=%lld prefix=0x%llx before-match=%d after-match=%d after-status=%s alias-overlap=%d mapping-same=%d returned-overlap=%d before-first=",
        (unsigned long long)r->resource,path,(long long)offset,(unsigned long long)t->destination,
        (unsigned long long)t->requested,(long long)actual,(unsigned long long)r->prefix,
        t->before_match,after_match,rpk_copy_reason(after_status),t->alias_overlap,stable,covers);
    if(t->before_read) for(uint64_t i=0;i<r->width;++i) fprintf(stderr,"%02x",t->before[i]); else fputs("unreadable",stderr);
    fputs(" after-first=",stderr);
    if(after.first_read) for(uint64_t i=0;i<r->width;++i) fprintf(stderr,"%02x",after.first[i]); else fputs("unreadable",stderr);
    const int changed=t->before_match && after_status==RPK_COPY_MISMATCH && stable && covers;
    fprintf(stderr,"; changed-across-read=%d; concurrent/uninstrumented writers not excluded, observation consumed\n",changed);
#ifdef _WIN32
    SetLastError(saved_error);
#endif
    errno=saved_errno;
    return changed;
}
