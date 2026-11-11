/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/runtime_track_probe.h"
#include <assert.h>
#include <stdio.h>

typedef struct Memory { unsigned char bytes[0xa000]; unsigned reads; } Memory;
static bool read_memory(void *ctx,uint64_t address,void *out,size_t size) {
    Memory *m=ctx; ++m->reads;
    if (address>=sizeof m->bytes || size>sizeof m->bytes-address) return false;
    memcpy(out,m->bytes+address,size); return true;
}
static void put_f(Memory *m,size_t address,float value) { memcpy(m->bytes+address,&value,4); }
static void put_u64(Memory *m,size_t address,uint64_t value) { memcpy(m->bytes+address,&value,8); }
static void put_i(Memory *m,size_t address,int32_t value) { memcpy(m->bytes+address,&value,4); }
int main(void) {
    DcTrackProbeGate gate={0};
    assert(dc_track_probe_due(&gate,0,0,0)==DC_TRACK_PROBE_NONE);
    assert(dc_track_probe_due(&gate,1,2,100)==DC_TRACK_PROBE_SAMPLE);
    assert(gate.epoch==1 && gate.samples==1);
    assert(dc_track_probe_due(&gate,1,2,500099)==DC_TRACK_PROBE_NONE);
    assert(dc_track_probe_due(&gate,1,2,500100)==DC_TRACK_PROBE_SAMPLE);
    assert(dc_track_probe_due(&gate,0,0,500101)==DC_TRACK_PROBE_RELEASE);
    assert(dc_track_probe_due(&gate,0,0,500102)==DC_TRACK_PROBE_NONE);
    assert(dc_track_probe_due(&gate,1,2,500103)==DC_TRACK_PROBE_SAMPLE);
    assert(gate.epoch==2); /* reused guest pointers are a new epoch after release */
    while(gate.samples<DC_TRACK_PROBE_MAX_SAMPLES) {
        assert(dc_track_probe_due(&gate,0,0,0)==DC_TRACK_PROBE_RELEASE);
        assert(dc_track_probe_due(&gate,1,2,0)==DC_TRACK_PROBE_SAMPLE);
    }
    assert(dc_track_probe_due(&gate,1,2,0)==DC_TRACK_PROBE_LIMIT);
    assert(dc_track_probe_due(&gate,1,2,0)==DC_TRACK_PROBE_NONE);
    assert(dc_track_probe_due(&gate,0,0,0)==DC_TRACK_PROBE_NONE);

    Memory memory={0};
    const uint64_t state=0x1000,vehicle=0x4000,surface=0x9000;
    for(unsigned w=0;w<4;++w) {
        put_f(&memory,state+0x180+0x40*w,w<2 ? 2 : 0);
        put_f(&memory,state+0x184+0x40*w,3);
        put_f(&memory,state+0x188+0x40*w,w%2 ? -1 : 1);
        put_f(&memory,state+0xa50+4*w,3000);
        for(unsigned k=0;k<3;++k) {
            put_u64(&memory,vehicle+0x5d0*w+0x2e0+8*k,surface);
            put_f(&memory,vehicle+0x5d0*w+0x2f8+4*k,k ? 0 : 1);
        }
    }
    put_f(&memory,surface+0x28,1.25f); put_f(&memory,surface+0x30,0.75f);
    put_i(&memory,surface+0x40,6); put_f(&memory,surface+0x4c,0.01f); put_f(&memory,surface+0x50,0.5f);
    unsigned char original[sizeof memory.bytes]; memcpy(original,memory.bytes,sizeof original);
    DcTrackProbeSample sample;
    dc_track_probe_capture(read_memory,&memory,state,vehicle,&sample);
    assert(sample.pose_valid && sample.center[0]==1 && sample.center[1]==3 && sample.center[2]==0);
    assert(fabs(sample.heading-1.5707963267948966)<1e-9);
    assert(sample.wheels[3].valid && sample.wheels[3].load==3000);
    assert(sample.wheels[0].surfaces[0].valid && sample.wheels[0].surfaces[0].kind==6);
    assert(sample.wheels[0].surfaces[0].grip_28==1.25f);
    assert(memcmp(original,memory.bytes,sizeof original)==0); /* no guest memory writes */
    assert(memory.reads==92); /* fixed upper read count, independent of pointer contents */

    put_f(&memory,state+0x180,NAN);
    put_u64(&memory,vehicle+0x2e0,UINT64_MAX-2);
    put_f(&memory,vehicle+0x2f8,INFINITY);
    dc_track_probe_capture(read_memory,&memory,state,vehicle,&sample);
    assert(!sample.pose_valid && !sample.wheels[0].valid);
    assert(!sample.wheels[0].surfaces[0].valid && !sample.wheels[0].surfaces[0].weight_valid);
    unsigned before=memory.reads; float out;
    assert(!dc_track_probe_read(read_memory,&memory,UINT64_MAX-2,10,&out,4));
    assert(memory.reads==before); /* overflow rejected before callback */
    dc_track_probe_capture(read_memory,&memory,0,0,&sample);
    assert(!sample.pose_valid);
    puts("track probe tests passed: gating, lifecycle, bounded reads, pose, materials, invalid/overflow, read-only");
}
