#define _GNU_SOURCE
#include "../src/runtime_rpk_trace.h"
#include <assert.h>
static unsigned char memory[1024],stack_bytes[8];
static unsigned reads,pauses,checks,stack_reads;
static int fail,attached;
static const RpkDebugRead event={UINT64_C(0x0003a8908986f62b),128,256,0x1000,0x1040,16,12,3};
static const uintptr_t caller=UINT64_C(0x800591f1e);
int runtime_memory_read_backing(uintptr_t at,void *out,uint64_t n) {
    ++reads; errno=ERANGE;
    assert(at>=0x1000 && n<=sizeof memory && at-0x1000<=sizeof memory-n);
    if(fail) return 0;
    memcpy(out,memory+at-0x1000,(size_t)n); return 1;
}
int runtime_memory_debug_read(uintptr_t at,void *out,uint64_t n) {
    if(at==0x500) return 0;
    return runtime_memory_read_backing(at,out,n);
}
int runtime_memory_debug_stack_read(uintptr_t at,void *out,uint64_t n) {
    ++stack_reads; assert(at==0x500 && n==8);
    if(fail) return 0;
    memcpy(out,stack_bytes,8); return 1;
}
#ifdef _WIN32
uintptr_t runtime_memory_debug_backing_address(uintptr_t a,uint64_t n) { (void)a; (void)n; return 0; }
#endif
static int debugger(void) { ++checks; return attached; }
static void pause_copy(const RpkRequestTicket *t,uintptr_t pc) { assert(t->valid && pc==caller); ++pauses; }
static const RpkRequestOps ops={debugger,pause_copy};
static void reset(void) {
    rpk_request_target=2; rpk_request_seen=rpk_request_active=rpk_request_calls=rpk_request_completed=0;
    reads=pauses=checks=stack_reads=0; fail=0; attached=1; rpk_request_debug=1;
    memset(memory,0x11,sizeof memory); memset(stack_bytes,0,sizeof stack_bytes);
    rpk_trace_config.prefix=(RpkTracePrefix){event.resource,64,16,12,3,{0}};
    memset(rpk_trace_config.prefix.bytes,0x11,36);
}
static void seed(void) { rpk_request_seed(&event,"verified","verified"); }
static RpkRequestTicket begin(uintptr_t src,uintptr_t dst,uint64_t n) { return rpk_request_begin(src,dst,n); }
static void *claim_request(void *out) {
    *(RpkRequestTicket *)out=begin(0x1000,0x1200,8);
    return NULL;
}
static void already_initialized(void) {}
int main(void) {
    reset(); RpkRequestTicket t=begin(0x1000,0x1200,8); assert(!t.valid && !reads);
    if(!BB_DIAGNOSTICS) { seed(); seed(); assert(!begin(0x1000,0x1200,8).valid && !reads); puts("Request OFF gate passed"); return 0; }
    rpk_request_seed(&event,"partial","verified"); assert(!rpk_request_seen);
    seed(); assert(!rpk_request_active); seed(); assert(rpk_request_active && reads==1);
    unsigned before=reads;
    assert(!begin(0x1001,0x1200,8).valid); assert(!begin(0x1000,0x1200,7).valid);
    assert(!begin(0x2000,0x1200,8).valid); // a different VA is not assumed to be an alias
    assert(!begin(0x1000,0x1001,8).valid); assert(!begin(0x1000,UINTPTR_MAX-3,8).valid);
    assert(!begin(0x1000,0,8).valid && reads==before);
    t=begin(0x1000,0x1200,8); assert(t.valid); memcpy(memory+512,memory,8);
    assert(rpk_request_end(&t,caller,&ops) && pauses==1 && checks==1);
    assert(!rpk_request_end(&t,caller,&ops) && pauses==1); seed(); assert(!rpk_request_active);
    reset(); seed(); seed(); memory[0]=0x22; assert(!begin(0x1000,0x1200,8).valid && !rpk_request_active);
    reset(); seed(); seed(); memory[64+2*16]=0x22; assert(!begin(0x1000,0x1200,8).valid); // last XYZ sample
    reset(); seed(); seed(); fail=1; assert(!begin(0x1000,0x1200,8).valid);
    reset(); seed(); seed(); t=begin(0x1000,0x1200,8); memory[512]=0x22;
    assert(!rpk_request_end(&t,caller,&ops) && !pauses);
    reset(); seed(); seed(); t=begin(0x1000,0x1200,8); memcpy(memory+512,memory,8); memory[64]=0x22;
    assert(rpk_request_end(&t,caller,&ops) && !pauses); // request copied, context now stale: no stop
    reset(); seed(); seed(); t=begin(0x1000,0x500,8); memcpy(stack_bytes,memory,8);
    assert(rpk_request_end(&t,caller,&ops) && stack_reads==1 && pauses==1);
    reset(); seed(); seed(); t=begin(0x1000,0x500,8); fail=1;
    assert(!rpk_request_end(&t,caller,&ops) && !pauses);
    reset(); seed(); seed(); t=begin(0x1000,0x1200,8); attached=0;
    assert(rpk_request_end(&t,caller,&ops) && !pauses && checks==1);
    reset(); seed(); seed(); rpk_request_calls=RPK_REQUEST_LIMIT; before=reads;
    assert(!begin(0x1000,0x1200,8).valid && reads==before && !rpk_request_active);
    reset(); fail=1; seed(); seed(); assert(!rpk_request_active); // failed seed consumed
    reset(); RpkDebugRead malformed=event; malformed.read_bytes=7;
    rpk_request_seed(&malformed,"verified","verified"); rpk_request_seed(&malformed,"verified","verified");
    assert(!rpk_request_active && !reads);
    reset(); malformed=event; malformed.read_base=UINTPTR_MAX-3;
    rpk_request_seed(&malformed,"verified","verified"); rpk_request_seed(&malformed,"verified","verified");
    assert(!rpk_request_active && !reads);
    reset(); seed(); seed();
    pthread_t workers[2]; RpkRequestTicket claimed[2]={{0},{0}};
    for(unsigned i=0;i<2;++i) assert(!pthread_create(&workers[i],NULL,claim_request,&claimed[i]));
    for(unsigned i=0;i<2;++i) assert(!pthread_join(workers[i],NULL));
    assert(claimed[0].valid+claimed[1].valid==1 && !rpk_request_active);
    memcpy(memory+512,memory,8);
    assert(rpk_request_end(&claimed[claimed[0].valid ? 0:1],caller,&ops) && pauses==1);
    // Real trace callback and public copy ticket: shared scope but independent of vertex-copy compatibility.
    reset(); pthread_once(&rpk_trace_once,already_initialized);
    strcpy(rpk_trace_config.path,"/app0/test.rpk"); rpk_trace_config.count=1;
    rpk_trace_config.spans[0]=(RpkTraceSpan){event.resource,128,256};
    memcpy(memory,"EVOSLITL",8); memcpy(memory+8,&event.resource,8);
    rpk_trace_note("/app0/test.rpk",128,0x1000,256,"synthetic-read"); assert(!rpk_request_active);
    rpk_trace_note("/app0/test.rpk",128,0x1000,256,"synthetic-read"); assert(rpk_request_active);
    RpkCopyTicket copy; errno=EDOM; rpk_request_debug=0;
    assert(!runtime_rpk_copy_begin(0x1000,0x1200,8,&copy) && copy.request.valid && errno==EDOM);
    copy.caller=caller; memcpy(memory+512,memory,8); runtime_rpk_copy_end(&copy);
    assert(errno==EDOM && rpk_request_completed && !rpk_request_active);
    puts("Request: selected read, exact range, alias refusal, fresh source/context, post-copy bytes, stack fallback, one-shot, failure/budget and shared-copy integration passed");
}
