/* Opt-in read-only loose-RPK range provenance. No guest buffer is dereferenced. */
#pragma once
#include "../gpu/shim/bbport_diagnostics.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include "runtime_rpk_debug.h"

typedef struct { uint64_t id, offset, size; } RpkTraceSpan;
typedef struct { uint64_t id,offset,stride,width,samples; unsigned char bytes[96]; } RpkTracePrefix;
typedef struct { char path[512]; unsigned count; RpkTraceSpan spans[8]; RpkTracePrefix prefix; } RpkTraceConfig;
static int rpk_trace_number(char **p, int base, char separator, uint64_t *out) {
    char *end; const char *start=*p;
    if (!*start || !((*start>='0' && *start<='9') ||
        (base==16 && ((*start>='a' && *start<='f') || (*start>='A' && *start<='F'))))) return 0;
    errno=0; unsigned long long n=strtoull(start,&end,base);
    if (errno || end==start || *end!=separator) return 0;
    *out=(uint64_t)n; *p=end+(separator!=0); return 1;
}
static int rpk_trace_parse(char *text,RpkTraceConfig *out) {
    RpkTraceConfig c={0};
    const int v2=!strncmp(text,"rpk-trace-v2\n",13);
    if (!v2 && strncmp(text,"rpk-trace-v1\n",13)) return 0;
    char *p=text+13, *end=strchr(p,'\n');
    if (!end || end-p>=511 || strncmp(p,"/app0/",6)) return 0;
    memcpy(c.path,p,(size_t)(end-p));
    if (strstr(c.path,"..") || strchr(c.path,'\r')) return 0;
    p=end+1;
    while (*p) {
        if (!(end=strchr(p,'\n'))) return 0;
        *end=0;
        if (!strncmp(p,"prefix ",7)) {
            if (!v2 || c.prefix.id) return 0;
            p+=7; RpkTracePrefix *q=&c.prefix;
            if (!rpk_trace_number(&p,16,' ',&q->id) || !rpk_trace_number(&p,10,' ',&q->offset) ||
                !rpk_trace_number(&p,10,' ',&q->stride) || !rpk_trace_number(&p,10,' ',&q->width) ||
                !rpk_trace_number(&p,10,' ',&q->samples) || q->id>>48!=3 ||
                (q->width!=6 && q->width!=12) || q->samples<3 || q->samples>8 ||
                q->stride<q->width || q->stride>4096 || strlen(p)!=q->width*q->samples*2) return 0;
            for (size_t i=0;i<q->width*q->samples*2;++i) {
                int n=p[i]>='0' && p[i]<='9' ? p[i]-'0':p[i]>='a' && p[i]<='f' ? p[i]-'a'+10:-1;
                if (n<0) return 0;
                q->bytes[i/2]=(unsigned char)((q->bytes[i/2]<<4)|n);
            }
            p=end+1; continue;
        }
        if (c.count==8) return 0;
        RpkTraceSpan *s=&c.spans[c.count];
        if (!rpk_trace_number(&p,16,' ',&s->id) || !rpk_trace_number(&p,10,' ',&s->offset) ||
            !rpk_trace_number(&p,10,0,&s->size) || !s->id || s->size<20 ||
            s->offset>UINT32_MAX || s->size>UINT32_MAX-s->offset) return 0;
        for (unsigned i=0;i<c.count;++i) {
            const RpkTraceSpan *old=&c.spans[i];
            if (old->id==s->id || (s->offset<old->offset+old->size && old->offset<s->offset+s->size)) return 0;
        }
        ++c.count; p=end+1;
    }
    if (!c.count) return 0;
    if (v2) {
        int found=0;
        for (unsigned i=0;i<c.count;++i) if (c.spans[i].id==c.prefix.id) {
            found=c.prefix.offset>=28 && c.prefix.offset<=c.spans[i].size &&
                (c.prefix.samples-1)*c.prefix.stride+c.prefix.width<=c.spans[i].size-c.prefix.offset;
        }
        if (!found) return 0;
    }
    *out=c; return 1;
}
/* Intersection is exact even for partial/split reads; file ranges include headers. */
static int rpk_trace_overlap(RpkTraceSpan s,uint64_t offset,uint64_t bytes,uintptr_t buffer,
                             uint64_t *resource_offset,uint64_t *length,uintptr_t *address) {
    if (bytes>UINT64_MAX-offset || bytes>UINTPTR_MAX-buffer || s.size>UINT64_MAX-s.offset) return 0;
    uint64_t lo=offset>s.offset ? offset:s.offset, hi=offset+bytes<s.offset+s.size ? offset+bytes:s.offset+s.size;
    if (lo>=hi) return 0;
    *resource_offset=lo-s.offset; *length=hi-lo; *address=buffer+(uintptr_t)(lo-offset); return 1;
}
static RpkTraceConfig rpk_trace_config;
static pthread_once_t rpk_trace_once=PTHREAD_ONCE_INIT;
static unsigned rpk_trace_logs;
extern int runtime_memory_read_backing(uintptr_t,void*,uint64_t);
#include "runtime_rpk_copy_impl.h"
#include "runtime_rpk_clear_impl.h"
#include "runtime_rpk_read_watch.h"
static void rpk_trace_init(void) {
    const char *path=BB_DIAGNOSTIC_ENV("BB_RPK_TRACE_FILE");
    if (!path || !*path) return;
    FILE *f=fopen(path,"rb"); char data[4097]; size_t size=0;
    if (f) { size=fread(data,1,4096,f); int bad=ferror(f); fclose(f); if (bad) size=0; }
    data[size]=0;
    if (!size || size==4096 || strlen(data)!=size || !rpk_trace_parse(data,&rpk_trace_config)) {
        fputs("RpkTrace: invalid/unreadable manifest; disabled\n",stderr); return;
    }
    rpk_debug_init();
    rpk_clear_init();
    rpk_read_watch_init();
    rpk_request_init();
    fprintf(stderr,"RpkTrace: enabled path=%s resources=%u limit=64; file reads only, not native selection\n",
            rpk_trace_config.path,rpk_trace_config.count);
    const char *copy=BB_DIAGNOSTIC_ENV("BB_RPK_COPY_TRACE");
    rpk_copy_enabled=copy && !strcmp(copy,"1") && rpk_trace_config.prefix.id;
    const char *copy_read=BB_DIAGNOSTIC_ENV("BB_RPK_COPY_READ");
    rpk_copy_read_target=rpk_debug_target(copy_read);
    if(copy_read && !rpk_copy_read_target) { rpk_copy_enabled=0; fputs("RpkCopy: invalid read selection; disabled (use 1 or 2)\n",stderr); }
    if(rpk_copy_enabled && rpk_copy_read_target) fprintf(stderr,"RpkCopy: initial seed restricted to verified read %u\n",rpk_copy_read_target);
    if(rpk_copy_enabled) fputs("RpkCopy: enabled; waits for verified read prefix; 16 addresses, 262144 HLE calls, 64 candidate copies\n",stderr);
}
static int rpk_trace_watches(const char *path) {
    pthread_once(&rpk_trace_once,rpk_trace_init);
    return rpk_trace_config.count && !strcmp(path,rpk_trace_config.path);
}
extern int runtime_memory_read_backing(uintptr_t,void*,uint64_t);
static const char *rpk_trace_check_prefix(const RpkTracePrefix *q,uint64_t id,uint64_t at,uint64_t length,
                                        uintptr_t address,uintptr_t *prefix_address) {
    *prefix_address=0;
    if (q->id!=id) return "not-target";
    const uint64_t span=(q->samples-1)*q->stride+q->width; /* config validated on input */
    if (length>UINTPTR_MAX-address || q->offset<at || q->offset-at>length || span>length-(q->offset-at)) return "not-covered";
    unsigned char row[12]; *prefix_address=address+(uintptr_t)(q->offset-at);
    for (uint64_t j=0;j<q->samples;++j) {
        if (!runtime_memory_read_backing(*prefix_address+j*q->stride,row,q->width)) return "unreadable";
        if (memcmp(row,q->bytes+j*q->width,(size_t)q->width)) return "MISMATCH";
    }
    return "verified";
}
static void rpk_trace_note(const char *path,int64_t offset,uintptr_t buffer,int64_t bytes,const char *method) {
    if (offset<0 || bytes<=0 || !rpk_trace_watches(path)) return;
    if (__atomic_load_n(&rpk_trace_logs,__ATOMIC_RELAXED)>=64) return;
    for (unsigned i=0;i<rpk_trace_config.count;++i) {
        RpkTraceSpan s=rpk_trace_config.spans[i]; uint64_t at,length; uintptr_t address;
        if (!rpk_trace_overlap(s,(uint64_t)offset,(uint64_t)bytes,buffer,&at,&length,&address)) continue;
        if (__atomic_fetch_add(&rpk_trace_logs,1,__ATOMIC_RELAXED)>=64) return;
        const char *header="partial";
        if (!at && length>=20) {
            unsigned char data[20]; uint64_t id=0;
            if (!runtime_memory_read_backing(address,data,sizeof data)) header="unreadable";
            else {
                for (unsigned j=0;j<8;++j) id|=(uint64_t)data[8+j]<<(j*8);
                header=!memcmp(data,"EVOSLITL",8) && id==s.id ? "verified":"MISMATCH";
            }
        }
        uintptr_t prefix_address=0;
        const char *prefix=rpk_trace_check_prefix(&rpk_trace_config.prefix,s.id,at,length,address,&prefix_address);
        rpk_copy_read_seed(prefix_address,header,prefix);
        fprintf(stderr,"RpkTrace: id=%016llx file-offset=%llu resource-offset=%llu bytes=%llu guest=0x%llx header=%s method=%s position-prefix=%s prefix-guest=0x%llx; read destination only, lifetime/copies unknown\n",
            (unsigned long long)s.id,(unsigned long long)((uint64_t)offset+address-buffer),
            (unsigned long long)at,(unsigned long long)length,(unsigned long long)address,header,method,prefix,(unsigned long long)prefix_address);
        const RpkTracePrefix *q=&rpk_trace_config.prefix;
        const RpkDebugRead debug_read={s.id,(uint64_t)offset,(uint64_t)bytes,buffer,prefix_address,q->stride,q->width,q->samples};
        rpk_request_seed(&debug_read,header,prefix);
        rpk_read_watch_seed(&debug_read,header,prefix);
        rpk_clear_seed(&debug_read,header,prefix);
        rpk_debug_read_note(&debug_read,header,prefix); // Last prefix observation before returning to the read caller.
    }
}
