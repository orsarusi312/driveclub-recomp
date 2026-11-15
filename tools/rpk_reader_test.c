#define _GNU_SOURCE
#include "../src/runtime_rpk_debug.h"
#include <assert.h>
static unsigned char header[64],table[96],underlying[8],under_table[96];
static unsigned calls,bytes_read;
static uintptr_t fail;
static void put(unsigned char *p,uint64_t n) { for(unsigned i=0;i<8;++i) p[i]=(unsigned char)(n>>(i*8)); }
int runtime_memory_read_backing(uintptr_t at,void *out,uint64_t bytes) {
    ++calls; bytes_read+=(unsigned)bytes;
    if(at==fail) return 0;
    const unsigned char *src=NULL;
    if(at==0x1000 && bytes==64) src=header;
    if(at==0x2000 && bytes==96) src=table;
    if(at==0x3000 && bytes==8) src=underlying;
    if(at==0x4000 && bytes==96) src=under_table;
    assert(src); memcpy(out,src,(size_t)bytes); return 1;
}
#ifdef _WIN32
uintptr_t runtime_memory_debug_backing_address(uintptr_t at,uint64_t bytes) { (void)at; (void)bytes; return 0; }
#endif
static void reset(void) {
    memset(header,0,sizeof header); put(header,0x2000); put(header+8,0x3000);
    put(header+16,1000); put(header+24,900); put(header+32,20); put(header+40,100);
    put(header+48,0x1040); put(header+56,65536); put(underlying,0x4000);
    for(unsigned i=0;i<12;++i) { put(table+i*8,0x5000+i*16); put(under_table+i*8,0x6000+i*16); }
    calls=bytes_read=0; fail=0;
}
static const char *capture(RpkReaderSnapshot *s) { return rpk_reader_capture(0x1040,65536,s,runtime_memory_read_backing); }
static int attached(void) { return 1; }
static void pause_mock(const RpkDebugRead *r) { rpk_reader_note(r->resource,r->read_base,r->read_bytes,runtime_memory_read_backing); }
int main(void) {
    RpkReaderSnapshot s; reset();
    rpk_reader_note(3,0x1040,65536,runtime_memory_read_backing); assert(!calls); // default off
    unsigned char unchanged[64]; memcpy(unchanged,header,64);
    assert(!strcmp(capture(&s),"layout-consistent") && calls==4 && bytes_read==264);
    assert(s.object==0x1000 && s.fields[6]==0x1040 && s.fields[7]==65536);
    assert(s.methods_read && s.underlying_read && s.methods[3]==0x5030 && s.underlying_methods[11]==0x60b0);
    assert(!memcmp(header,unchanged,64));
    reset(); assert(!strcmp(rpk_reader_capture(63,1,&s,runtime_memory_read_backing),"read-range") && !calls);
    assert(!strcmp(rpk_reader_capture(UINTPTR_MAX-1,4,&s,runtime_memory_read_backing),"read-range") && !calls);
    assert(!strcmp(rpk_reader_capture(0x1040,0,&s,runtime_memory_read_backing),"read-range") && !calls);
    assert(!strcmp(rpk_reader_capture(0x1040,65537,&s,runtime_memory_read_backing),"read-range") && !calls);
    reset(); fail=0x1000; assert(!strcmp(capture(&s),"header-unreadable") && calls==1);
    reset(); put(header+48,0x1041); assert(!strcmp(capture(&s),"buffer-layout-mismatch") && calls==1);
    reset(); put(header+56,65535); assert(!strcmp(capture(&s),"buffer-layout-mismatch") && calls==1);
    reset(); put(header+40,65536); assert(!strcmp(capture(&s),"cursor-range") && calls==1);
    reset(); put(header+32,UINT64_MAX); assert(!strcmp(capture(&s),"cursor-range") && calls==1);
    reset(); put(header,UINT64_MAX-7); assert(!strcmp(capture(&s),"object-pointer-range") && calls==1);
    reset(); put(header+8,0); assert(!strcmp(capture(&s),"object-pointer-range") && calls==1);
    reset(); put(header,0x2001); assert(!strcmp(capture(&s),"object-pointer-range") && calls==1);
    reset(); fail=0x2000; assert(!strcmp(capture(&s),"layout-consistent") && !s.methods_read && s.underlying_read);
    reset(); fail=0x3000; assert(!strcmp(capture(&s),"layout-consistent") && !s.underlying_read && calls==3);
    reset(); put(underlying,UINT64_MAX-7); assert(!strcmp(capture(&s),"layout-consistent") && !s.underlying_read && calls==3);
    reset(); fail=0x4000; assert(!strcmp(capture(&s),"layout-consistent") && !s.underlying_read && calls==4);
    reset(); put(header+32,0); put(header+40,65536); assert(!strcmp(capture(&s),"layout-consistent")); // pre-refill cursor allowed
    reset();
    rpk_reader_enabled=1;
    const RpkDebugRead r={3,128,65536,0x1040,0x1060,16,12,8};
    RpkDebugState state={2,0,0}; const RpkDebugOps ops={attached,pause_mock};
    rpk_debug_dispatch(&state,&r,"partial","verified",&ops);
    rpk_debug_dispatch(&state,&r,"verified","verified",&ops); assert(!calls);
    rpk_debug_dispatch(&state,&r,"verified","verified",&ops);
    for(unsigned i=0;i<100;++i) rpk_debug_dispatch(&state,&r,"verified","verified",&ops);
    assert(calls==(BB_DIAGNOSTICS ? 4u:0u) && bytes_read==(BB_DIAGNOSTICS ? 264u:0u));
    puts("RPK reader: bounded metadata, malformed layouts, pointers, overflow, table failures, unchanged bytes, selected-read coupling and OFF/default gates passed");
}

int runtime_memory_debug_read(uintptr_t address,void *out,uint64_t bytes) { return runtime_memory_read_backing(address,out,bytes); }
