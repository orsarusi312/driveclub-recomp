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
inline bool IndexMatch(const Spec& s, uintptr_t address, uint32_t width, Read read) {
    if(width!=2 && width!=4) return false;
    const size_t n=std::min(24u,s.count), bytes=n*width;
    std::array<unsigned char,96> data{};
    if(address>UINTPTR_MAX-bytes || !read(address,data.data(),bytes)) return false;
    for(size_t i=0;i<n;++i) {
        uint32_t actual=0;
        for(uint32_t k=0;k<width;++k) actual|=uint32_t(data[i*width+k])<<(8*k);
        if(actual!=(uint32_t(s.indices[2*i])|uint32_t(s.indices[2*i+1])<<8)) return false;
    }
    return true;
}
inline bool PositionMatch(const Spec& s, uintptr_t address, uint64_t size,
                          uint32_t stride, uint32_t first_vertex, Read read) {
    const int64_t vertex=int64_t(int32_t(first_vertex))+s.first;
    if(stride!=s.stride || vertex<0) return false;
    const uint64_t start=uint64_t(vertex)*stride;
    const uint64_t end=start+uint64_t(s.samples-1)*stride+s.width;
    if(end>size || address>UINTPTR_MAX-end) return false;
    std::array<unsigned char,12> row{};
    for(uint32_t i=0;i<s.samples;++i) {
        if(!read(address+start+uint64_t(i)*stride,row.data(),s.width) ||
           std::memcmp(row.data(),s.positions.data()+i*s.width,s.width)) return false;
    }
    return true;
}
struct Config { bool enabled=false; Spec spec; };
inline const Config& Configuration() {
    static const Config config=[] {
        Config c;
        if(const char* e=BB_DIAGNOSTIC_ENV("BB_MESH_PROBE")) {
            c.enabled=Parse(e,c.spec);
            std::fprintf(stderr,"MeshProbe: %s; direct indexed draw recording only; no visibility claim\n",
                         c.enabled ? "enabled" : "invalid signature, disabled");
        }
        return c;
    }();
    return config;
}
struct State {
    bool eligible=false, matched=false;
    uintptr_t index=0, vertex=0;
    uint64_t direct=0, indirect=0, index_matches=0, recorded=0, unread_or_mismatch=0;
    std::chrono::steady_clock::time_point last=std::chrono::steady_clock::now();
};
inline thread_local State state;
inline void Begin(bool indexed,uint32_t count,uintptr_t index,uint32_t width,bool indirect=false) {
    const auto& c=Configuration();
    if(!c.enabled) return;
    state.eligible=state.matched=false;
    if(indirect) ++state.indirect; else ++state.direct;
    if(!indirect && indexed && count==c.spec.count) {
        state.eligible=IndexMatch(c.spec,index,width,runtime_memory_read_backing);
        if(state.eligible) { ++state.index_matches; state.index=index; }
        else ++state.unread_or_mismatch;
    }
    const auto now=std::chrono::steady_clock::now();
    if(now-state.last>=std::chrono::seconds(5)) {
        std::fprintf(stderr,"MeshProbe: progress direct=%llu indirect-unchecked=%llu index-prefix=%llu draw-recorded=%llu index-unread-or-mismatch=%llu\n",
            (unsigned long long)state.direct,(unsigned long long)state.indirect,
            (unsigned long long)state.index_matches,(unsigned long long)state.recorded,
            (unsigned long long)state.unread_or_mismatch);
        state.last=now;
    }
}
inline void Observe(uintptr_t address,uint64_t size,uint32_t stride,uint32_t first_vertex) {
    if(!state.eligible || state.matched) return;
    if(PositionMatch(Configuration().spec,address,size,stride,first_vertex,runtime_memory_read_backing)) {
        state.matched=true;
        state.vertex=address+uint64_t(int64_t(int32_t(first_vertex))+Configuration().spec.first)*stride;
    }
}
inline void Recorded(uint64_t vs,uint64_t ps,uint32_t count,uint32_t instances) {
    if(state.eligible && state.matched && count==Configuration().spec.count) {
        if(++state.recorded<=20)
            std::fprintf(stderr,"MeshProbe: draw-recorded signature-match #%llu vb=0x%llx ib=0x%llx indices=%u instances=%u vs=%llx ps=%llx (not GPU completion or visibility)\n",
                (unsigned long long)state.recorded,(unsigned long long)state.vertex,
                (unsigned long long)state.index,count,instances,(unsigned long long)vs,(unsigned long long)ps);
    }
    state.eligible=false;
}
} // namespace BbMeshProbe
