// Optional, bounded read-only marker probe. A recorded draw is not proof of visible pixels.
#pragma once
#include "bbport_diagnostics.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <atomic>
#include <cmath>
#include <string_view>

extern "C" int runtime_memory_read_backing(uintptr_t, void*, uint64_t);
namespace BbMeshProbe {
using Read = int (*)(uintptr_t, void*, uint64_t);
struct Spec {
    uint32_t stride=0, width=0, samples=0, count=0, first=0;
    std::array<unsigned char,96> positions{};
    std::array<unsigned char,48> indices{};
};
inline bool Parse(std::string_view text, Spec& output) {
    Spec s;
    if (text.size()>400 || text.substr(0,3)!="v1:") return false;
    text.remove_prefix(3);
    for (auto* number : {&s.stride,&s.width,&s.samples,&s.count,&s.first}) {
        size_t i=0;
        while (i<text.size() && text[i]>='0' && text[i]<='9') {
            if (*number>1000000) return false;
            *number=*number*10+uint32_t(text[i++]-'0');
        }
        if (!i || i==text.size() || text[i]!=':') return false;
        text.remove_prefix(i+1);
    }
    if ((s.width!=6 && s.width!=12) || s.stride<s.width || s.stride>4096 ||
        s.samples<3 || s.samples>8 || !s.count || s.count%3 || s.count>1000000 || s.first>65535) return false;
    auto hex=[&](unsigned char* out,size_t bytes) {
        if (text.size()<bytes*2) return false;
        for(size_t i=0;i<bytes*2;++i) {
            const char c=text[i];
            int n=c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : -1;
            if(n<0) return false;
            out[i/2]=static_cast<unsigned char>((out[i/2]<<4)|n);
        }
        text.remove_prefix(bytes*2); return true;
    };
    if (!hex(s.positions.data(),s.width*s.samples) || text.empty() || text.front()!=':') return false;
    text.remove_prefix(1);
    if (!hex(s.indices.data(),std::min(24u,s.count)*2) || !text.empty()) return false;
    output=s; return true;
}
// Values describe CPU backing bytes, not GPU completion or shader semantics.
enum class Gate { Match, Stride, Format, Base, Range, Read, Prefix, Width };
struct Indices { std::array<uint32_t,24> values{}; uint32_t size=0; };
inline Gate ReadIndices(uintptr_t address,uint32_t width,uint32_t count,Indices& out,Read read) {
    if(width!=2 && width!=4) return Gate::Width;
    const uint32_t n=std::min(24u,count), bytes=n*width;
    if(!n || address>UINTPTR_MAX-bytes) return Gate::Range;
    std::array<unsigned char,96> raw{};
    if(!read(address,raw.data(),bytes)) return Gate::Read;
    out={}; out.size=n;
    for(uint32_t i=0;i<n;++i)
        for(uint32_t k=0;k<width;++k) out.values[i]|=uint32_t(raw[i*width+k])<<(8*k);
    return Gate::Match;
}
inline bool IndexDelta(const Spec& s,const Indices& actual,int64_t& delta) {
    if(actual.size!=std::min(24u,s.count)) return false;
    for(uint32_t i=0;i<actual.size;++i) {
        const uint32_t expected=uint32_t(s.indices[2*i])|uint32_t(s.indices[2*i+1])<<8;
        const int64_t difference=int64_t(actual.values[i])-expected;
        if(!i) delta=difference;
        else if(difference!=delta) return false;
    }
    return true;
}
struct Binding {
    uintptr_t address=0; uint64_t size=0;
    uint32_t stride=0, base_vertex=0, width=0, format=0, location=0, offset=0;
    bool per_vertex=true;
};
inline Gate ReadVertex(const Binding& b,int64_t vertex,void* bytes,Read read) {
    if(!b.per_vertex || (b.width!=6 && b.width!=12)) return Gate::Format;
    if(!b.stride || b.stride>4096) return Gate::Stride;
    if(vertex<0) return Gate::Base;
    if(uint64_t(vertex)>(UINT64_MAX-b.offset-b.width)/b.stride) return Gate::Range;
    const uint64_t start=uint64_t(vertex)*b.stride+b.offset, end=start+b.width;
    if(end>b.size || b.address>UINTPTR_MAX-end) return Gate::Range;
    return read(b.address+start,bytes,b.width) ? Gate::Match : Gate::Read;
}
inline Gate Positions(const Spec& s,const Binding& b,int64_t delta,Read read) {
    if(b.stride!=s.stride) return Gate::Stride;
    if(!b.per_vertex || b.width!=s.width) return Gate::Format;
    const int64_t first=int64_t(int32_t(b.base_vertex))+s.first+delta;
    std::array<unsigned char,12> row{};
    for(uint32_t i=0;i<s.samples;++i) {
        Gate g=ReadVertex(b,first+i,row.data(),read);
        if(g!=Gate::Match) return g;
        if(std::memcmp(row.data(),s.positions.data()+i*s.width,s.width)) return Gate::Prefix;
    }
    return Gate::Match;
}
// Decode only the float/half XYZ formats explicitly identified by the renderer.
inline bool FiniteXYZ(const unsigned char* bytes,uint32_t width) {
    for(unsigned i=0;i<3;++i) {
        if(width==6) {
            const uint16_t h=uint16_t(bytes[2*i])|uint16_t(bytes[2*i+1])<<8;
            if((h&0x7c00)==0x7c00) return false;
        } else if(width==12) {
            float v; std::memcpy(&v,bytes+4*i,4); if(!std::isfinite(v)) return false;
        } else return false;
    }
    return true;
}
struct Control {
    bool armed=false, captured=false, validated=false;
    uint32_t attempts=0, count=0, width=0, format=0, stride=0, location=0, offset=0;
    uint64_t serial=0, vs=0, ps=0;
    Indices indices;
    std::array<uint32_t,3> vertices{};
    std::array<unsigned char,36> bytes{};
};
inline bool ControlSample(Control& control,bool capture,uint64_t serial,uint64_t vs,uint64_t ps,
                          uint32_t count,const Indices& indices,const Binding& b,Read read) {
    if(!capture && (serial<=control.serial || vs!=control.vs || ps!=control.ps || count!=control.count ||
        b.width!=control.width || b.format!=control.format || b.stride!=control.stride ||
        b.location!=control.location || b.offset!=control.offset ||
        indices.size!=control.indices.size || indices.values!=control.indices.values)) return false;
    std::array<uint32_t,3> selected{}; unsigned n=0;
    for(uint32_t i=0;i<indices.size && n<3;++i) {
        bool duplicate=false;
        for(unsigned j=0;j<n;++j) duplicate|=selected[j]==indices.values[i];
        if(!duplicate) selected[n++]=indices.values[i];
    }
    if(n!=3) return false;
    std::array<unsigned char,36> bytes{};
    for(unsigned i=0;i<3;++i) {
        if(ReadVertex(b,int64_t(int32_t(b.base_vertex))+selected[i],bytes.data()+i*12,read)!=Gate::Match ||
           !FiniteXYZ(bytes.data()+i*12,b.width)) return false;
    }
    if(!capture) return selected==control.vertices && bytes==control.bytes;
    control.captured=true; control.serial=serial; control.vs=vs; control.ps=ps;
    control.count=count; control.width=b.width; control.format=b.format; control.stride=b.stride;
    control.location=b.location; control.offset=b.offset; control.indices=indices;
    control.vertices=selected; control.bytes=bytes;
    return true;
}
struct Config { bool enabled=false, offline=false; Spec spec; const char* trigger=nullptr; };
inline const Config& Configuration() {
    static const Config config=[] {
        Config c;
        if(const char* e=BB_DIAGNOSTIC_ENV("BB_MESH_PROBE")) {
            c.offline=Parse(e,c.spec);
            if(!c.offline) std::fputs("MeshProbe: invalid offline signature; offline matching disabled\n",stderr);
        }
        c.trigger=BB_DIAGNOSTIC_ENV("BB_MESH_PROBE_CONTROL");
        if(c.trigger && !*c.trigger) c.trigger=nullptr;
        c.enabled=c.offline || c.trigger;
        if(c.enabled) std::fprintf(stderr,"MeshProbe: progressive-v2 enabled offline=%d control=%s; CPU backing bytes at direct draw recording, not visibility\n",
                                  int(c.offline),c.trigger ? "waiting-for-trigger-file" : "off");
        return c;
    }();
    return config;
}
inline std::atomic<unsigned> next_lane{0};
struct State {
    unsigned lane=next_lane.fetch_add(1), binding_count=0, detail_logs=0, vertex_logs=0;
    bool indexed=false, bound=false;
    uint32_t count=0, index_width=0, index_offset=0;
    uintptr_t index=0, index_base=0;
    std::array<Binding,32> bindings{};
    Control control;
    uint64_t direct=0, indirect=0, serial=0, nonindexed=0, count_mismatch=0, count_equal=0,
        missing_index=0, index_read=0, index_range=0, index_type=0, index_prefix=0,
        index_exact=0, index_rebased=0, stride=0, format=0, base=0, range=0, vertex_read=0,
        position_prefix=0, position_unpaired=0, exact_recorded=0, rebased_recorded=0,
        no_bindings=0, binding_overflow=0, stale_record=0, control_index_fail=0, base_zero_match=0;
    std::chrono::steady_clock::time_point last=std::chrono::steady_clock::now(), poll=last-std::chrono::seconds(2);
};
inline thread_local State state;
inline void Progress() {
    const auto now=std::chrono::steady_clock::now();
    if(now-state.last<std::chrono::seconds(5)) return;
    auto& s=state;
    std::fprintf(stderr,"MeshProbe: lane=%u direct=%llu indirect-unchecked=%llu nonindexed=%llu count-equal=%llu count-mismatch=%llu missing-index=%llu index-read-fail=%llu index-range=%llu index-type=%llu index-prefix-mismatch=%llu index-exact=%llu index-rebased=%llu no-bindings=%llu binding-overflow=%llu stride=%llu format-or-rate=%llu base-negative=%llu vertex-range=%llu vertex-read-fail=%llu position-prefix-mismatch=%llu position-unpaired=%llu exact-recorded=%llu rebased-recorded=%llu stale-record=%llu base-zero-only=%llu control-attempts=%u control-index-fail=%llu control-captured=%d control-validated=%d\n",
        s.lane,(unsigned long long)s.direct,(unsigned long long)s.indirect,(unsigned long long)s.nonindexed,
        (unsigned long long)s.count_equal,(unsigned long long)s.count_mismatch,(unsigned long long)s.missing_index,
        (unsigned long long)s.index_read,(unsigned long long)s.index_range,(unsigned long long)s.index_type,
        (unsigned long long)s.index_prefix,(unsigned long long)s.index_exact,(unsigned long long)s.index_rebased,
        (unsigned long long)s.no_bindings,(unsigned long long)s.binding_overflow,(unsigned long long)s.stride,
        (unsigned long long)s.format,(unsigned long long)s.base,(unsigned long long)s.range,
        (unsigned long long)s.vertex_read,(unsigned long long)s.position_prefix,(unsigned long long)s.position_unpaired,
        (unsigned long long)s.exact_recorded,(unsigned long long)s.rebased_recorded,(unsigned long long)s.stale_record,
        (unsigned long long)s.base_zero_match,
        s.control.attempts,(unsigned long long)s.control_index_fail,int(s.control.captured),int(s.control.validated));
    s.last=now;
}
inline void Begin(bool indexed,uint32_t count,bool indirect=false) {
    const auto& c=Configuration(); if(!c.enabled) return;
    state.indexed=indexed && !indirect; state.bound=false; state.binding_count=0; state.count=count;
    ++state.serial;
    if(indirect) ++state.indirect; else ++state.direct;
    Progress();
    const auto now=std::chrono::steady_clock::now();
    if(c.trigger && !state.control.armed && now-state.poll>=std::chrono::seconds(1)) {
        state.poll=now;
        if(FILE* f=std::fopen(c.trigger,"rb")) {
            std::fclose(f); state.control.armed=true;
            std::fprintf(stderr,"MeshProbe: lane=%u positive-control armed; budget=4096 sampled draws\n",state.lane);
        }
    }
}
inline void IndexBound(uintptr_t address,uint32_t width,uintptr_t base,uint32_t offset) {
    if(!Configuration().enabled) return;
    state.index=address; state.index_width=width; state.index_base=base; state.index_offset=offset; state.bound=true;
}
inline void Observe(const Binding& b) {
    const auto& c=Configuration();
    if(!c.enabled || !state.indexed) return;
    const bool candidate=c.offline && state.count==c.spec.count;
    const auto& control=state.control;
    if(!candidate && !(control.armed && !control.validated && control.attempts<4096 && state.count>=12 &&
                       (!control.captured || state.count==control.count))) return;
    if(state.binding_count<state.bindings.size()) state.bindings[state.binding_count++]=b;
    else ++state.binding_overflow;
}
inline void Recorded(uint64_t vs,uint64_t ps,uint32_t count,uint32_t instances) {
    const auto& c=Configuration(); if(!c.enabled) return;
    auto& s=state;
    if(!s.indexed) { ++s.nonindexed; return; }
    s.indexed=false; // Never reuse this draw's state on another callback.
    if(count!=s.count) { ++s.stale_record; return; }
    const bool offline=c.offline && count==c.spec.count;
    if(c.offline) ++(offline ? s.count_equal : s.count_mismatch);
    auto& control=s.control;
    const bool trial=control.armed && !control.validated && control.attempts<4096 && count>=12 &&
        (!control.captured || (count==control.count && vs==control.vs && ps==control.ps));
    if(!offline && !trial) return;
    if(!s.bound) { ++s.missing_index; return; }
    if(trial) ++control.attempts;
    Indices indices;
    const Gate gate=ReadIndices(s.index,s.index_width,count,indices,runtime_memory_read_backing);
    int64_t delta=0;
    const bool index_match=gate==Gate::Match && offline && IndexDelta(c.spec,indices,delta);
    if(offline) {
        if(gate==Gate::Read) ++s.index_read;
        else if(gate==Gate::Range) ++s.index_range;
        else if(gate==Gate::Width) ++s.index_type;
        else if(!index_match) ++s.index_prefix;
        else ++(delta ? s.index_rebased : s.index_exact);
        if(s.detail_logs<6) {
            ++s.detail_logs;
            std::fprintf(stderr,"MeshProbe: lane=%u candidate ib=0x%llx base=0x%llx element-offset=%u width=%u count=%u read-status=%d index-match=%d delta=%lld prefix=%u,%u,%u expected=%u,%u,%u vs=%llx ps=%llx\n",
                s.lane,(unsigned long long)s.index,(unsigned long long)s.index_base,s.index_offset,s.index_width,count,
                int(gate),int(index_match),(long long)(index_match ? delta : 0),indices.values[0],indices.values[1],indices.values[2],
                unsigned(c.spec.indices[0]|unsigned(c.spec.indices[1])<<8),
                unsigned(c.spec.indices[2]|unsigned(c.spec.indices[3])<<8),
                unsigned(c.spec.indices[4]|unsigned(c.spec.indices[5])<<8),
                (unsigned long long)vs,(unsigned long long)ps);
        }
    }
    if(trial && gate!=Gate::Match) ++s.control_index_fail;
    if(!s.binding_count) ++s.no_bindings;
    bool position_match=false;
    for(unsigned i=0;i<s.binding_count;++i) {
        const Binding& b=s.bindings[i];
        if(offline && !position_match) {
            Gate p=Positions(c.spec,b,index_match ? delta : 0,runtime_memory_read_backing);
            if(s.vertex_logs<6) {
                ++s.vertex_logs;
                std::fprintf(stderr,"MeshProbe: lane=%u vertex vb=0x%llx bytes=%llu stride=%u format=%u xyz-width=%u location=%u attr-offset=%u base-vertex=%d per-vertex=%d status=%d\n",
                    s.lane,(unsigned long long)b.address,(unsigned long long)b.size,b.stride,b.format,b.width,b.location,
                    b.offset,int32_t(b.base_vertex),int(b.per_vertex),int(p));
            }
            if(b.base_vertex && (p==Gate::Prefix || p==Gate::Base || p==Gate::Range)) {
                Binding zero=b; zero.base_vertex=0;
                if(Positions(c.spec,zero,index_match ? delta : 0,runtime_memory_read_backing)==Gate::Match)
                    ++s.base_zero_match; // Diagnostic only; never promotes a draw to a match.
            }
            switch(p) {
            case Gate::Stride: ++s.stride; break;
            case Gate::Format: ++s.format; break;
            case Gate::Base: ++s.base; break;
            case Gate::Range: ++s.range; break;
            case Gate::Read: ++s.vertex_read; break;
            case Gate::Prefix: ++s.position_prefix; break;
            case Gate::Match: position_match=true; break;
            default: break;
            }
        }
        if(trial && gate==Gate::Match && !control.validated) {
            if(!control.captured) {
                if(ControlSample(control,true,s.serial,vs,ps,count,indices,b,runtime_memory_read_backing))
                    std::fprintf(stderr,"MeshProbe: lane=%u positive-control captured serial=%llu count=%u format=%u stride=%u location=%u attr-offset=%u base-vertex=%d vb=0x%llx ib=0x%llx vs=%llx ps=%llx; requires later replay\n",
                        s.lane,(unsigned long long)s.serial,count,b.format,b.stride,b.location,b.offset,int32_t(b.base_vertex),
                        (unsigned long long)b.address,(unsigned long long)s.index,(unsigned long long)vs,(unsigned long long)ps);
            } else if(ControlSample(control,false,s.serial,vs,ps,count,indices,b,runtime_memory_read_backing)) {
                control.validated=true;
                std::fprintf(stderr,"MeshProbe: lane=%u positive-control REPLAY-MATCH capture=%llu replay=%llu; fresh index and three referenced XYZ byte samples; not donor identity or GPU completion\n",
                    s.lane,(unsigned long long)control.serial,(unsigned long long)s.serial);
            }
        }
    }
    if(position_match) {
        if(!index_match) ++s.position_unpaired;
        else {
            auto& matches=delta ? s.rebased_recorded : s.exact_recorded;
            if(++matches<=10) std::fprintf(stderr,"MeshProbe: lane=%u offline-%s draw-recorded #%llu delta=%lld count=%u instances=%u vs=%llx ps=%llx; no visibility claim\n",
                s.lane,delta ? "rebased" : "exact",(unsigned long long)matches,(long long)delta,count,instances,
                (unsigned long long)vs,(unsigned long long)ps);
        }
    }
    if(trial && control.attempts==4096 && !control.validated)
        std::fprintf(stderr,"MeshProbe: lane=%u positive-control budget exhausted without replay; restart for another sample\n",s.lane);
}
} // namespace BbMeshProbe
