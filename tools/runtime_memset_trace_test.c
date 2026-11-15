/* Exercise the real guest wrapper; unrelated runtime sections are discarded at link. */
#define _GNU_SOURCE
#include "../src/runtime.c"
#include <assert.h>
static unsigned observations,writes;
static int gpu_mode;
static unsigned char bytes[8200];
uintptr_t guest_cpu_return_address(uintptr_t native) { assert(native); return 0x12345678; }
void runtime_rpk_clear_before(uintptr_t dst,int value,uint64_t count,uintptr_t caller) {
    assert(dst==(uintptr_t)bytes && value==0x23 && count==sizeof bytes && caller==0x12345678);
    for(unsigned i=0;i<sizeof bytes;++i) assert(bytes[i]==0x11);
    ++observations;
}
int bbgpu_guest_gpu_pages(uintptr_t address,uint64_t count) {
    assert(address==(uintptr_t)bytes && count==sizeof bytes); return gpu_mode;
}
int bbgpu_guest_write(uintptr_t dst,const void *source,uint64_t count) {
    assert(dst>=(uintptr_t)bytes && dst+count<=(uintptr_t)bytes+sizeof bytes && count<=4096);
    ++writes;
    if(gpu_mode==2) return 0;
    memcpy((void *)dst,source,(size_t)count); return 1;
}
int main(void) {
    for(gpu_mode=0;gpu_mode<3;++gpu_mode) {
        memset(bytes,0x11,sizeof bytes); writes=0;
        assert(guest_memset(bytes,0x23,sizeof bytes)==bytes);
        for(unsigned i=0;i<sizeof bytes;++i) assert(bytes[i]==0x23);
        assert(writes==(gpu_mode ? 3u:0u));
    }
    assert(observations==(BB_DIAGNOSTICS ? 3u:0u));
    puts("Guest memset wrapper: observer before writes, caller capture, CPU/GPU/fallback bytes and return preserved, ON/OFF passed");
}
