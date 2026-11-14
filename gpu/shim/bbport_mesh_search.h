// Bounded donor-byte search in actual vertex bindings, independent of draw counts.
#pragma once
#include "bbport_mesh_probe.h"
#include <charconv>
#include "../../src/runtime_rpk_copy.h"
namespace BbMeshSearch {
using namespace BbMeshProbe;
constexpr uint64_t ByteBudget=64ull*1024*1024, BindingLimit=2ull*1024*1024;
constexpr uint64_t AnchorBudget=2ull*1024*1024;
struct Target { uint64_t resource=0; uint32_t native_vertices=0, slot_vertices=0; bool requested=false, valid=false; };
inline bool ParseTarget(std::string_view text,Target& out) {
    if(text.size()>100 || text.substr(0,3)!="t1:") return false;
    text.remove_prefix(3); Target t;
    auto number=[&](auto& value,int base,bool last) {
        const size_t end=last ? text.size():text.find(':');
        if(end==std::string_view::npos || !end) return false;
        const auto r=std::from_chars(text.data(),text.data()+end,value,base);
        if(r.ec!=std::errc{} || r.ptr!=text.data()+end) return false;
        text.remove_prefix(end+(last ? 0:1)); return true;
    };
    if(!number(t.resource,16,false) || !number(t.native_vertices,10,false) || !number(t.slot_vertices,10,true) ||
       t.resource>>48!=3 || !t.slot_vertices || t.slot_vertices>t.native_vertices || t.native_vertices>10000000) return false;
    t.requested=t.valid=true; out=t; return true;
}
inline const Target& Targeting() {
    static const Target t=[] { Target value; if(const char* p=BB_DIAGNOSTIC_ENV("BB_MESH_SEARCH_TARGET")) {
        value.requested=true;
        if(!ParseTarget(p,value)) std::fputs("MeshSearch: invalid target; search disabled (no broad fallback)\n",stderr);
    } return value; }(); return t;
}
inline uint64_t Vertices(const Binding& b) {
    if(!b.stride || b.offset>b.size || b.size-b.offset<b.width) return 0;
    return (b.size-b.offset-b.width)/b.stride+1;
}
inline bool Shape(const Target& t,const Binding& b) {
    const uint64_t n=Vertices(b);
    return n==t.native_vertices || n==t.slot_vertices;
}
struct Result { bool found=false, truncated=false; uint64_t vertex=0, bytes=0, failures=0;
    unsigned anchor_attempts=0, anchor_short=0, anchor_mismatch=0, anchor_range=0; };
// Cheap independent pool: native offset and isolated/repacked start, never numeric tolerance.
inline Result Anchors(const Spec& spec,const Binding& b,uint64_t budget,Read read) {
    Result r; std::array<unsigned char,12> row{};
    if(!b.per_vertex || b.width!=spec.width || b.stride<b.width || b.stride>4096) return r;
    for(unsigned which=0;which<2;++which) {
        if(which && !spec.first) break;
        const uint32_t first=which ? 0:spec.first;
        if(Vertices(b)<uint64_t(first)+spec.samples) { ++r.anchor_short; continue; }
        ++r.anchor_attempts;
        bool match=true;
        for(uint32_t i=0;i<spec.samples;++i) {
            if(spec.width>budget-r.bytes) { r.truncated=true; return r; }
            r.bytes+=spec.width;
            Gate gate=ReadVertex(b,int64_t(first)+i,row.data(),read);
            if(gate!=Gate::Match) { if(gate==Gate::Read) ++r.failures; else ++r.anchor_range; match=false; break; }
            if(std::memcmp(row.data(),spec.positions.data()+i*spec.width,spec.width)) { ++r.anchor_mismatch; match=false; break; }
        }
        if(match) { r.found=true; r.vertex=first; return r; }
    }
    return r;
}
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
        if(cap-result.bytes<overlap) { result.truncated=true; break; }
        const uint64_t n=std::min({batch,candidates-first,(cap-result.bytes-overlap)/b.stride+1});
        const uint64_t bytes=(n-1)*b.stride+overlap;
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
struct Entry { Binding binding; Result result; bool reported=false; };
enum class Rejection { Instance, Format, Width, Stride, Bounds, Shape, Eligible };
inline Rejection Classify(const Spec& spec,const Target& target,const Binding& b) {
    if(!b.per_vertex) return Rejection::Instance;
    if(!b.width) return Rejection::Format;
    if(b.width!=spec.width) return Rejection::Width;
    if(b.stride<b.width || b.stride>4096) return Rejection::Stride;
    if(b.offset>b.size || b.size-b.offset<b.width || b.address>UINTPTR_MAX-b.size) return Rejection::Bounds;
    if(target.valid && !Shape(target,b)) return Rejection::Shape;
    return Rejection::Eligible;
}
struct Layouts {
    struct Row { Binding b; Rejection reason{}; bool indirect=false; uint64_t count=0; };
    std::array<Row,1024> rows{};
    uint64_t observations=0, overflow=0; unsigned reports=0;
    void Add(const Binding& b,Rejection reason,bool indirect) {
        ++observations;
        uint64_t h=b.size^(uint64_t(b.stride)<<32)^(uint64_t(b.format)<<12)^b.offset^
            (uint64_t(b.width)<<48)^(uint64_t(reason)<<56)^uint64_t(indirect);
        h^=h>>30; h*=UINT64_C(0xbf58476d1ce4e5b9); h^=h>>27;
        for(unsigned n=0;n<8;++n) {
            auto& row=rows[(h+n)%rows.size()];
            if(!row.count || (row.b.size==b.size && row.b.stride==b.stride && row.b.format==b.format &&
                row.b.offset==b.offset && row.b.width==b.width && row.reason==reason && row.indirect==indirect)) {
                if(!row.count) row={b,reason,indirect,0};
                ++row.count; return;
            }
        }
        ++overflow; // Bounded lookup; tracked counts are exact, omitted layouts explicitly counted.
    }
    void Print(unsigned lane) {
        if(reports++>=12) return;
        std::array<unsigned,1024> order{}; unsigned used=0;
        for(unsigned i=0;i<rows.size();++i) if(rows[i].count) order[used++]=i;
        const unsigned top=std::min(used,8u);
        std::partial_sort(order.begin(),order.begin()+top,order.begin()+used,
            [&](unsigned a,unsigned b){return rows[a].count>rows[b].count;});
        const char* reasons[]={"instance-rate","unsupported-format","xyz-width","stride","bounds","record-count","eligible"};
        std::fprintf(stderr,"MeshLayouts: lane=%u observations=%llu tracked=%u overflow=%llu report=%u; top tracked tuples, counts are observations not unique buffers\n",
            lane,(unsigned long long)observations,used,(unsigned long long)overflow,reports);
        for(unsigned i=0;i<top;++i) {
            const auto& r=rows[order[i]];
            std::fprintf(stderr,"MeshLayouts: lane=%u count=%llu path=%s bytes=%llu records=%llu stride=%u format=%u xyz-width=%u attr-offset=%u reason=%s example-vb=0x%llx\n",
                lane,(unsigned long long)r.count,r.indirect ? "indirect":"direct",(unsigned long long)r.b.size,
                (unsigned long long)(r.b.stride ? r.b.size/r.b.stride:0),r.b.stride,r.b.format,r.b.width,r.b.offset,
                reasons[unsigned(r.reason)],(unsigned long long)r.b.address);
        }
    }
};
inline bool LayoutsOn() { static bool on=[] { const char* p=BB_DIAGNOSTIC_ENV("BB_MESH_LAYOUTS"); return p && !std::strcmp(p,"1"); }(); return on; }
struct SearchState {
    Layouts layouts;
    std::array<Entry,4096> cache{};
    std::array<uint16_t,8192> lookup{}; // index+1, bounded open addressing, at most half full
    std::array<Entry,32> hits{};
    std::array<Entry*,8> candidates{};
    unsigned used=0, hit_count=0, candidate_count=0, candidate_logs=0, supported=0, logs=0, checks=0;
    uint64_t bytes=0, failures=0, truncated=0, unsupported=0, skips=0, full=0,
             direct=0, indirect=0, found=0, referenced=0, stale=0, no_supported_bindings=0,
             anchor_bytes=0, anchor_hits=0, shape_candidates=0, shape_skipped=0, scan_complete=0, budget_skipped=0,
             anchor_attempts=0, anchor_short=0, anchor_mismatch=0, anchor_range=0, copy_candidates=0, copy_hits=0;
    bool armed=false, indirect_draw=false, budget_reported=false;
    std::chrono::steady_clock::time_point poll{}, last=std::chrono::steady_clock::now();
};
inline thread_local SearchState search;
inline const char* Trigger() {
    static const char* value=[] {
        const char* p=BB_DIAGNOSTIC_ENV("BB_MESH_SEARCH");
        if(!p || !*p) return static_cast<const char*>(nullptr);
        if(Targeting().requested && !Targeting().valid) return static_cast<const char*>(nullptr);
        if(!Configuration().offline) { std::fputs("MeshSearch: requires valid BB_MESH_PROBE signature; disabled\n",stderr); return static_cast<const char*>(nullptr); }
        std::fprintf(stderr,"MeshSearch: waiting for trigger file; mode=%s 64 MiB scan/lane, 2 MiB/binding, %u bindings, 4096 hit checks; target=%016llx native=%u slot=%u; targeted anchors have separate 2 MiB budget\n",
            Targeting().valid ? "targeted":"broad",Targeting().valid ? 4096u:1024u,
            (unsigned long long)Targeting().resource,Targeting().native_vertices,Targeting().slot_vertices);
        return p;
    }();
    return value;
}
inline void Begin(bool indirect) {
    if(!Trigger()) return;
    auto& s=search; s.hit_count=0; s.candidate_count=0; s.supported=0; s.indirect_draw=indirect;
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
        if(Targeting().valid) std::fprintf(stderr,"MeshSearchTarget: lane=%u anchor-bytes=%llu anchor-hits=%llu shape-candidates=%llu shape-skipped=%llu scan-complete=%llu budget-skipped=%llu anchor-attempts=%llu anchor-too-short=%llu anchor-mismatch=%llu anchor-range=%llu; shape is prioritization, not identity\n",
            state.lane,(unsigned long long)s.anchor_bytes,(unsigned long long)s.anchor_hits,
            (unsigned long long)s.shape_candidates,(unsigned long long)s.shape_skipped,
            (unsigned long long)s.scan_complete,(unsigned long long)s.budget_skipped,
            (unsigned long long)s.anchor_attempts,(unsigned long long)s.anchor_short,
            (unsigned long long)s.anchor_mismatch,(unsigned long long)s.anchor_range);
        if(LayoutsOn()) s.layouts.Print(state.lane);
        if(Targeting().valid) std::fprintf(stderr,"MeshCopyBinding: lane=%u candidates=%llu fresh-matches=%llu; historical address candidates require fresh bytes\n",
            state.lane,(unsigned long long)s.copy_candidates,(unsigned long long)s.copy_hits);
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
    if(LayoutsOn()) s.layouts.Add(b,Classify(spec,Targeting(),b),s.indirect_draw);
    if(!b.per_vertex || b.width!=spec.width || b.stride<b.width || b.stride>4096) { ++s.unsupported; return; }
    ++s.supported;
    Entry* entry=nullptr;
    size_t slot=Key(b)%s.lookup.size();
    while(s.lookup[slot]) {
        auto& cached=s.cache[s.lookup[slot]-1];
        if(Same(cached.binding,b)) { entry=&cached; ++s.skips; break; }
        slot=(slot+1)%s.lookup.size();
    }
    if(!entry) {
        if(s.used==(Targeting().valid ? 4096u:1024u)) { ++s.full; return; }
        Result result;
        if(Targeting().valid) {
            uintptr_t at=0; uint64_t id=0;
            if(b.offset<=b.size && b.address<=UINTPTR_MAX-b.offset &&
               runtime_rpk_copy_find(b.address+b.offset,b.size-b.offset,&at,&id) && id==Targeting().resource &&
               at>=b.address+b.offset && (at-b.address-b.offset)%b.stride==0) {
                ++s.copy_candidates;
                const uint64_t cost=uint64_t(spec.samples)*spec.width;
                if(cost<=AnchorBudget-s.anchor_bytes) {
                    s.anchor_bytes+=cost;
                    const uint64_t vertex=(at-b.address-b.offset)/b.stride;
                    if(Fresh(spec,b,vertex,runtime_memory_read_backing)) {
                        result.found=true; result.vertex=vertex; ++s.copy_hits;
                    }
                }
            }
            const bool copied=result.found;
            if(!copied) result=Anchors(spec,b,AnchorBudget-s.anchor_bytes,runtime_memory_read_backing);
            s.anchor_bytes+=result.bytes; s.failures+=result.failures;
            s.anchor_attempts+=result.anchor_attempts; s.anchor_short+=result.anchor_short;
            s.anchor_mismatch+=result.anchor_mismatch; s.anchor_range+=result.anchor_range;
            if(result.found && !copied) ++s.anchor_hits;
            if(result.truncated) { ++s.budget_skipped; return; }
        }
        const bool eligible=!Targeting().valid || Shape(Targeting(),b);
        if(!result.found && eligible) {
            const uint64_t window=uint64_t(spec.samples-1)*b.stride+b.width;
            if(ByteBudget-s.bytes<window) { ++s.budget_skipped; return; }
            ++s.shape_candidates;
            result=Find(spec,b,ByteBudget-s.bytes,runtime_memory_read_backing);
            s.bytes+=result.bytes; s.failures+=result.failures; s.truncated+=result.truncated;
            if(!result.truncated && !result.failures) ++s.scan_complete;
        } else if(!eligible) ++s.shape_skipped;
        // A cached truncated result remains incomplete, never a completed negative scan.
        auto& added=s.cache[s.used++]; added.binding=b; added.result=result;
        s.lookup[slot]=uint16_t(s.used);
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
    if(Targeting().valid && Shape(Targeting(),b) && !entry->reported && s.candidate_logs<12 && s.candidate_count<s.candidates.size()) {
        entry->binding=b; s.candidates[s.candidate_count++]=entry;
    }
}
// Invoked only after the draw command is handed to the scheduler, on its owning lane.
inline void Recorded(uint64_t vs,uint64_t ps,uint32_t count,uintptr_t args=0,uint32_t stride=0,
                     uint32_t max_count=0,uintptr_t count_address=0,bool indexed=true) {
    if(!Trigger() || !search.armed) return;
    auto& s=search; const auto& spec=Configuration().spec;
    if(!s.supported) ++s.no_supported_bindings;
    for(unsigned i=0;i<s.candidate_count && s.candidate_logs<12;++i) {
        auto& entry=*s.candidates[i]; if(entry.reported) continue;
        entry.reported=true; ++s.candidate_logs;
        const auto& b=entry.binding;
        std::fprintf(stderr,"MeshSearchTarget: lane=%u shape-candidate path=%s vb=0x%llx bytes=%llu vertices=%llu stride=%u base-vertex=%d count=%u ib=0x%llx vs=%llx ps=%llx cached-prefix=%d scan-truncated=%d; shape only, not resource identity\n",
            state.lane,s.indirect_draw ? "indirect-unchecked":"direct",(unsigned long long)b.address,
            (unsigned long long)b.size,(unsigned long long)Vertices(b),b.stride,int32_t(b.base_vertex),count,
            (unsigned long long)(state.bound ? state.index:0),(unsigned long long)vs,(unsigned long long)ps,
            int(entry.result.found),int(entry.result.truncated));
    }
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
    s.candidate_count=0;
}
} // namespace BbMeshSearch
