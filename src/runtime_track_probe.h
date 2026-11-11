/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded, read-only diagnostics for the existing eboot 1.28 player FFB callback.
 * Offsets are those already used by runtime_pad.c; this is not collision injection.
 */
#ifndef DC_RUNTIME_TRACK_PROBE_H
#define DC_RUNTIME_TRACK_PROBE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

#define DC_TRACK_PROBE_INTERVAL_US 500000u
#define DC_TRACK_PROBE_MAX_SAMPLES 600u

typedef struct DcTrackProbeGate {
    uint64_t state, vehicle, last_us;
    unsigned epoch, samples;
    bool exhausted;
} DcTrackProbeGate;
enum { DC_TRACK_PROBE_NONE, DC_TRACK_PROBE_SAMPLE, DC_TRACK_PROBE_RELEASE, DC_TRACK_PROBE_LIMIT };

/* Keep the process-wide sample budget across race restarts; no stale pointer cache. */
static inline int dc_track_probe_due(DcTrackProbeGate *g, uint64_t state, uint64_t vehicle, uint64_t now) {
    if (g->samples >= DC_TRACK_PROBE_MAX_SAMPLES) {
        if (!g->exhausted) { g->exhausted=true; return DC_TRACK_PROBE_LIMIT; }
        return DC_TRACK_PROBE_NONE;
    }
    if (!state || !vehicle) {
        bool had=g->state!=0;
        g->state=g->vehicle=0;
        return had ? DC_TRACK_PROBE_RELEASE : DC_TRACK_PROBE_NONE;
    }
    const bool changed=state!=g->state || vehicle!=g->vehicle;
    if (changed) { ++g->epoch; g->state=state; g->vehicle=vehicle; }
    if (!changed && now>=g->last_us && now-g->last_us<DC_TRACK_PROBE_INTERVAL_US) return DC_TRACK_PROBE_NONE;
    g->last_us=now; ++g->samples;
    return DC_TRACK_PROBE_SAMPLE;
}

typedef bool (*DcTrackProbeRead)(void *context, uint64_t address, void *out, size_t size);
typedef struct DcTrackProbeSurface {
    uint64_t address;
    float weight, grip_28, grip_30, bump_height, bump_length;
    int32_t kind;
    bool weight_valid, valid;
} DcTrackProbeSurface;
typedef struct DcTrackProbeWheel {
    float position[3], load;
    bool valid;
    DcTrackProbeSurface surfaces[3];
} DcTrackProbeWheel;
typedef struct DcTrackProbeSample {
    DcTrackProbeWheel wheels[4];
    double center[3], heading;
    bool pose_valid;
} DcTrackProbeSample;

static inline bool dc_track_probe_read(DcTrackProbeRead read, void *ctx, uint64_t base, uint64_t off, void *out, size_t size) {
    if (!base || off>UINT64_MAX-base || size>UINT64_MAX-(base+off)) return false;
    return read(ctx,base+off,out,size);
}
static inline bool dc_track_probe_floats(const float *v, size_t n) {
    for (size_t i=0;i<n;++i) if (!isfinite(v[i])) return false;
    return true;
}
static inline void dc_track_probe_capture(DcTrackProbeRead read, void *ctx, uint64_t state, uint64_t vehicle, DcTrackProbeSample *out) {
    memset(out,0,sizeof *out);
    out->pose_valid=true;
    for (unsigned w=0;w<4;++w) {
        DcTrackProbeWheel *wheel=&out->wheels[w];
        wheel->valid=dc_track_probe_read(read,ctx,state,0x180+0x40*w,wheel->position,12) &&
            dc_track_probe_read(read,ctx,state,0xa50+4*w,&wheel->load,4) &&
            dc_track_probe_floats(wheel->position,3) && isfinite(wheel->load);
        if (!wheel->valid) out->pose_valid=false;
        for (unsigned s=0;s<3;++s) {
            DcTrackProbeSurface *surface=&wheel->surfaces[s];
            surface->weight_valid=dc_track_probe_read(read,ctx,vehicle,0x5d0*w+0x2f8+4*s,&surface->weight,4) && isfinite(surface->weight);
            if (!dc_track_probe_read(read,ctx,vehicle,0x5d0*w+0x2e0+8*s,&surface->address,8) || !surface->address) continue;
            surface->valid=dc_track_probe_read(read,ctx,surface->address,0x28,&surface->grip_28,4) &&
                dc_track_probe_read(read,ctx,surface->address,0x30,&surface->grip_30,4) &&
                dc_track_probe_read(read,ctx,surface->address,0x40,&surface->kind,4) &&
                dc_track_probe_read(read,ctx,surface->address,0x4c,&surface->bump_height,4) &&
                dc_track_probe_read(read,ctx,surface->address,0x50,&surface->bump_length,4) &&
                isfinite(surface->grip_28) && isfinite(surface->grip_30) && isfinite(surface->bump_height) && isfinite(surface->bump_length);
        }
    }
    if (out->pose_valid) {
        for (unsigned k=0;k<3;++k) for (unsigned w=0;w<4;++w) out->center[k]+=out->wheels[w].position[k]/4.0;
        double dx=((double)out->wheels[0].position[0]+out->wheels[1].position[0]-out->wheels[2].position[0]-out->wheels[3].position[0])/2;
        double dz=((double)out->wheels[0].position[2]+out->wheels[1].position[2]-out->wheels[2].position[2]-out->wheels[3].position[2])/2;
        if (dx*dx+dz*dz<1e-12) out->pose_valid=false;
        else out->heading=atan2(dx,dz);
    }
}
#endif
