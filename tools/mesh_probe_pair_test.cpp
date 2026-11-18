#include "../gpu/shim/bbport_mesh_probe.h"
#include <cassert>
#include <filesystem>
#include <string>
#include <thread>
static std::array<unsigned char,4096> bytes{};
static unsigned reads;
extern "C" int runtime_memory_read_backing(uintptr_t at,void* out,uint64_t n) {
    ++reads;
    if(at<0x1000 || at-0x1000>bytes.size() || n>bytes.size()-(at-0x1000)) return 0;
    std::memcpy(out,bytes.data()+at-0x1000,n); return 1;
}
static void env(const char* name,const std::string& value) {
#ifdef _WIN32
    _putenv_s(name,value.c_str());
#else
    if(value.empty()) unsetenv(name); else setenv(name,value.c_str(),1);
#endif
}
static std::string hex(const unsigned char* p,size_t n) {
    std::string s; const char* h="0123456789abcdef";
    for(size_t i=0;i<n;++i) { s+=h[p[i]>>4]; s+=h[p[i]&15]; } return s;
}
int main(int argc,char** argv) {
    using namespace BbMeshProbe;
    assert(argc==2); const std::string mode=argv[1];
    for(unsigned i=0;i<92;++i) for(unsigned axis=0;axis<3;++axis) {
        uint16_t h=uint16_t(0x3800+i+axis*128);
        std::memcpy(bytes.data()+512+i*12+axis*2,&h,2);
    }
    for(unsigned i=0;i<96;++i) bytes[2048+2*i]=uint8_t(i%28);
    for(unsigned i=0;i<186;++i) bytes[2*i]=uint8_t(28+i%64);
    auto signature=[&](unsigned count,unsigned first,unsigned index) {
        std::string s="v1:12:6:8:"+std::to_string(count)+":"+std::to_string(first)+":";
        for(unsigned i=0;i<8;++i) s+=hex(bytes.data()+512+(first+i)*12,6);
        return s+":"+hex(bytes.data()+index,48);
    };
    const auto root=std::filesystem::temp_directory_path()/("dc-pair-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(root)); const auto flag=(root/"arm").string();
    env("BB_MESH_PROBE",signature(96,0,2048));
    env("BB_MESH_PROBE_SECOND",mode=="invalid" ? "bad" : mode=="same-count" ? signature(96,28,0) : signature(186,28,0));
    env("BB_MESH_PROBE_TRIGGER",mode=="missing-trigger" ? "" : flag);
    env("BB_MESH_PROBE_CONTROL","");
    Binding binding{0x1200,92*12,12,0,6,97,0,0,true};
    auto draw=[&](unsigned count,bool bound=true,bool valid=true) {
        Begin(true,count); auto b=binding; if(!valid) b.width=0;
        Observe(b); if(bound) IndexBound(count==96 ? 0x1800 : 0x1000,2,0,0);
        Recorded(1,2,count,1);
    };
    const auto original=bytes;
    draw(96); assert(reads==0 && !state.offline_armed);
    FILE* f=std::fopen(flag.c_str(),"wb"); assert(f); std::fclose(f);
    state.poll-=std::chrono::seconds(2);
    draw(3); assert(reads==0);
    if constexpr(!BB_DIAGNOSTICS) { draw(96); assert(reads==0 && !Configuration().enabled); }
    else if(mode=="invalid" || mode=="same-count" || mode=="missing-trigger") {
        draw(96); assert(reads==0 && !Configuration().enabled);
    } else {
        assert(Configuration().paired && state.offline_armed);
        if(mode=="match96" || mode=="match186") {
            const unsigned selected=mode=="match96" ? 96 : 186;
            draw(selected==96 ? 186 : 96,true,false); // Index-only must not become a paired match.
            if(selected==186) { // Rebased indices and a matching base vertex.
                for(unsigned i=0;i<186;++i) bytes[2*i]-=28;
                binding.base_vertex=28;
            }
            draw(selected);
            assert(state.offline_finished && state.offline_ended && state.offline_attempts==2);
            assert(state.coverage[selected==96 ? 0 : 1].matches==1);
            assert(state.coverage[selected==96 ? 1 : 0].matches==0);
            bytes=original; binding.base_vertex=0;
        } else if(mode=="budget") {
            for(unsigned i=0;i<4096;++i) draw(i%2 ? 96 : 186,false);
            assert(state.offline_ended && !state.offline_finished && state.offline_attempts==4096);
            assert(state.coverage[0].attempts==2048 && state.coverage[1].attempts==2048 && reads==0);
        } else {
            assert(mode=="no-coverage" || mode=="partial");
            if(mode=="partial") draw(96,true,false);
            state.offline_started-=std::chrono::seconds(91);
            Begin(false,0,true); // Deadline closes even without any eligible direct draw.
            assert(state.offline_ended && !state.offline_finished);
            assert(state.coverage[0].attempts==(mode=="partial" ? 1u : 0u));
            assert(state.coverage[1].attempts==0);
        }
        const auto before=reads; draw(96); draw(186); FinishPair(); assert(reads==before);
        // A different recording lane must own independent counters and deadline.
        std::thread worker([&] { draw(96); assert(state.coverage[0].matches==1 && state.offline_ended); }); worker.join();
    }
    assert(bytes==original); std::filesystem::remove_all(root);
    std::puts("paired probe gates, coverage, terminal state and unchanged source passed");
}
