// Bounded donor-byte search in actual vertex bindings, independent of draw counts.
#pragma once
#include "bbport_mesh_probe.h"
namespace BbMeshSearch {
using namespace BbMeshProbe;
constexpr uint64_t ByteBudget=64ull*1024*1024, BindingLimit=2ull*1024*1024;
struct Result { bool found=false, truncated=false; uint64_t vertex=0, bytes=0, failures=0; };
inline Result Find(const Spec& spec,const Binding& b,uint64_t budget,Read read) {
    Result result;
    if(!b.per_vertex || b.width!=spec.width || b.stride<b.width || b.stride>4096 ||
       b.offset>b.size || b.size-b.offset<b.width || b.address>UINTPTR_MAX-b.size) return result;
    const uint64_t vertices=(b.size-b.offset-b.width)/b.stride+1;
    if(vertices<spec.samples) return result;
    const uint64_t candidates=vertices-spec.samples+1;
    std::array<unsigned char,65536> block{};
    const uint64_t overlap=uint64_t(spec.samples-1)*b.stride+b.width;
    const uint64_t batch=(block.size()-overlap)/b.stride+1;
    const uint64_t cap=std::min(budget,BindingLimit);
    for(uint64_t first=0;first<candidates;) {
        const uint64_t n=std::min(batch,candidates-first), bytes=(n-1)*b.stride+overlap;
        if(bytes>cap-result.bytes) { result.truncated=true; break; }
        result.bytes+=bytes;
        if(!read(b.address+b.offset+first*b.stride,block.data(),bytes)) ++result.failures;
        else for(uint64_t j=0;j<n;++j) {
            bool match=true;
            for(uint32_t k=0;k<spec.samples && match;++k)
                match=!std::memcmp(block.data()+(j+k)*b.stride,spec.positions.data()+k*spec.width,spec.width);
            if(match) { result.found=true; result.vertex=first+j; return result; }
        }
        first+=n;
    }
    return result;
}
inline bool Fresh(const Spec& spec,const Binding& b,uint64_t vertex,Read read) {
    if(vertex>uint64_t(INT64_MAX)-spec.samples) return false;
    std::array<unsigned char,12> bytes{};
    for(uint32_t k=0;k<spec.samples;++k)
        if(ReadVertex(b,int64_t(vertex+k),bytes.data(),read)!=Gate::Match ||
           std::memcmp(bytes.data(),spec.positions.data()+k*spec.width,spec.width)) return false;
    return true;
}
inline unsigned Referenced(const Spec& spec,const Binding& b,uint64_t vertex,const Indices& indices) {
    unsigned count=0;
    for(uint32_t i=0;i<indices.size;++i) {
        const int64_t effective=int64_t(int32_t(b.base_vertex))+indices.values[i];
        if(effective>=0 && uint64_t(effective)>=vertex && uint64_t(effective)-vertex<spec.samples) ++count;
    }
    return count;
}
struct Entry { Binding binding; Result result; };
struct SearchState {
    std::array<Entry,1024> cache{};
    std::array<uint16_t,2048> lookup{}; // index+1, bounded open addressing, at most half full
    std::array<Entry,32> hits{};
    unsigned used=0, hit_count=0, supported=0, logs=0, checks=0;
    uint64_t bytes=0, failures=0, truncated=0, unsupported=0, skips=0, full=0,
             direct=0, indirect=0, found=0, referenced=0, stale=0, no_supported_bindings=0;
    bool armed=false, indirect_draw=false, budget_reported=false;
    std::chrono::steady_clock::time_point poll{}, last=std::chrono::steady_clock::now();
};
inline thread_local SearchState search;
inline const char* Trigger() {
    static const char* value=[] {
        const char* p=BB_DIAGNOSTIC_ENV("BB_MESH_SEARCH");
        if(!p || !*p) return static_cast<const char*>(nullptr);
        if(!Configuration().offline) { std::fputs("MeshSearch: requires valid BB_MESH_PROBE signature; disabled\n",stderr); return static_cast<const char*>(nullptr); }
        std::fputs("MeshSearch: waiting for trigger file; 64 MiB/lane, 2 MiB/binding, 1024 bindings, 4096 hit checks; CPU bytes only\n",stderr);
        return p;
    }();
    return value;
}
inline void Begin(bool indirect) {
    if(!Trigger()) return;
    auto& s=search; s.hit_count=0; s.supported=0; s.indirect_draw=indirect;
    const auto now=std::chrono::steady_clock::now();
    if(!s.armed && now-s.poll>=std::chrono::seconds(1)) {
        s.poll=now;
        if(FILE* f=std::fopen(Trigger(),"rb")) { std::fclose(f); s.armed=true;
            std::fprintf(stderr,"MeshSearch: lane=%u armed\n",state.lane); }
    }
    if(!s.armed) return;
    if(indirect) ++s.indirect; else ++s.direct;
    if(now-s.last>=std::chrono::seconds(5)) {
        std::fprintf(stderr,"MeshSearch: lane=%u direct=%llu indirect=%llu bindings=%u bytes=%llu read-fail=%llu truncated=%llu unsupported=%llu cache-skips=%llu cache-full=%llu prefix-copies=%llu referenced-prefix=%llu stale=%llu no-supported-bindings=%llu checks=%u\n",
            state.lane,(unsigned long long)s.direct,(unsigned long long)s.indirect,s.used,(unsigned long long)s.bytes,
            (unsigned long long)s.failures,(unsigned long long)s.truncated,(unsigned long long)s.unsupported,
            (unsigned long long)s.skips,(unsigned long long)s.full,(unsigned long long)s.found,
            (unsigned long long)s.referenced,(unsigned long long)s.stale,(unsigned long long)s.no_supported_bindings,s.checks);
        s.last=now;
    }
}
inline bool Same(const Binding& a,const Binding& b) {
    return a.address==b.address && a.size==b.size && a.stride==b.stride && a.width==b.width &&
           a.offset==b.offset && a.format==b.format && a.per_vertex==b.per_vertex;
}
inline uint64_t Key(const Binding& b) {
    uint64_t h=b.address ^ (b.size<<17) ^ (uint64_t(b.stride)<<48) ^ b.offset ^ b.format;
    h^=h>>30; h*=UINT64_C(0xbf58476d1ce4e5b9); h^=h>>27;
    return h;
}
inline void Observe(const Binding& b) {
    if(!Trigger() || !search.armed) return;
    auto& s=search; const auto& spec=Configuration().spec;
    if(!b.per_vertex || b.width!=spec.width || b.stride<b.width || b.stride>4096) { ++s.unsupported; return; }
    ++s.supported;
    const Entry* entry=nullptr;
    size_t slot=Key(b)%s.lookup.size();
    while(s.lookup[slot]) {
        const auto& cached=s.cache[s.lookup[slot]-1];
        if(Same(cached.binding,b)) { entry=&cached; ++s.skips; break; }
        slot=(slot+1)%s.lookup.size();
    }
    if(!entry) {
        if(s.used==s.cache.size()) { ++s.full; return; }
        if(s.bytes>=ByteBudget) return;
        auto& added=s.cache[s.used++]; added.binding=b;
        s.lookup[slot]=uint16_t(s.used);
        added.result=Find(spec,b,ByteBudget-s.bytes,runtime_memory_read_backing);
        s.bytes+=added.result.bytes; s.failures+=added.result.failures; s.truncated+=added.result.truncated;
        if(added.result.found) ++s.found;
        entry=&added;
        // A remaining budget smaller than one window cannot be used for this binding.
        if(added.result.truncated && ByteBudget-s.bytes<65536 && !s.budget_reported) {
            s.budget_reported=true;
            std::fprintf(stderr,"MeshSearch: lane=%u scan budget nearly/exhausted; zeros are incomplete coverage\n",state.lane);
        }
    }
    if(entry->result.found && s.hit_count<s.hits.size()) {
        s.hits[s.hit_count]=*entry; s.hits[s.hit_count++].binding=b;
    }
}
// Invoked only after the draw command is handed to the scheduler, on its owning lane.
inline void Recorded(uint64_t vs,uint64_t ps,uint32_t count,uintptr_t args=0,uint32_t stride=0,
                     uint32_t max_count=0,uintptr_t count_address=0,bool indexed=true) {
    if(!Trigger() || !search.armed) return;
    auto& s=search; const auto& spec=Configuration().spec;
    if(!s.supported) ++s.no_supported_bindings;
    for(unsigned i=0;i<s.hit_count && s.checks<4096;++i) {
        ++s.checks; const auto& hit=s.hits[i];
        if(!Fresh(spec,hit.binding,hit.result.vertex,runtime_memory_read_backing)) { ++s.stale; continue; }
        Indices indices; Gate gate=Gate::Range; unsigned touched=0;
        if(!s.indirect_draw && indexed && state.bound) {
            gate=ReadIndices(state.index,state.index_width,count,indices,runtime_memory_read_backing);
            if(gate==Gate::Match) touched=Referenced(spec,hit.binding,hit.result.vertex,indices);
        }
        if(touched) ++s.referenced;
        if(s.logs++<16) std::fprintf(stderr,"MeshSearch: lane=%u path=%s position-prefix-copy vb=0x%llx bytes=%llu vertex=%llu stride=%u location=%u attr-offset=%u base-vertex=%d count=%u indexed=%d ib=0x%llx element-offset=%u index-status=%d prefix=%u,%u,%u referenced-prefix-indices=%u vs=%llx ps=%llx args=0x%llx arg-stride=%u max-count=%u count-address=0x%llx; shared buffer bytes, not donor selection or visibility\n",
            state.lane,s.indirect_draw ? "indirect-unchecked":"direct",(unsigned long long)hit.binding.address,
            (unsigned long long)hit.binding.size,(unsigned long long)hit.result.vertex,hit.binding.stride,
            hit.binding.location,hit.binding.offset,int32_t(hit.binding.base_vertex),count,int(indexed),(unsigned long long)(state.bound ? state.index:0),
            state.bound ? state.index_offset : 0,int(gate),indices.values[0],indices.values[1],indices.values[2],touched,
            (unsigned long long)vs,(unsigned long long)ps,(unsigned long long)args,stride,max_count,(unsigned long long)count_address);
    }
    s.hit_count=0;
}
} // namespace BbMeshSearch
