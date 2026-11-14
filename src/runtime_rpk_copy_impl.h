/* Included only by runtime_file.c's trace implementation and the standalone tests.
 * Metadata lock is never held while reading guest backing or performing game copies. */
#pragma once
#include "runtime_rpk_copy.h"
#define RPK_COPY_CALL_LIMIT 262144u
static pthread_mutex_t rpk_copy_lock=PTHREAD_MUTEX_INITIALIZER;
static uintptr_t rpk_copy_seeds[16];
static unsigned rpk_copy_count,rpk_copy_active,rpk_copy_calls,rpk_copy_logs,rpk_copy_rejects;
static unsigned rpk_copy_ready,rpk_copy_queries;
static int rpk_copy_catalog_full;
static int rpk_copy_enabled;
static void rpk_copy_seed(uintptr_t address) {
    if (!rpk_copy_enabled || __atomic_load_n(&rpk_copy_calls,__ATOMIC_RELAXED)>=RPK_COPY_CALL_LIMIT ||
        __atomic_load_n(&rpk_copy_logs,__ATOMIC_RELAXED)>=64) return;
    pthread_mutex_lock(&rpk_copy_lock);
    int known=0;
    for(unsigned i=0;i<rpk_copy_count;++i) known|=rpk_copy_seeds[i]==address;
    const int added=!known && rpk_copy_count<16;
    if(added) rpk_copy_seeds[rpk_copy_count++]=address;
    const int full=!known && !added && !rpk_copy_catalog_full;
    if(full) rpk_copy_catalog_full=1;
    pthread_mutex_unlock(&rpk_copy_lock);
    __atomic_store_n(&rpk_copy_active,1,__ATOMIC_RELEASE);
    __atomic_store_n(&rpk_copy_ready,1,__ATOMIC_RELEASE);
    if(added) fprintf(stderr,"RpkCopy: seeded verified prefix=0x%llx; metadata only, allocation lifetime unknown\n",(unsigned long long)address);
    if(full) fputs("RpkCopy: address catalog full (16); further verified destinations are logged but not retained\n",stderr);
}
static int rpk_copy_verify(uintptr_t address) {
    const RpkTracePrefix *p=&rpk_trace_config.prefix;
    const uint64_t span=(p->samples-1)*p->stride+p->width;
    if(span>UINTPTR_MAX-address) return 0;
    unsigned char row[12];
    for(uint64_t i=0;i<p->samples;++i)
        if(!runtime_memory_read_backing(address+i*p->stride,row,p->width) ||
            memcmp(row,p->bytes+i*p->width,(size_t)p->width)) return 0;
    return 1;
}
int runtime_rpk_copy_begin(uintptr_t source,uintptr_t destination,uint64_t bytes,RpkCopyTicket *ticket) {
    ticket->valid=0;
    if(!__atomic_load_n(&rpk_copy_active,__ATOMIC_ACQUIRE)) return 0;
    const unsigned call=__atomic_fetch_add(&rpk_copy_calls,1,__ATOMIC_RELAXED);
    if(call==0 || call==65535) fprintf(stderr,"RpkCopy: HLE copy calls observed=%u candidate-checks=%u\n",call+1,
        __atomic_load_n(&rpk_copy_logs,__ATOMIC_RELAXED));
    if(call>=RPK_COPY_CALL_LIMIT) {
        __atomic_store_n(&rpk_copy_active,0,__ATOMIC_RELEASE);
        if(call==RPK_COPY_CALL_LIMIT) {
            fprintf(stderr,"RpkCopy: call budget exhausted calls=%u events=%u source-rejects=%u; inline/native/GPU copies unchecked\n",
                call,__atomic_load_n(&rpk_copy_logs,__ATOMIC_RELAXED),__atomic_load_n(&rpk_copy_rejects,__ATOMIC_RELAXED));
        }
        return 0;
    }
    if(!bytes || bytes>UINTPTR_MAX-source || bytes>UINTPTR_MAX-destination) return 0;
    uintptr_t seeds[16]; unsigned count;
    pthread_mutex_lock(&rpk_copy_lock);
    count=rpk_copy_count; memcpy(seeds,rpk_copy_seeds,count*sizeof(*seeds));
    pthread_mutex_unlock(&rpk_copy_lock);
    const RpkTracePrefix *p=&rpk_trace_config.prefix;
    const uint64_t span=(p->samples-1)*p->stride+p->width;
    for(unsigned i=0;i<count;++i) {
        if(seeds[i]<source || seeds[i]-source>bytes || span>bytes-(seeds[i]-source)) continue;
        // Bound expensive source checks even when a stale seed is copied repeatedly.
        const unsigned event=__atomic_fetch_add(&rpk_copy_logs,1,__ATOMIC_RELAXED);
        if(event>=64) {
            if(event==64) fputs("RpkCopy: verification budget exhausted (64 candidate copies)\n",stderr);
            __atomic_store_n(&rpk_copy_active,0,__ATOMIC_RELEASE); return 0;
        }
        if(!rpk_copy_verify(seeds[i])) {
            __atomic_fetch_add(&rpk_copy_rejects,1,__ATOMIC_RELAXED); continue;
        }
        *ticket=(RpkCopyTicket){seeds[i],destination+(seeds[i]-source),bytes,p->id,1};
        return 1;
    }
    return 0;
}
void runtime_rpk_copy_end(const RpkCopyTicket *ticket) {
    if(!ticket->valid) return;
    const int match=rpk_copy_verify(ticket->destination);
    fprintf(stderr,"RpkCopy: id=%016llx source-prefix=0x%llx destination-prefix=0x%llx copy-bytes=%llu destination=%s; observed HLE copy, not rendering\n",
        (unsigned long long)ticket->id,(unsigned long long)ticket->source,(unsigned long long)ticket->destination,
        (unsigned long long)ticket->bytes,match ? "verified":"mismatch-or-unreadable");
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
        const uintptr_t at=rpk_copy_seeds[i-1];
        if(at>=address && at-address<=bytes && span<=bytes-(at-address)) {
            *prefix=at; *id=p->id; found=1; break;
        }
    }
    pthread_mutex_unlock(&rpk_copy_lock);
    return found; // Historical metadata only: caller MUST freshly validate the bytes.
}
