/* Included only by runtime_file.c's trace implementation and the standalone tests.
 * Metadata lock is never held while reading guest backing or performing game copies. */
#pragma once
#include "runtime_rpk_copy.h"
#define RPK_COPY_CALL_LIMIT 262144u
static pthread_mutex_t rpk_copy_lock=PTHREAD_MUTEX_INITIALIZER;
typedef struct { uintptr_t address; unsigned generation; int live, busy; } RpkCopySeed;
static RpkCopySeed rpk_copy_seeds[16];
static unsigned rpk_copy_count,rpk_copy_active,rpk_copy_calls,rpk_copy_logs,rpk_copy_rejects;
static unsigned rpk_copy_ready,rpk_copy_queries,rpk_copy_partial,rpk_copy_retired,rpk_copy_generation;
static int rpk_copy_catalog_full;
static int rpk_copy_enabled;
static unsigned rpk_copy_read_target,rpk_copy_read_seen;
static void rpk_copy_seed(uintptr_t address) {
    if (!rpk_copy_enabled || __atomic_load_n(&rpk_copy_calls,__ATOMIC_RELAXED)>=RPK_COPY_CALL_LIMIT ||
        __atomic_load_n(&rpk_copy_logs,__ATOMIC_RELAXED)>=64) return;
    pthread_mutex_lock(&rpk_copy_lock);
    unsigned at=rpk_copy_count;
    for(unsigned i=0;i<rpk_copy_count;++i) if(rpk_copy_seeds[i].address==address) { at=i; break; }
    const int added=at==rpk_copy_count && rpk_copy_count<16;
    if(added) ++rpk_copy_count;
    const int retained=at<rpk_copy_count;
    unsigned generation=0;
    if(retained) {
        generation=++rpk_copy_generation;
        rpk_copy_seeds[at]=(RpkCopySeed){address,generation,1,0};
    }
    const int full=!retained && !rpk_copy_catalog_full;
    if(full) rpk_copy_catalog_full=1;
    if(retained) {
        __atomic_store_n(&rpk_copy_active,1,__ATOMIC_RELEASE);
        __atomic_store_n(&rpk_copy_ready,1,__ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&rpk_copy_lock);
    if(retained) fprintf(stderr,"RpkCopy: seeded verified prefix=0x%llx generation=%u; metadata only, allocation lifetime unknown\n",(unsigned long long)address,generation);
    if(full) fputs("RpkCopy: address catalog full (16); further verified destinations are logged but not retained\n",stderr);
}
/* Optional read ordinal narrows initial seeds; verified copy destinations still chain. */
static void rpk_copy_read_seed(uintptr_t address,const char *header,const char *prefix) {
    if(!rpk_copy_enabled || strcmp(prefix,"verified")) return;
    if(rpk_copy_read_target) {
        if(strcmp(header,"verified") || __atomic_load_n(&rpk_copy_read_seen,__ATOMIC_RELAXED)>=rpk_copy_read_target) return;
        if(__atomic_fetch_add(&rpk_copy_read_seen,1,__ATOMIC_RELAXED)+1!=rpk_copy_read_target) return;
    } else if(strcmp(header,"verified") && strcmp(header,"partial")) return;
    rpk_copy_seed(address);
}
enum { RPK_COPY_MATCH, RPK_COPY_MISMATCH, RPK_COPY_UNREADABLE, RPK_COPY_BOUNDS };
static const char *rpk_copy_reason(int result) {
    const char *names[]={"verified","prefix-mismatch","backing-unreadable","bounds"};
    return names[result];
}
static int rpk_copy_verify(uintptr_t address,unsigned *sample) {
    const RpkTracePrefix *p=&rpk_trace_config.prefix;
    const uint64_t span=(p->samples-1)*p->stride+p->width;
    *sample=0;
    if(span>UINTPTR_MAX-address) return RPK_COPY_BOUNDS;
    unsigned char row[12];
    for(uint64_t i=0;i<p->samples;++i) {
        *sample=(unsigned)i;
        if(!runtime_memory_read_backing(address+i*p->stride,row,p->width)) return RPK_COPY_UNREADABLE;
        if(memcmp(row,p->bytes+i*p->width,(size_t)p->width)) return RPK_COPY_MISMATCH;
    }
    return RPK_COPY_MATCH;
}
#include "runtime_rpk_request_impl.h"
/* A failed old snapshot must not retire a newer independently verified read. */
static int rpk_copy_release(unsigned slot,unsigned generation,int retire) {
    int retired=0,live=0;
    pthread_mutex_lock(&rpk_copy_lock);
    RpkCopySeed *s=&rpk_copy_seeds[slot];
    if(s->generation==generation) {
        s->busy=0;
        if(retire && s->live) { s->live=0; retired=1; }
    }
    for(unsigned i=0;i<rpk_copy_count;++i) live|=rpk_copy_seeds[i].live;
    if(!live) __atomic_store_n(&rpk_copy_active,0,__ATOMIC_RELEASE);
    pthread_mutex_unlock(&rpk_copy_lock);
    if(retired) __atomic_fetch_add(&rpk_copy_retired,1,__ATOMIC_RELAXED);
    return retired;
}
static int rpk_copy_begin_impl(uintptr_t source,uintptr_t destination,uint64_t bytes,RpkCopyTicket *ticket) {
    ticket->valid=0;
    if(!__atomic_load_n(&rpk_copy_active,__ATOMIC_ACQUIRE)) return 0;
    const unsigned call=__atomic_fetch_add(&rpk_copy_calls,1,__ATOMIC_RELAXED);
    if(call==0 || call==65535) fprintf(stderr,"RpkCopy: HLE copy calls observed=%u candidate-checks=%u\n",call+1,
        __atomic_load_n(&rpk_copy_logs,__ATOMIC_RELAXED));
    if(call>=RPK_COPY_CALL_LIMIT) {
        __atomic_store_n(&rpk_copy_active,0,__ATOMIC_RELEASE);
        if(call==RPK_COPY_CALL_LIMIT) {
            fprintf(stderr,"RpkCopy: call budget exhausted calls=%u events=%u source-rejects=%u partial-windows=%u retired=%u; inline/native/GPU copies unchecked\n",
                call,__atomic_load_n(&rpk_copy_logs,__ATOMIC_RELAXED),__atomic_load_n(&rpk_copy_rejects,__ATOMIC_RELAXED),
                __atomic_load_n(&rpk_copy_partial,__ATOMIC_RELAXED),__atomic_load_n(&rpk_copy_retired,__ATOMIC_RELAXED));
        }
        return 0;
    }
    if(!bytes || source==destination || bytes>UINTPTR_MAX-source || bytes>UINTPTR_MAX-destination) return 0;
    RpkCopySeed seeds[16]; unsigned count;
    pthread_mutex_lock(&rpk_copy_lock);
    count=rpk_copy_count; memcpy(seeds,rpk_copy_seeds,count*sizeof(*seeds));
    pthread_mutex_unlock(&rpk_copy_lock);
    const RpkTracePrefix *p=&rpk_trace_config.prefix;
    const uint64_t span=(p->samples-1)*p->stride+p->width;
    for(unsigned i=0;i<count;++i) {
        const uintptr_t at=seeds[i].address;
        if(!seeds[i].live || span>UINTPTR_MAX-at || at>=source+bytes || source>=at+span) continue;
        if(at<source || span>bytes-(at-source)) {
            const unsigned partial=__atomic_fetch_add(&rpk_copy_partial,1,__ATOMIC_RELAXED);
            if(partial<4) fprintf(stderr,"RpkCopy: partial-window source-prefix=0x%llx source=0x%llx destination=0x%llx bytes=%llu; no source verification charged\n",
                (unsigned long long)at,(unsigned long long)source,(unsigned long long)destination,(unsigned long long)bytes);
            continue;
        }
        // At most one in-flight verifier for this seed generation.
        pthread_mutex_lock(&rpk_copy_lock);
        RpkCopySeed *live=&rpk_copy_seeds[i];
        const int claimed=live->live && !live->busy && live->generation==seeds[i].generation;
        if(claimed) live->busy=1;
        pthread_mutex_unlock(&rpk_copy_lock);
        if(!claimed) continue;
        const unsigned event=__atomic_fetch_add(&rpk_copy_logs,1,__ATOMIC_RELAXED);
        if(event>=64) {
            rpk_copy_release(i,seeds[i].generation,0);
            if(event==64) fputs("RpkCopy: verification budget exhausted (64 candidate copies)\n",stderr);
            __atomic_store_n(&rpk_copy_active,0,__ATOMIC_RELEASE); return 0;
        }
        unsigned sample=0;
        const int result=rpk_copy_verify(at,&sample);
        const int retired=rpk_copy_release(i,seeds[i].generation,result!=RPK_COPY_MATCH);
        if(result!=RPK_COPY_MATCH) {
            __atomic_fetch_add(&rpk_copy_rejects,1,__ATOMIC_RELAXED);
            fprintf(stderr,"RpkCopy: source-rejected reason=%s sample=%u prefix=0x%llx generation=%u source=0x%llx destination=0x%llx bytes=%llu retired=%d; no copy claim\n",
                rpk_copy_reason(result),sample,(unsigned long long)at,seeds[i].generation,
                (unsigned long long)source,(unsigned long long)destination,(unsigned long long)bytes,retired);
            continue;
        }
        *ticket=(RpkCopyTicket){.source=at,.destination=destination+(at-source),.bytes=bytes,.id=p->id,
            .valid=1,.source_base=source,.destination_base=destination};
        return 1;
    }
    return 0;
}
static void rpk_copy_end_impl(const RpkCopyTicket *ticket) {
    if(!ticket->valid) return;
    unsigned sample=0;
    const int result=rpk_copy_verify(ticket->destination,&sample);
    const int match=result==RPK_COPY_MATCH;
    fprintf(stderr,"RpkCopy: id=%016llx source-prefix=0x%llx destination-prefix=0x%llx copy-bytes=%llu destination=%s source-before=verified source-base=0x%llx destination-base=0x%llx caller=0x%llx; verified samples only, not whole-resource ownership or rendering",
        (unsigned long long)ticket->id,(unsigned long long)ticket->source,(unsigned long long)ticket->destination,
        (unsigned long long)ticket->bytes,rpk_copy_reason(result),(unsigned long long)ticket->source_base,
        (unsigned long long)ticket->destination_base,(unsigned long long)ticket->caller);
    if(match) {
        fputs(" matched-prefix-first=",stderr);
        for(uint64_t i=0;i<rpk_trace_config.prefix.width;++i) fprintf(stderr,"%02x",rpk_trace_config.prefix.bytes[i]);
    }
    fputc('\n',stderr);
    if(match) rpk_copy_seed(ticket->destination);
}
int runtime_rpk_copy_find(uintptr_t address,uint64_t bytes,uintptr_t *prefix,uint64_t *id) {
    if(!__atomic_load_n(&rpk_copy_ready,__ATOMIC_ACQUIRE) || bytes>UINTPTR_MAX-address) return 0;
    const unsigned query=__atomic_fetch_add(&rpk_copy_queries,1,__ATOMIC_RELAXED);
    if(query>=4096) {
        if(query==4096) fputs("RpkCopy: binding query budget exhausted (4096)\n",stderr);
        return 0;
    }
    const RpkTracePrefix *p=&rpk_trace_config.prefix;
    const uint64_t span=(p->samples-1)*p->stride+p->width;
    int found=0;
    pthread_mutex_lock(&rpk_copy_lock);
    for(unsigned i=rpk_copy_count;i>0;--i) {
        if(!rpk_copy_seeds[i-1].live) continue;
        const uintptr_t at=rpk_copy_seeds[i-1].address;
        if(at>=address && at-address<=bytes && span<=bytes-(at-address)) {
            *prefix=at; *id=p->id; found=1; break;
        }
    }
    pthread_mutex_unlock(&rpk_copy_lock);
    return found; // Historical metadata only: caller MUST freshly validate the bytes.
}

/* These observers must not change errno/LastError seen by the existing copy path. */
int runtime_rpk_copy_begin(uintptr_t source,uintptr_t destination,uint64_t bytes,RpkCopyTicket *ticket) {
    const int saved=errno;
#ifdef _WIN32
    const DWORD error=GetLastError();
#endif
    const RpkRequestTicket request=rpk_request_begin(source,destination,bytes);
    const int result=rpk_copy_begin_impl(source,destination,bytes,ticket);
    ticket->request=request;
    if(!result) ticket->caller=0;
#ifdef _WIN32
    SetLastError(error);
#endif
    errno=saved; return result;
}
void runtime_rpk_copy_end(const RpkCopyTicket *ticket) {
    const int saved=errno;
#ifdef _WIN32
    const DWORD error=GetLastError();
#endif
    rpk_copy_end_impl(ticket);
    const RpkRequestOps ops={rpk_debug_attached,rpk_request_pause};
    rpk_request_end(&ticket->request,ticket->caller,&ops);
#ifdef _WIN32
    SetLastError(error);
#endif
    errno=saved;
}
