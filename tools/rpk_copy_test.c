#define _GNU_SOURCE
#include "../src/runtime_rpk_trace.h"
#include <assert.h>
static unsigned char memory[2048];
static unsigned reads;
int runtime_memory_read_backing(uintptr_t address,void *out,uint64_t size) {
    ++reads;
    if(address<0x1000 || address-0x1000>sizeof memory || size>sizeof memory-(address-0x1000)) return 0;
    memcpy(out,memory+address-0x1000,(size_t)size); return 1;
}
int main(void) {
    // Exercise configuration gating without opening an external manifest.
    assert(!rpk_trace_watches("/app0/not-configured"));
    rpk_trace_note("/app0/not-configured",0,0x1000,1,"pread");
    RpkCopyTicket ticket;
    assert(!runtime_rpk_copy_begin(0x1000,0x1100,100,&ticket) && reads==0);
    RpkTracePrefix *p=&rpk_trace_config.prefix;
    p->id=UINT64_C(0x0003000000000001); p->offset=32; p->stride=16; p->width=12; p->samples=3;
    for(unsigned i=0;i<36;++i) p->bytes[i]=(unsigned char)(i+1);
    for(unsigned i=0;i<3;++i) memcpy(memory+32+i*16,p->bytes+i*12,12);
    rpk_copy_enabled=1;
    strcpy(rpk_trace_config.path,"/app0/test.rpk"); rpk_trace_config.count=1;
    rpk_trace_config.spans[0]=(RpkTraceSpan){p->id,0,128};
    rpk_trace_note("/app0/test.rpk",0,0x1000,128,"pread");
    assert(!rpk_copy_count); // A contradictory resource header must not seed provenance.
    memcpy(memory,"EVOSLITL",8); memcpy(memory+8,&p->id,8);
    rpk_trace_note("/app0/test.rpk",0,0x1000,128,"pread"); assert(rpk_copy_count==1);
    unsigned before=reads;
    assert(!runtime_rpk_copy_begin(UINTPTR_MAX-1,0x1200,8,&ticket) && reads==before);
    assert(!runtime_rpk_copy_begin(0x1020,0x1200,43,&ticket) && reads==before);
    unsigned char unchanged[2048]; memcpy(unchanged,memory,sizeof memory);
    assert(runtime_rpk_copy_begin(0x1000,0x1100,128,&ticket));
    assert(!memcmp(unchanged,memory,sizeof memory)); // tracer never performs the copy
    memcpy(memory+256,memory,128); // the game operation between begin/end
    runtime_rpk_copy_end(&ticket); assert(rpk_copy_count==2);
    assert(ticket.source==0x1020 && ticket.destination==0x1120);
    uintptr_t found; uint64_t id;
    assert(runtime_rpk_copy_find(0x1100,128,&found,&id) && found==0x1120 && id==p->id);
    assert(!runtime_rpk_copy_find(0x1100,40,&found,&id));
    rpk_copy_queries=4096;
    assert(!runtime_rpk_copy_find(0x1100,128,&found,&id));
    rpk_copy_queries=0;
    assert(runtime_rpk_copy_begin(0x1120,0x1320,44,&ticket));
    memcpy(memory+800,memory+288,44); runtime_rpk_copy_end(&ticket); assert(rpk_copy_count==3);
    // Overlapping memmove uses a pre-copy check and validates the actual result afterward.
    assert(runtime_rpk_copy_begin(0x1320,0x1324,44,&ticket));
    memmove(memory+804,memory+800,44); runtime_rpk_copy_end(&ticket); assert(rpk_copy_count==4);
    memory[32]^=1;
    assert(!runtime_rpk_copy_begin(0x1000,0x1500,128,&ticket) && rpk_copy_rejects);
    assert(runtime_rpk_copy_begin(0x1120,0x1600,44,&ticket));
    before=rpk_copy_count; runtime_rpk_copy_end(&ticket); assert(rpk_copy_count==before); // no matching write
    rpk_copy_calls=RPK_COPY_CALL_LIMIT; before=reads;
    assert(!runtime_rpk_copy_begin(0x1120,0x1600,44,&ticket) && reads==before && !rpk_copy_active);
    rpk_copy_seed(0x1800); assert(!rpk_copy_active); // no rearming exhausted budget
    // Verification budget independent from cheap range checks.
    rpk_copy_calls=0; rpk_copy_logs=64; rpk_copy_active=1;
    assert(!runtime_rpk_copy_begin(0x1120,0x1600,44,&ticket) && reads==before && !rpk_copy_active);
    puts("RPK copy: exact source/destination, chaining, overlap, stale bytes, failed copy, range and budgets passed");
}
