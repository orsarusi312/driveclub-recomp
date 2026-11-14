#include "../gpu/shim/bbport_mesh_search.h"
#include <cassert>
#include <vector>
#include <thread>
#include <filesystem>
using namespace BbMeshProbe;
static std::vector<unsigned char> memory(3*1024*1024);
static uint64_t reads=0;
extern "C" int runtime_memory_read_backing(uintptr_t address,void* out,uint64_t size) {
    ++reads;
    if(address<0x1000 || address-0x1000>memory.size() || size>memory.size()-(address-0x1000)) return 0;
    std::memcpy(out,memory.data()+address-0x1000,size); return 1;
}
static std::string hex(const unsigned char* p,size_t n) {
    std::string s; for(size_t i=0;i<n;++i) { s+="0123456789abcdef"[p[i]>>4]; s+="0123456789abcdef"[p[i]&15]; } return s;
}
int main() {
    Spec spec; spec.width=12; spec.stride=16; spec.samples=8; spec.count=195; spec.first=1456;
    for(unsigned i=0;i<96;++i) spec.positions[i]=static_cast<unsigned char>(i+1);
    Binding b{0x1000,memory.size(),16,0,12,106,0,0,true};
    auto place=[&](uint64_t vertex) { for(unsigned i=0;i<spec.samples;++i) std::memcpy(memory.data()+(vertex+i)*b.stride+b.offset,spec.positions.data()+i*12,12); };
    // Crosses a chunk boundary and is not at the offline vertex offset.
    place(4093);
    auto result=BbMeshSearch::Find(spec,b,BbMeshSearch::ByteBudget,runtime_memory_read_backing);
    assert(result.found && result.vertex==4093 && !result.failures);
    assert(BbMeshSearch::Fresh(spec,b,4093,runtime_memory_read_backing));
    memory[4093*16]^=1;
    assert(!BbMeshSearch::Fresh(spec,b,4093,runtime_memory_read_backing));
    result=BbMeshSearch::Find(spec,b,BbMeshSearch::ByteBudget,runtime_memory_read_backing);
    assert(!result.found && result.truncated && result.bytes<=BbMeshSearch::BindingLimit);
    uint64_t before=reads;
    result=BbMeshSearch::Find(spec,b,12,runtime_memory_read_backing);
    assert(!result.found && result.truncated && reads==before);
    Binding bad=b; bad.address=UINTPTR_MAX-8;
    assert(!BbMeshSearch::Find(spec,bad,100000,runtime_memory_read_backing).found && reads==before);
    bad=b; bad.size=10;
    assert(!BbMeshSearch::Find(spec,bad,100000,runtime_memory_read_backing).found && reads==before);
    bad=b; bad.address=0x800000;
    result=BbMeshSearch::Find(spec,bad,65536,runtime_memory_read_backing);
    assert(result.failures && !result.found);
    // Repacked stride and an attribute offset remain byte-exact, no type conversion.
    std::fill(memory.begin(),memory.end(),0); b.stride=12; b.offset=4; b.size=1024; place(10);
    result=BbMeshSearch::Find(spec,b,65536,runtime_memory_read_backing);
    assert(result.found && result.vertex==10);
    Indices indices; indices.size=4; indices.values={0,10,11,100};
    assert(BbMeshSearch::Referenced(spec,b,10,indices)==2);
    b.base_vertex=10; assert(BbMeshSearch::Referenced(spec,b,10,indices)==1); b.base_vertex=0;
    std::string signature="v1:16:12:8:195:1456:"+hex(spec.positions.data(),96)+":"+hex(spec.indices.data(),48);
    auto root=std::filesystem::temp_directory_path()/("mesh-search-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(root)); auto trigger=(root/"arm").string();
#ifdef _WIN32
    _putenv_s("BB_MESH_PROBE",signature.c_str()); _putenv_s("BB_MESH_SEARCH",trigger.c_str());
#else
    setenv("BB_MESH_PROBE",signature.c_str(),1); setenv("BB_MESH_SEARCH",trigger.c_str(),1);
#endif
    before=reads; BbMeshSearch::Begin(false); BbMeshSearch::Observe(b); assert(reads==before);
    FILE* flag=std::fopen(trigger.c_str(),"wb"); assert(flag); std::fclose(flag);
    BbMeshSearch::search.poll-=std::chrono::seconds(2);
    // Search is independent of donor index count, and works for indirect bindings too.
    BbMeshProbe::Begin(true,3); BbMeshSearch::Begin(false); BbMeshSearch::Observe(b);
    BbMeshSearch::Recorded(1,2,3);
    if constexpr(BB_DIAGNOSTICS) {
        assert(BbMeshSearch::search.used==1 && BbMeshSearch::search.found==1 && BbMeshSearch::search.checks==1);
        // A shared buffer hit alone is not selection; a sampled index must reference it.
        memory[1024]=10; memory[1026]=11; memory[1028]=12;
        const auto unchanged=memory;
        BbMeshProbe::Begin(true,3); BbMeshSearch::Begin(false); BbMeshSearch::Observe(b);
        BbMeshProbe::IndexBound(0x1400,2,0x13f0,8); BbMeshSearch::Recorded(1,2,3);
        assert(BbMeshSearch::search.referenced==1 && memory==unchanged);
        before=reads;
        BbMeshProbe::Begin(false,0,true); BbMeshSearch::Begin(true); BbMeshSearch::Observe(b); assert(reads==before);
        BbMeshSearch::Recorded(1,2,0,0x9000,20,8,0xA000);
        assert(BbMeshSearch::search.checks==3 && reads==before+8); // no indirect command reads
        memory[124]^=1;
        BbMeshSearch::Begin(false); BbMeshSearch::Observe(b); BbMeshSearch::Recorded(1,2,3);
        assert(BbMeshSearch::search.stale==1);
        BbMeshSearch::search.checks=4096; before=reads;
        BbMeshSearch::Begin(false); BbMeshSearch::Observe(b); BbMeshSearch::Recorded(1,2,3); assert(reads==before);
        std::thread worker([] { assert(BbMeshSearch::search.used==0 && !BbMeshSearch::search.armed); }); worker.join();
    } else assert(reads==before);
    std::filesystem::remove_all(root);
    // Half-width byte signatures remain exact; an instance-rate descriptor is excluded.
    Spec half=spec; half.width=6; half.stride=8;
    Binding hb{0x1000,256,8,0,6,97,0,0,true};
    std::fill(memory.begin(),memory.end(),0);
    for(unsigned i=0;i<8;++i) std::memcpy(memory.data()+(5+i)*8,half.positions.data()+i*6,6);
    result=BbMeshSearch::Find(half,hb,1024,runtime_memory_read_backing);
    assert(result.found && result.vertex==5);
    hb.per_vertex=false; before=reads;
    assert(!BbMeshSearch::Find(half,hb,1024,runtime_memory_read_backing).found && reads==before);
    puts("Mesh search: chunk boundaries, relocation/repacking, budgets, stale bytes, direct/indirect paths and TLS passed");
}
