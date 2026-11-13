/* Real host mappings, synthetic data only. Including the backend also permits
 * checking the fault reporter while its internal lock is held. No GPU needed. */
#include "../src/runtime_memory.c"
#include <assert.h>
#ifdef NDEBUG
#error This test requires assertions
#endif
/* The standalone backend has no import-name database. Exercise allocation APIs
 * directly; this unused resolver dependency is deliberately not emulated. */
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) {
    (void)table; (void)count; (void)nid;
    assert(!"import lookup is outside this test"); return 0;
}
#ifdef _WIN32
/* Only the unrelated optimization-file watcher needs this symbol; it is not
 * started by this test. The runtime normally supplies it from compat_win.c. */
void compat_sleep_ns(uint64_t ns) { Sleep((DWORD)((ns+999999)/1000000)); }
#endif

static const uintptr_t boundary=UINT64_C(0x513b600000);
static void check_report(uintptr_t address, const char *expected) {
    FILE *f=tmpfile(); assert(f);
    memory_diagnose(f,address);
    assert(!fseek(f,0,SEEK_SET));
    char text[8192]; size_t n=fread(text,1,sizeof(text)-1,f); text[n]=0;
    assert(strstr(text,expected));
    assert(!fclose(f));
}
static void check_host(uintptr_t address, int committed, int writable) {
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION m;
    assert(VirtualQuery((void *)address,&m,sizeof(m))==sizeof(m));
    assert(m.State==(committed ? MEM_COMMIT : MEM_RESERVE));
    if (committed) assert(m.Protect==(writable ? PAGE_READWRITE : PAGE_READONLY));
#else
    (void)address; (void)committed; (void)writable;
#endif
}
int main(void) {
    check_report(boundary,"tables unavailable");
    uintptr_t base=boundary-2*PAGE;
    void *reserved=(void *)base;
#ifdef _WIN32
    runtime_memory_reserve();
#endif
    /* Reproduce the logged launch mismatch without writing to an unmapped
     * address: the 1440p patch asks for 6144 MiB from a 5056 MiB direct pool. */
    assert(POOL_SIZE==UINT64_C(5056)*1024*1024);
    int64_t refused=-1;
    assert(direct_allocate(0,(int64_t)POOL_SIZE,UINT64_C(6144)*1024*1024,PAGE,3,&refused)==NO_SPACE);
    assert(refused==-1 && !runtime_memory_is_mapped(boundary,8));
    check_host(boundary,0,0);
    check_report(boundary,"address not in a recorded VMA");
    assert(!reserve_range(&reserved,6*PAGE,MAP_FIXED_FLAG,PAGE));
    check_report(boundary,"kind=reserved");
    check_host(boundary,0,0);
    int64_t phys=-1;
    assert(!direct_allocate(0,(int64_t)POOL_SIZE,6*PAGE,PAGE,3,&phys));
    void *mapped=(void *)base;
    assert(!direct_map(&mapped,4*PAGE,0x33,MAP_FIXED_FLAG,phys,PAGE));
    volatile uint64_t *left=(volatile uint64_t *)(boundary-8);
    volatile uint64_t *right=(volatile uint64_t *)boundary;
    *left=0x1234; *right=0x5678;
    uint64_t copy=0;
    assert(runtime_memory_read_backing(boundary,&copy,8) && copy==*right);
    assert(!kernel_mprotect((void *)base,PAGE,1));
    check_host(base,1,0);
    /* Split a section into left, replacement and right views. The left view
     * contains two different native protection runs; both must survive. */
    for (unsigned i=0;i<16;++i) {
        void *slot=(void *)boundary;
        assert(!direct_map(&slot,PAGE,3,MAP_FIXED_FLAG,phys+4*PAGE,PAGE));
        *right=0xabcd+i;
        assert(*left==0x1234);
        check_host(base,1,0); check_host(boundary-8,1,1); check_host(boundary,1,1);
        assert(runtime_memory_read_backing(boundary,&copy,8) && copy==*right);
        check_report(boundary,"guest containing [0x513b600000,");
        assert(direct_map(&slot,PAGE,3,MAP_FIXED_FLAG|MAP_NO_OVERWRITE,phys,PAGE)==NO_MEMORY);
        assert(!direct_unmap((void *)boundary,PAGE));
        check_report(boundary,"address not in a recorded VMA");
        check_host(boundary,0,0);
        assert(!runtime_memory_is_mapped(boundary,8));
        assert(runtime_memory_is_mapped(boundary-8,8));
        assert(!runtime_memory_is_mapped(boundary-8,16));
        assert(!runtime_memory_read_backing(boundary,&copy,8));
    }
    mapped=(void *)boundary;
    assert(!direct_map(&mapped,PAGE,3,MAP_FIXED_FLAG,phys+4*PAGE,PAGE));
    runtime_memory_gpu_protect(boundary,PAGE,1,0);
    check_report(boundary,"prot=0x3"); /* guest permissions differ from host */
    check_host(boundary,1,0); /* reporter must not change protection */
    runtime_memory_gpu_protect(boundary,PAGE,1,1);
    *right=0xbeef;
    write_lock(); check_report(boundary,"tables unavailable"); write_unlock();
    read_lock(); /* atomic-mode snapshot declines, legacy mode permits nested readers */
    MemorySnapshot snapshot;
    if (!use_legacy()) assert(!memory_snapshot(boundary,&snapshot));
    read_unlock();
    assert(direct_allocate((int64_t)POOL_SIZE,(int64_t)POOL_SIZE,PAGE,PAGE,3,&phys)==NO_SPACE);
    assert(direct_map(&mapped,PAGE,3,MAP_FIXED_FLAG,(int64_t)POOL_SIZE,PAGE)==INVALID);
    assert(!direct_unmap((void *)base,6*PAGE));
    assert(!runtime_memory_is_mapped(boundary,8));
    runtime_memory_diagnose(boundary);
    puts("runtime memory tests passed: boundary writes, split/remap, alias, protection, holes, refusal, nonblocking diagnostics");
    return 0;
}
