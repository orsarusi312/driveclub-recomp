#define _GNU_SOURCE
#include "../src/runtime_rpk_debug.h"
#include <assert.h>
#ifdef _WIN32
/* No real mappings or debugger break are used by the standalone observer tests. */
uintptr_t runtime_memory_debug_backing_address(uintptr_t address,uint64_t size) { (void)address; (void)size; return 0; }
#endif
#include <pthread.h>
static unsigned checks,pauses;
static int attached;
static RpkDebugRead observed;
static int debugger(void) { __atomic_fetch_add(&checks,1,__ATOMIC_RELAXED); return attached; }
static void pause_at_read(const RpkDebugRead *r) { observed=*r; __atomic_fetch_add(&pauses,1,__ATOMIC_RELAXED); }
static const RpkDebugOps ops={debugger,pause_at_read};
static const RpkDebugRead read_event={UINT64_C(0x0003a8908986f62b),128,60048,0x1000,0x6b1c,16,12,8};
static RpkDebugState concurrent;
static void *race(void *arg) {
    (void)arg;
    for(unsigned i=0;i<100;++i) rpk_debug_dispatch(&concurrent,&read_event,"verified","verified",&ops);
    return NULL;
}
int main(void) {
    assert(rpk_debug_target("1")==1 && rpk_debug_target("2")==2);
    const char *bad[]={NULL,"","0","3","-1"," 1","1 ","01","true","18446744073709551616"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];++i) assert(!rpk_debug_target(bad[i]));
    RpkDebugState s={0};
    assert(!rpk_debug_dispatch(&s,&read_event,"verified","verified",&ops) && !checks);
    s.target=1;
    const char *states[]={"partial","MISMATCH","unreadable","not-target","not-covered"};
    for(unsigned i=0;i<sizeof states/sizeof states[0];++i) {
        assert(!rpk_debug_dispatch(&s,&read_event,states[i],"verified",&ops));
        assert(!rpk_debug_dispatch(&s,&read_event,"verified",states[i],&ops));
    }
    assert(!s.seen && !checks); // no stop on partial/malformed/missing data
    assert(!rpk_debug_dispatch(&s,&read_event,"verified","verified",&ops));
    assert(s.consumed==1 && checks==1 && !pauses);
    attached=1;
    assert(!rpk_debug_dispatch(&s,&read_event,"verified","verified",&ops) && checks==1); // no delayed surprise
    s=(RpkDebugState){2,0,0};
    assert(!rpk_debug_dispatch(&s,&read_event,"verified","verified",&ops) && checks==1);
    const RpkDebugRead unchanged=read_event;
    assert(rpk_debug_dispatch(&s,&read_event,"verified","verified",&ops));
    assert(pauses==1 && s.consumed==1 && !memcmp(&observed,&read_event,sizeof observed));
    assert(!memcmp(&unchanged,&read_event,sizeof unchanged));
    for(unsigned i=0;i<100;++i) assert(!rpk_debug_dispatch(&s,&read_event,"verified","verified",&ops));
    concurrent=(RpkDebugState){1,0,0};
    pthread_t threads[8];
    for(unsigned i=0;i<8;++i) assert(!pthread_create(&threads[i],NULL,race,NULL));
    for(unsigned i=0;i<8;++i) assert(!pthread_join(threads[i],NULL));
    assert(pauses==2 && concurrent.consumed==1); // exactly one selected callback across threads
    // Default wrapper is inert, including Windows: no debugger break in automated tests.
    errno=EDOM; rpk_debug_read_note(&read_event,"verified","verified"); assert(errno==EDOM);
    if(!BB_DIAGNOSTICS) {
        rpk_debug_state.target=1;
        rpk_debug_read_note(&read_event,"verified","verified");
        assert(!rpk_debug_state.consumed);
    }
    puts("RPK debug boundary: strict selection, verified-only, no debugger, one-shot, concurrency, unchanged input and OFF/default gates passed");
}
