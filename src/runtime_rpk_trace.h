/* Opt-in read-only loose-RPK range provenance. No guest buffer is dereferenced. */
#pragma once
#include "../gpu/shim/bbport_diagnostics.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

typedef struct { uint64_t id, offset, size; } RpkTraceSpan;
typedef struct { char path[512]; unsigned count; RpkTraceSpan spans[8]; } RpkTraceConfig;
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
    if (strncmp(text,"rpk-trace-v1\n",13)) return 0;
    char *p=text+13, *end=strchr(p,'\n');
    if (!end || end-p>=511 || strncmp(p,"/app0/",6)) return 0;
    memcpy(c.path,p,(size_t)(end-p));
    if (strstr(c.path,"..") || strchr(c.path,'\r')) return 0;
    p=end+1;
    while (*p) {
        if (c.count==8 || !(end=strchr(p,'\n'))) return 0;
        *end=0;
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
static void rpk_trace_init(void) {
    const char *path=BB_DIAGNOSTIC_ENV("BB_RPK_TRACE_FILE");
    if (!path || !*path) return;
    FILE *f=fopen(path,"rb"); char data[4097]; size_t size=0;
    if (f) { size=fread(data,1,4096,f); int bad=ferror(f); fclose(f); if (bad) size=0; }
    data[size]=0;
    if (!size || size==4096 || strlen(data)!=size || !rpk_trace_parse(data,&rpk_trace_config)) {
        fputs("RpkTrace: invalid/unreadable manifest; disabled\n",stderr); return;
    }
    fprintf(stderr,"RpkTrace: enabled path=%s resources=%u limit=64; file reads only, not native selection\n",
            rpk_trace_config.path,rpk_trace_config.count);
}
static int rpk_trace_watches(const char *path) {
    pthread_once(&rpk_trace_once,rpk_trace_init);
    return rpk_trace_config.count && !strcmp(path,rpk_trace_config.path);
}
extern int runtime_memory_read_backing(uintptr_t,void*,uint64_t);
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
        fprintf(stderr,"RpkTrace: id=%016llx file-offset=%llu resource-offset=%llu bytes=%llu guest=0x%llx header=%s method=%s; read destination only, lifetime/copies unknown\n",
            (unsigned long long)s.id,(unsigned long long)((uint64_t)offset+address-buffer),
            (unsigned long long)at,(unsigned long long)length,(unsigned long long)address,header,method);
    }
}
