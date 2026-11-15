#define _GNU_SOURCE
#include "../src/runtime_rpk_trace.h"
#include <assert.h>
#ifdef _WIN32
uintptr_t runtime_memory_debug_backing_address(uintptr_t address,uint64_t size) { (void)address; (void)size; return 0; }
#endif
static unsigned char storage[256];
static unsigned reads,pauses,attached_checks;
static int unreadable,attached;
static const uintptr_t base=0x1000, caller=UINT64_C(0x800591dc5);
static const RpkDebugRead event={UINT64_C(0x0003a8908986f62b),128,256,0x1000,0x1020,16,12,8};
void rpk_clear_peer(uintptr_t destination,int value,uint64_t bytes,uintptr_t pc);
int runtime_memory_read_backing(uintptr_t address,void *out,uint64_t size) {
    __atomic_fetch_add(&reads,1,__ATOMIC_RELAXED);
    errno=ERANGE;
#ifdef _WIN32
    SetLastError(ERROR_READ_FAULT);
#endif
    assert(address>=base && size<=sizeof storage && address-base<=sizeof storage-size);
    if(unreadable) return 0;
    memcpy(out,storage+address-base,(size_t)size); return 1;
}
static int debugger(void) { ++attached_checks; return attached; }
static void pause_before(uintptr_t prefix,uintptr_t dst,uint64_t bytes,int value,uintptr_t pc) {
    assert(prefix==event.prefix && dst==base && bytes==256 && value==0 && pc==caller);
    assert(storage[32]==0x11); ++pauses;
}
static const RpkClearOps ops={debugger,pause_before};
static void reset(unsigned target) {
    rpk_clear_target=target; rpk_clear_seen=rpk_clear_active=rpk_clear_calls=0;
    rpk_clear_debug=1; reads=pauses=attached_checks=0; unreadable=0; attached=1;
    memset(storage,0x11,sizeof storage);
    rpk_trace_config.prefix=(RpkTracePrefix){event.resource,28,16,12,8,{0}};
    memset(rpk_trace_config.prefix.bytes,0x11,96);
}
static void already_initialized(void) {}
static void seed(void) { rpk_clear_seed(&event,"verified","verified"); }
static void observe(uintptr_t dst,uint64_t bytes) { rpk_clear_observe(dst,0,bytes,caller,&ops); }
static void *race_seed(void *p) { (void)p; seed(); return NULL; }
static void *race_observe(void *p) { (void)p; observe(base,256); return NULL; }
static void race(void *(*fn)(void *)) {
    pthread_t threads[8];
    for(unsigned i=0;i<8;++i) assert(!pthread_create(threads+i,NULL,fn,NULL));
    for(unsigned i=0;i<8;++i) assert(!pthread_join(threads[i],NULL));
}
int main(void) {
    reset(0); seed(); observe(base,256); assert(!reads && !pauses);
    reset(1);
    if(!BB_DIAGNOSTICS) {
        seed(); rpk_clear_peer(base,0,256,caller); observe(base,256);
        assert(!rpk_clear_active && !reads && !pauses);
        puts("RPK clear: diagnostics OFF is inert"); return 0;
    }
    const char *bad[]={"partial","MISMATCH","unreadable","not-covered"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];++i) {
        rpk_clear_seed(&event,bad[i],"verified"); rpk_clear_seed(&event,"verified",bad[i]);
    }
    assert(!rpk_clear_active && !rpk_clear_seen);
    reset(2); seed(); assert(!rpk_clear_active); seed(); assert(rpk_clear_active);
    RpkDebugRead other=event; other.prefix+=16;
    rpk_clear_seed(&other,"verified","verified"); assert(rpk_clear_read.prefix==event.prefix);
    observe(base,256); assert(pauses==1 && reads==8 && !rpk_clear_active);
    seed(); observe(base,256); assert(pauses==1 && reads==8); // no reseed/repeat
    reset(1); seed();
    observe(base,0); observe(UINTPTR_MAX-2,8); observe(base,32);
    observe(event.prefix+12,4); observe(event.prefix+7*16+12,1);
    assert(!reads && !pauses && rpk_clear_active); // padding, adjacency, overflow do not qualify
    rpk_clear_debug=0; observe(event.prefix+1,1);
    assert(reads==8 && !attached_checks && !pauses && !rpk_clear_active); // partial sampled-byte overlap
    reset(1); seed(); storage[32+7*16]=0x22; observe(base,256);
    assert(reads==8 && !pauses && !attached_checks && !rpk_clear_active); // fresh verification of ALL samples
    reset(1); seed(); unreadable=1; observe(base,256);
    assert(reads==1 && !pauses && !attached_checks && !rpk_clear_active);
    reset(1); seed(); attached=0; observe(base,256); attached=1; observe(base,256);
    assert(reads==8 && attached_checks==1 && !pauses); // no delayed debugger stop
    reset(1); seed(); rpk_clear_calls=RPK_CLEAR_CALL_LIMIT; observe(base,256);
    assert(!reads && !pauses && !rpk_clear_active);
    reset(1); race(race_seed); assert(rpk_clear_seen>=1 && rpk_clear_seen<=8 && rpk_clear_active);
    race(race_observe); assert(reads==8 && pauses==1 && !rpk_clear_active);
    reset(1); seed(); rpk_clear_read.prefix=UINTPTR_MAX-2; observe(base,256); assert(!reads);
    reset(1); seed(); rpk_clear_read.samples=UINT64_MAX; observe(base,256); assert(!reads);
    reset(1); seed(); rpk_clear_debug=0;
    unsigned char before[sizeof storage]; memcpy(before,storage,sizeof storage);
    errno=EDOM;
#ifdef _WIN32
    SetLastError(ERROR_INVALID_HANDLE);
#endif
    rpk_clear_peer(base,0,256,caller); // real exported entry from another translation unit
    assert(errno==EDOM && reads==8 && !rpk_clear_active && !memcmp(before,storage,sizeof storage));
#ifdef _WIN32
    assert(GetLastError()==ERROR_INVALID_HANDLE);
#endif
    // Integration: the actual trace callback seeds from verified resource header + prefix.
    reset(1);
    pthread_once(&rpk_trace_once,already_initialized);
    strcpy(rpk_trace_config.path,"/app0/test.rpk");
    rpk_trace_config.count=1; rpk_trace_config.spans[0]=(RpkTraceSpan){event.resource,128,256};
    rpk_trace_config.prefix.offset=32;
    memcpy(storage,"EVOSLITL",8);
    for(unsigned i=0;i<8;++i) storage[8+i]=(unsigned char)(event.resource>>(8*i));
    rpk_trace_note("/app0/test.rpk",128,base,256,"synthetic-read");
    assert(rpk_clear_active && rpk_clear_read.prefix==event.prefix && reads==9);
    observe(base,256); assert(pauses==1 && reads==17);
    puts("RPK clear: verified-only selection, no reseed, overlap/padding/overflow, fresh/stale/unreadable, debugger gate, budgets, concurrency, peer entry, errors and unchanged bytes passed");
}
