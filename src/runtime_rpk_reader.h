/* Read-only candidate reader metadata at the existing one-shot debugger boundary.
 * Layout comes from the supplied native constructor, not from a general object schema. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../gpu/shim/bbport_diagnostics.h"
typedef int (*RpkReaderRead)(uintptr_t,void *,uint64_t);
typedef struct {
    uintptr_t object;
    uint64_t fields[8], underlying_vtable, methods[12], underlying_methods[12];
    int methods_read, underlying_read;
} RpkReaderSnapshot;
static inline uint64_t rpk_reader_u64(const unsigned char *p) {
    uint64_t n=0; for(unsigned i=0;i<8;++i) n|=(uint64_t)p[i]<<(8*i); return n;
}
static inline int rpk_reader_range(uint64_t at,uint64_t bytes) {
    return at && at<=UINTPTR_MAX && bytes<=UINTPTR_MAX-(uintptr_t)at;
}
static inline int rpk_reader_table(uint64_t at,uint64_t *slots,RpkReaderRead read) {
    unsigned char data[96];
    if((at&7) || !rpk_reader_range(at,sizeof data) || !read((uintptr_t)at,data,sizeof data)) return 0;
    for(unsigned i=0;i<12;++i) slots[i]=rpk_reader_u64(data+8*i);
    return 1;
}
static inline const char *rpk_reader_capture(uintptr_t base,uint64_t bytes,RpkReaderSnapshot *out,RpkReaderRead read) {
    memset(out,0,sizeof *out);
    if(base<64 || !bytes || bytes>65536 || bytes>UINTPTR_MAX-base) return "read-range";
    unsigned char header[64]; out->object=base-64;
    if(!read(out->object,header,sizeof header)) return "header-unreadable";
    for(unsigned i=0;i<8;++i) out->fields[i]=rpk_reader_u64(header+8*i);
    const uint64_t *f=out->fields;
    if(f[6]!=base || f[7]!=65536) return "buffer-layout-mismatch";
    if(f[4]>f[7] || f[5]>f[7] || f[4]>f[7]-f[5]) return "cursor-range";
    if((f[0]&7) || (f[1]&7) || !rpk_reader_range(f[0],96) || !rpk_reader_range(f[1],8)) return "object-pointer-range";
    out->methods_read=rpk_reader_table(f[0],out->methods,read);
    unsigned char pointer[8];
    if(read((uintptr_t)f[1],pointer,sizeof pointer)) {
        out->underlying_vtable=rpk_reader_u64(pointer);
        out->underlying_read=rpk_reader_table(out->underlying_vtable,out->underlying_methods,read);
    }
    return "layout-consistent"; // NOT proof of object type, ownership or allocation lifetime.
}
static int rpk_reader_enabled;
static inline void rpk_reader_init(void) {
    const char *setting=BB_DIAGNOSTIC_ENV("BB_RPK_READER");
    rpk_reader_enabled=setting && !strcmp(setting,"1");
    if(setting && !rpk_reader_enabled) fputs("RpkReader: invalid setting; disabled (use 1)\n",stderr);
}
static inline void rpk_reader_note(uint64_t resource,uintptr_t base,uint64_t bytes,RpkReaderRead read) {
    if(!BB_DIAGNOSTICS || !rpk_reader_enabled) return;
    RpkReaderSnapshot s;
    const char *status=rpk_reader_capture(base,bytes,&s,read);
    fprintf(stderr,"RpkReader: resource=%016llx candidate=0x%llx status=%s read-base=0x%llx read-bytes=%llu; snapshot only, no ownership claim\n",
        (unsigned long long)resource,(unsigned long long)s.object,status,(unsigned long long)base,(unsigned long long)bytes);
    if(strcmp(status,"layout-consistent")) return;
    fprintf(stderr,"RpkReader: vtable=0x%llx underlying=0x%llx logical=%llu window-start=%llu available=%llu cursor=%llu buffer=0x%llx capacity=%llu\n",
        (unsigned long long)s.fields[0],(unsigned long long)s.fields[1],(unsigned long long)s.fields[2],
        (unsigned long long)s.fields[3],(unsigned long long)s.fields[4],(unsigned long long)s.fields[5],
        (unsigned long long)s.fields[6],(unsigned long long)s.fields[7]);
    const unsigned slots[]={2,3,6,7,11};
    for(unsigned layer=0;layer<2;++layer) {
        const int ok=layer ? s.underlying_read:s.methods_read;
        fprintf(stderr,"RpkReader: %s vtable=0x%llx table=%s",layer ? "underlying":"wrapper",
            (unsigned long long)(layer ? s.underlying_vtable:s.fields[0]),ok ? "read":"unreadable-or-invalid");
        if(ok) for(unsigned i=0;i<5;++i) fprintf(stderr," +%02x=0x%llx",slots[i]*8,
            (unsigned long long)(layer ? s.underlying_methods[slots[i]]:s.methods[slots[i]]));
        fputc('\n',stderr);
    }
}
