// Opt-in metadata only: selected recorded draw -> descriptors -> buffer-cache operations.
#pragma once
#include "bbport_diagnostics.h"
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <string_view>
namespace BbBufferPath {
struct Pair { uint64_t vs=0,ps=0; bool valid=false; };
inline bool Parse(std::string_view text,Pair& out) {
    if(text.size()>37 || text.substr(0,3)!="p1:") return false;
    text.remove_prefix(3); const auto colon=text.find(':');
    if(colon==std::string_view::npos || !colon || colon+1==text.size()) return false;
    Pair p;
    auto a=std::from_chars(text.data(),text.data()+colon,p.vs,16);
    auto b=std::from_chars(text.data()+colon+1,text.data()+text.size(),p.ps,16);
    if(a.ec!=std::errc{} || b.ec!=std::errc{} || a.ptr!=text.data()+colon || b.ptr!=text.data()+text.size() || !p.vs || !p.ps) return false;
    p.valid=true; out=p; return true;
}
struct Config { Pair pair; const char* trigger=nullptr; bool early=false; };
inline const Config& Configuration() {
    static const Config c=[] {
        Config c; const char* pair=BB_DIAGNOSTIC_ENV("BB_BUFFER_PATH");
        if(!pair) return c;
        c.trigger=BB_DIAGNOSTIC_ENV("BB_BUFFER_PATH_TRIGGER");
        if(const char* e=BB_DIAGNOSTIC_ENV("BB_BUFFER_PATH_EARLY")) c.early=std::string_view(e)=="1";
        if(!Parse(pair,c.pair) || !c.trigger || !*c.trigger) {
            c.pair.valid=false;
            std::fputs("BufferPath: invalid pair/trigger; disabled\n",stderr);
        } else std::fprintf(stderr,"BufferPath: waiting for trigger; vs=%llx ps=%llx; early=%d; metadata only, 16 unique binding sets and 256 attempts/lane/phase, 64 events/draw; no RPK identity claim\n",
            (unsigned long long)c.pair.vs,(unsigned long long)c.pair.ps,int(c.early));
        return c;
    }(); return c;
}
enum class Kind { Vertex,Index,Resource,Ring,Special,Obtain,StreamQueued,StreamNow,Arena,UploadQueued,UploadNow,IndirectArgs,IndirectCount,Shader };
struct Event {
    Kind kind{}; uint64_t address=0,size=0,token=0,offset=0,stage=0;
    uint32_t slot=0,stride=0,format=0,flags=0;
};
struct State {
    std::array<Event,64> events{};
    std::array<uint64_t,16> keys{};
    unsigned count=0,overflow=0,emitted=0,attempts=0;
    bool active=false,armed=false,indirect=false;
    std::chrono::steady_clock::time_point poll{};
};
inline thread_local State state;
inline std::atomic<uint64_t> serial{0};
inline bool Active() { if constexpr(!BB_DIAGNOSTICS) return false; return state.active; }
inline void Add(Event event) {
    if(!Active()) return;
    if(state.count==state.events.size()) { ++state.overflow; return; }
    state.events[state.count++]=event;
}
// Key only the vertex/index binding set, not transient constant-ring offsets.
inline uint64_t Key(const State& s) {
    uint64_t h=s.indirect ? 17:19;
    auto mix=[&](uint64_t x) { h^=x; h*=UINT64_C(1099511628211); };
    for(unsigned i=0;i<s.count;++i) {
        const auto& e=s.events[i];
        if(e.kind!=Kind::Vertex && e.kind!=Kind::Index) continue;
        mix(uint64_t(e.kind)); mix(e.address); mix(e.size); mix(e.stride); mix(e.format); mix(e.slot); mix(e.offset); mix(e.flags);
    }
    return h;
}
struct Scope {
    bool owner=false;
    Scope(uint64_t vs,uint64_t ps,bool indirect) {
        if constexpr(!BB_DIAGNOSTICS) return;
        auto& s=state; const auto& c=Configuration();
        if(s.active || !c.pair.valid || vs!=c.pair.vs || ps!=c.pair.ps) return;
        if(!s.armed) {
            const auto now=std::chrono::steady_clock::now();
            if(now-s.poll>=std::chrono::seconds(1)) {
                s.poll=now;
                if(FILE* f=std::fopen(c.trigger,"rb")) {
                    std::fclose(f); s.armed=true; s.emitted=s.attempts=0;
                    std::fputs("BufferPath: armed for selected shader pair; gameplay phase, fresh bounded budget\n",stderr);
                }
            }
            if(!s.armed && !c.early) return;
        }
        if(s.attempts==256 || s.emitted==s.keys.size()) return;
        if(++s.attempts==256) std::fputs("BufferPath: lane phase attempt limit reached (256); final attempt captured if unique\n",stderr);
        s.active=owner=true; s.indirect=indirect; s.count=s.overflow=0;
    }
    ~Scope() { if(owner) state.active=false; }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
    void Recorded(uint32_t count,uint32_t instances,int32_t base_vertex) {
        if(!owner || !state.active) return;
        auto& s=state; s.active=false;
        const uint64_t key=Key(s);
        for(unsigned i=0;i<s.emitted;++i) if(s.keys[i]==key) return;
        s.keys[s.emitted++]=key;
        const auto id=serial.fetch_add(1,std::memory_order_relaxed)+1;
        std::fprintf(stderr,"BufferPath: draw=%llu phase=%s path=%s count=%u instances=%u base-vertex=%d events=%u overflow=%u; recorded command, not GPU completion or resource identity\n",
            (unsigned long long)id,s.armed ? "gameplay":"startup",s.indirect ? "indirect-unchecked":"direct",count,instances,base_vertex,s.count,s.overflow);
        const char* names[]={"vertex","index","resource","constant-ring","special","obtain","stream-queued","stream-now","arena","upload-queued","upload-now","indirect-args","indirect-count","shader"};
        for(unsigned i=0;i<s.count;++i) {
            const auto& e=s.events[i];
            std::fprintf(stderr,"BufferPath: draw=%llu event=%u kind=%s guest=0x%llx bytes=%llu buffer-token=0x%llx offset=%llu stage=%llx slot=%u stride=%u format=%u flags=%u\n",
                (unsigned long long)id,i,names[unsigned(e.kind)],(unsigned long long)e.address,(unsigned long long)e.size,
                (unsigned long long)e.token,(unsigned long long)e.offset,(unsigned long long)e.stage,e.slot,e.stride,e.format,e.flags);
        }
        if(s.emitted==s.keys.size()) std::fputs("BufferPath: lane phase capture limit reached (16); further sets unobserved\n",stderr);
    }
};
} // namespace BbBufferPath
