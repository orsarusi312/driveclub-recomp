#define _GNU_SOURCE
#include "../src/runtime_rpk_trace.h"
#include <assert.h>
static unsigned char memory[256];
static unsigned reads,alias_calls;
static int fail,remap;
static void *seed_race(void *arg);
static void already_initialized(void) {}
int runtime_memory_read_backing(uintptr_t a,void *out,uint64_t n) {
    ++reads; errno=ERANGE;
    assert(a>=0x1000 && a-0x1000+n<=sizeof memory);
    if(fail) return 0;
    memcpy(out,memory+a-0x1000,(size_t)n); return 1;
}
int runtime_memory_debug_read(uintptr_t a,void *out,uint64_t n) { return runtime_memory_read_backing(a,out,n); }
uintptr_t runtime_memory_debug_backing_address(uintptr_t a,uint64_t n) {
    ++alias_calls;
    uintptr_t base=a>=0x2000 ? 0x2000:0x1000;
    if(a<base || n>256 || a-base>256-n) return 0;
    return (remap ? 0xa000:0x9000)+(a-base);
}
static const RpkDebugRead event={UINT64_C(0x0003a8908986f62b),128,256,0x1000,0x1020,16,12,8};
static void reset(void) {
    rpk_read_target=2; rpk_read_seen=rpk_read_active=rpk_read_calls=0;
    reads=alias_calls=0; fail=remap=0; memset(memory,0x11,sizeof memory);
    rpk_trace_config.prefix=(RpkTracePrefix){event.resource,32,16,12,8,{0}};
    memset(rpk_trace_config.prefix.bytes,0x11,96);
}
static void seed(void) { rpk_read_watch_seed(&event,"verified","verified"); }
static RpkReadTicket begin(uintptr_t dst,uint64_t n) { return rpk_read_watch_begin(dst,n,runtime_memory_debug_backing_address); }
static void *seed_race(void *arg) { (void)arg; seed(); return NULL; }
int main(void) {
    reset(); assert(!begin(0x1000,256).valid && !reads && !alias_calls);
    if(!BB_DIAGNOSTICS) { seed(); seed(); assert(!begin(0x1000,256).valid && !reads); puts("Read watch OFF gate passed"); return 0; }
    rpk_read_watch_seed(&event,"partial","verified"); assert(!rpk_read_seen);
    seed(); assert(!rpk_read_active); seed(); assert(rpk_read_active);
    assert(!begin(UINTPTR_MAX-2,8).valid && !reads);
    assert(!begin(0x1000,0).valid);
    assert(!begin(0x102c,4).valid && !reads); // stride padding
    errno=EDOM; RpkReadTicket t=begin(0x1000,256); assert(t.valid && t.before_match && errno==EDOM);
    unsigned char old[256]; memcpy(old,memory,256);
    assert(!rpk_read_watch_end(&t,256,"/app0/test",123) && !memcmp(old,memory,256));
    assert(!begin(0x1000,256).valid); seed(); assert(!rpk_read_active);
    reset(); seed(); seed(); t=begin(0x2000,256); assert(t.valid && t.alias_overlap && t.covered_at==32);
    memset(memory+32,0x22,12); errno=EDOM;
    assert(rpk_read_watch_end(&t,256,"/app0/test",124) && errno==EDOM); // same physical storage, different VA
    reset(); seed(); seed(); t=begin(0x1000,256); memset(memory+32,0x22,12); remap=1;
    assert(!rpk_read_watch_end(&t,256,"/app0/test",125)); // no claim across remapping
    reset(); seed(); seed(); memset(memory+32,0x22,12); t=begin(0x1000,256); assert(!t.before_match);
    assert(!rpk_read_watch_end(&t,256,"/app0/test",126)); // already stale before read
    reset(); seed(); seed(); t=begin(0x1000,256); fail=1;
    assert(!rpk_read_watch_end(&t,256,"/app0/test",127)); // unreadability is not a mismatch
    reset(); seed(); seed(); t=begin(0x1000,256); memset(memory+32,0x22,12);
    assert(!rpk_read_watch_end(&t,-1,"/app0/test",128));
    assert(!rpk_read_watch_end(&t,0,"/app0/test",128));
    assert(!rpk_read_watch_end(&t,32,"/app0/test",128)); // short read ended before sampled bytes
    assert(!rpk_read_watch_end(&t,257,"/app0/test",128)); // malformed return count
    reset(); seed(); seed(); t=begin(0x1021,1); assert(t.valid && t.before_match);
    memory[33]=0x22; assert(rpk_read_watch_end(&t,1,"/app0/test",129));
    reset(); seed(); seed(); rpk_read_calls=RPK_READ_WATCH_LIMIT; errno=EDOM;
    assert(!begin(0x1000,256).valid && !reads && !rpk_read_active && errno==EDOM);
    reset(); pthread_t threads[8];
    for(unsigned i=0;i<8;++i) assert(!pthread_create(threads+i,NULL,seed_race,NULL));
    for(unsigned i=0;i<8;++i) assert(!pthread_join(threads[i],NULL));
    assert(rpk_read_active && rpk_read_seen>=2 && rpk_read_seen<=9);
    reset(); pthread_once(&rpk_trace_once,already_initialized);
    strcpy(rpk_trace_config.path,"/app0/test.rpk");
    rpk_trace_config.count=1; rpk_trace_config.spans[0]=(RpkTraceSpan){event.resource,128,256};
    memcpy(memory,"EVOSLITL",8);
    for(unsigned i=0;i<8;++i) memory[8+i]=(unsigned char)(event.resource>>(8*i));
    rpk_trace_note("/app0/test.rpk",128,0x1000,256,"synthetic-read"); assert(!rpk_read_active);
    rpk_trace_note("/app0/test.rpk",128,0x1000,256,"synthetic-read"); assert(rpk_read_active);
    t=begin(0x1000,256); assert(t.valid && t.before_match);
    memory[32]=0x22; assert(rpk_read_watch_end(&t,256,"/app0/test.rpk",384));
    puts("Read watch: ordinal, read ranges, partial/padding, aliases, remap, stale/unreadable, actual counts, errors, bounded reads and publication passed");
}

int runtime_memory_debug_stack_read(uintptr_t a,void *out,uint64_t n) { (void)a; (void)out; (void)n; return 0; }
