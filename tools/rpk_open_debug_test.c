#include "../src/runtime_rpk_open_debug.h"
#include <assert.h>

static RpkOpenDebugState state=RPK_OPEN_DEBUG_STATE;
static unsigned checks,pauses,passed;
static int attached;
static int debugger(void) { ++checks; errno=ERANGE; return attached; }
static void pause_before_open(const char *path) {
    assert(!strcmp(path,"/app0/worlds/fixture.rpk"));
    assert(pthread_mutex_trylock(&state.lock)==EBUSY); /* Gate held across pause. */
    ++pauses; errno=ERANGE;
#ifdef _WIN32
    SetLastError(ERROR_INVALID_DATA);
#endif
}
static const RpkOpenDebugOps ops={debugger,pause_before_open};
static int observe(int enabled,const char *expected,const char *path) {
    return rpk_open_debug_dispatch(&state,enabled,expected,path,&ops);
}
static const char path[]="/app0/worlds/fixture.rpk";
static void *open_attempt(void *unused) {
    (void)unused;
    errno=EDOM;
    observe(1,path,path);
    assert(errno==EDOM);
    /* No matching open can reach its continuation before the callback returns. */
    pthread_mutex_lock(&state.lock);
    assert(pauses==1); ++passed;
    pthread_mutex_unlock(&state.lock);
    return NULL;
}
int main(void) {
    const char *invalid[]={NULL,"","0","2","01"," 1","1 ","true"};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i) assert(!rpk_open_debug_enabled(invalid[i]));
    assert(rpk_open_debug_enabled("1")==!!BB_DIAGNOSTICS);
    assert(!observe(0,path,path));
    assert(!observe(1,path,"/app0/worlds/other.rpk"));
    assert(!observe(1,NULL,path) && !observe(1,"",path) && !observe(1,path,NULL));
    assert(!state.consumed && !checks && !pauses);
    errno=EDOM;
    assert(!observe(1,path,path)); /* No debugger, or build disabled. */
    assert(errno==EDOM);
    if(!BB_DIAGNOSTICS) {
        attached=1;
        assert(!observe(1,path,path) && !checks && !pauses && !state.consumed);
        puts("RPK open debug: OFF gate ignores even explicit enable"); return 0;
    }
    assert(state.consumed && checks==1 && !pauses);
    attached=1;
    assert(!observe(1,path,path) && checks==1); /* No delayed surprise break. */
    state.consumed=0;
    errno=EDOM;
#ifdef _WIN32
    SetLastError(ERROR_ACCESS_DENIED);
#endif
    assert(observe(1,path,path) && errno==EDOM && pauses==1);
#ifdef _WIN32
    assert(GetLastError()==ERROR_ACCESS_DENIED);
#endif
    for(unsigned i=0;i<100;++i) assert(!observe(1,path,path));
    assert(checks==2 && pauses==1);
    state.consumed=0; pauses=0;
    pthread_t threads[8];
    for(unsigned i=0;i<8;++i) assert(!pthread_create(&threads[i],NULL,open_attempt,NULL));
    for(unsigned i=0;i<8;++i) assert(!pthread_join(threads[i],NULL));
    assert(pauses==1 && passed==8 && checks==3);
    assert(!pthread_mutex_destroy(&state.lock));
    puts("RPK open debug: strict opt-in/path, one-shot, no debugger, error preservation and concurrent gate passed");
}
