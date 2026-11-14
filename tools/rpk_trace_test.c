#define _GNU_SOURCE
#include "../src/runtime_rpk_trace.h"
#include <assert.h>
#include <unistd.h>
static unsigned reads;
int runtime_memory_read_backing(uintptr_t address,void *out,uint64_t size) {
    ++reads;
    assert(address==0x1000 && size==20);
    unsigned char header[20]="EVOSLITL"; header[8]=1; header[14]=3;
    memcpy(out,header,20); return 1;
}
static int parse(const char *s,RpkTraceConfig *out) {
    char copy[4096]; assert(strlen(s)<sizeof copy); strcpy(copy,s); return rpk_trace_parse(copy,out);
}
int main(void) {
    RpkTraceConfig c={0};
    const char *valid="rpk-trace-v1\n/app0/worlds/test.rpk\n0003000000000001 128 64\n0004000000000001 192 32\n";
    assert(parse(valid,&c) && c.count==2 && c.spans[0].offset==128);
    const char *invalid[]={"", "rpk-trace-v2\n/app0/x\n1 1 20\n", "rpk-trace-v1\n/app0/../x\n1 1 20\n",
        "rpk-trace-v1\n/app0/x\n1 1 20", "rpk-trace-v1\n/app0/x\n1 -1 20\n",
        "rpk-trace-v1\n/app0/x\n1 18446744073709551616 20\n", "rpk-trace-v1\n/app0/x\n1 4294967290 20\n",
        "rpk-trace-v1\n/app0/x\n1 1 0\n", "rpk-trace-v1\n/app0/x\n1 1 20\n2 5 20\n",
        "rpk-trace-v1\n/app0/x\n1 1 20\n1 25 20\n"};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i) { RpkTraceConfig bad={0}; assert(!parse(invalid[i],&bad) && !bad.count); }
    uint64_t at=99,length=99; uintptr_t address=0;
    assert(rpk_trace_overlap(c.spans[0],120,32,0x1000,&at,&length,&address) && at==0 && length==24 && address==0x1008);
    assert(rpk_trace_overlap(c.spans[0],140,100,0x1000,&at,&length,&address) && at==12 && length==52 && address==0x1000);
    assert(!rpk_trace_overlap(c.spans[0],0,128,0x1000,&at,&length,&address));
    assert(!rpk_trace_overlap(c.spans[0],UINT64_MAX,20,0x1000,&at,&length,&address));
    assert(!rpk_trace_overlap(c.spans[0],128,20,UINTPTR_MAX-3,&at,&length,&address));
    // Use an exclusive temporary file; never touch user files or game data.
    char path[]="rpk-trace-test-XXXXXX"; int fd=mkstemp(path); assert(fd>=0);
    FILE *f=fdopen(fd,"wb"); assert(f); assert(fwrite(valid,1,strlen(valid),f)==strlen(valid)); fclose(f);
#ifdef _WIN32
    _putenv_s("BB_RPK_TRACE_FILE",path);
#else
    setenv("BB_RPK_TRACE_FILE",path,1);
#endif
    rpk_trace_note("/app0/other",128,0x1000,64,"pread"); assert(!reads);
    rpk_trace_note(c.path,128,0x1000,64,"pread");
    if(BB_DIAGNOSTICS) assert(reads==1); else assert(!reads);
    // Split/failed reads never trigger an out-of-range header read.
    unsigned before=reads;
    rpk_trace_note(c.path,140,0x1010,10,"pread");
    rpk_trace_note(c.path,128,0x1000,-1,"pread"); assert(reads==before);
    for(unsigned i=0;i<100;++i) rpk_trace_note(c.path,128,0x1000,64,"pread");
    assert(reads<=64); before=reads;
    rpk_trace_note(c.path,128,0x1000,64,"pread"); assert(reads==before);
    remove(path); puts("RPK trace: parse, overlap, overflow, split reads, build gate and log/read cap passed");
}
