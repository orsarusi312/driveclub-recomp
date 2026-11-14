#include "../gpu/shim/bbport_mesh_probe.h"
#include <cassert>
#include <string>
#include <thread>
#include <filesystem>
static std::array<unsigned char,1024> memory{};
static unsigned reads;
extern "C" int runtime_memory_read_backing(uintptr_t address, void* out, uint64_t size) {
    ++reads;
    if(address<0x1000 || address-0x1000>memory.size() || size>memory.size()-(address-0x1000)) return 0;
    std::memcpy(out,memory.data()+address-0x1000,size); return 1;
}
static std::string Hex(const unsigned char* data,size_t size) {
    std::string out; const char* digits="0123456789abcdef";
    for(size_t i=0;i<size;++i) { out+=digits[data[i]>>4]; out+=digits[data[i]&15]; }
    return out;
}
int main(int argc,char** argv) {
    using namespace BbMeshProbe;
    const bool control_only=argc==2 && !std::strcmp(argv[1],"--control-only");
    if(argc==2 && !control_only) {
        FILE* f=std::fopen(argv[1],"rb"); assert(f);
        char line[512]; assert(std::fgets(line,sizeof(line),f)); std::fclose(f);
        line[std::strcspn(line,"\r\n")]=0;
        Spec parsed; assert(Parse(line,parsed)); return 0;
    }
    Spec spec;
    for(unsigned i=0;i<36;++i) spec.positions[i]=static_cast<unsigned char>(i+1);
    spec.indices[2]=1; spec.indices[4]=2;
    std::string text="v1:16:12:3:3:0:"+Hex(spec.positions.data(),36)+":"+Hex(spec.indices.data(),6);
    assert(Parse(text,spec));
    for(const auto& bad:{"", "v2:16:12:3:3:", "v1:42949672960:12:3:3:", "v1:1:12:3:3:"})
        assert(!Parse(bad,spec));
    assert(!Parse(text+"z",spec)); assert(!Parse(text.substr(0,text.size()-1),spec));
    for(unsigned i=0;i<3;++i) std::memcpy(memory.data()+64+i*16,spec.positions.data()+i*12,12);
    std::memcpy(memory.data(),spec.indices.data(),6);
    const auto original=memory;
    Indices indices; int64_t delta=999;
    assert(ReadIndices(0x1000,2,3,indices,runtime_memory_read_backing)==Gate::Match);
    assert(IndexDelta(spec,indices,delta) && delta==0);
    indices.values[0]+=10; indices.values[1]+=10; indices.values[2]+=10;
    assert(IndexDelta(spec,indices,delta) && delta==10);
    indices.values[1]++; assert(!IndexDelta(spec,indices,delta));
    memory[132]=1; memory[136]=2;
    assert(ReadIndices(0x1080,4,3,indices,runtime_memory_read_backing)==Gate::Match);
    assert(IndexDelta(spec,indices,delta) && delta==0);
    assert(ReadIndices(0x1080,1,3,indices,runtime_memory_read_backing)==Gate::Width);
    assert(ReadIndices(0x8000,2,3,indices,runtime_memory_read_backing)==Gate::Read);
    assert(ReadIndices(UINTPTR_MAX-2,2,3,indices,runtime_memory_read_backing)==Gate::Range);
    Binding b{0x1040,48,16,0,12,106,0,0,true};
    assert(Positions(spec,b,0,runtime_memory_read_backing)==Gate::Match);
    Binding variant=b; variant.stride=12;
    assert(Positions(spec,variant,0,runtime_memory_read_backing)==Gate::Stride);
    variant=b; variant.width=6;
    assert(Positions(spec,variant,0,runtime_memory_read_backing)==Gate::Format);
    variant=b; variant.base_vertex=UINT32_MAX;
    assert(Positions(spec,variant,0,runtime_memory_read_backing)==Gate::Base);
    spec.first=1;
    assert(Positions(spec,variant,0,runtime_memory_read_backing)==Gate::Match);
    assert(Positions(spec,b,-1,runtime_memory_read_backing)==Gate::Match);
    spec.first=0;
    variant=b; variant.size=11;
    assert(Positions(spec,variant,0,runtime_memory_read_backing)==Gate::Range);
    variant=b; variant.address=0x8000;
    assert(Positions(spec,variant,0,runtime_memory_read_backing)==Gate::Read);
    variant=b; variant.address=UINTPTR_MAX-10;
    unsigned before=reads;
    assert(Positions(spec,variant,0,runtime_memory_read_backing)==Gate::Range && reads==before);
    variant=b; variant.offset=4;
    assert(Positions(spec,variant,0,runtime_memory_read_backing)==Gate::Prefix);
    variant=b; variant.address-=4; variant.size+=4; variant.offset=4;
    assert(Positions(spec,variant,0,runtime_memory_read_backing)==Gate::Match);
    unsigned char half[6]={0,0x3c,0,0,0,0};
    assert(FiniteXYZ(half,6)); half[1]=0x7c; assert(!FiniteXYZ(half,6));
    assert(!FiniteXYZ(half,0));
    memory=original;
    ReadIndices(0x1000,2,3,indices,runtime_memory_read_backing);
    Control control;
    assert(ControlSample(control,true,1,123,456,3,indices,b,runtime_memory_read_backing));
    assert(!ControlSample(control,false,1,123,456,3,indices,b,runtime_memory_read_backing));
    assert(!ControlSample(control,false,2,124,456,3,indices,b,runtime_memory_read_backing));
    assert(ControlSample(control,false,2,123,456,3,indices,b,runtime_memory_read_backing));
    memory[64]^=1;
    assert(!ControlSample(control,false,3,123,456,3,indices,b,runtime_memory_read_backing));
    memory=original;
    // Replaying from a different address validates bytes, not an address cache.
    std::memcpy(memory.data()+256,memory.data()+64,48);
    variant=b; variant.address=0x1100;
    assert(ControlSample(control,false,4,123,456,3,indices,variant,runtime_memory_read_backing));
    memory=original;
    const auto root=std::filesystem::temp_directory_path()/(
        "dc-mesh-probe-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(root));
    const auto trigger=(root/"arm").string();
#ifdef _WIN32
    _putenv_s("BB_MESH_PROBE",control_only ? "" : text.c_str());
    _putenv_s("BB_MESH_PROBE_CONTROL",trigger.c_str());
#else
    if(control_only) unsetenv("BB_MESH_PROBE"); else setenv("BB_MESH_PROBE",text.c_str(),1);
    setenv("BB_MESH_PROBE_CONTROL",trigger.c_str(),1);
#endif
    if(control_only) {
        for(unsigned i=0;i<12;++i) memory[2*i]=static_cast<unsigned char>(i%3);
        FILE* flag=std::fopen(trigger.c_str(),"wb"); assert(flag); std::fclose(flag);
        before=reads;
        for(unsigned i=0;i<2;++i) {
            Begin(true,12); Observe(b); IndexBound(0x1000,2,0x1000,0); Recorded(1,2,12,1);
            if constexpr (BB_DIAGNOSTICS) assert(state.control.captured && state.control.validated==bool(i));
        }
        assert(!Configuration().offline);
        if constexpr (!BB_DIAGNOSTICS) assert(!state.control.captured && reads==before);
        std::filesystem::remove_all(root);
        std::puts("mesh probe control-only build gate and replay passed");
        return 0;
    }
    if constexpr (BB_DIAGNOSTICS) {
        auto draw=[&](uint32_t count,uintptr_t index,const Binding& binding) {
            const auto before_draw=memory;
            Begin(true,count); Observe(binding); // Vertex binding precedes index binding in the real path.
            IndexBound(index,2,index,0); Recorded(1,2,count,1);
            assert(memory==before_draw);
        };
        draw(3,0x1000,b); assert(state.exact_recorded==1 && !state.control.armed);
        Recorded(1,2,3,1); assert(state.exact_recorded==1);
        draw(6,0x1000,b); assert(state.count_mismatch==1);
        draw(3,0x8000,b); assert(state.index_read==1 && state.position_unpaired==1);
        memory[2]=2;
        draw(3,0x1000,b); assert(state.index_prefix==1 && state.position_unpaired==2);
        memory=original;
        variant=b; variant.base_vertex=1;
        draw(3,0x1000,variant); assert(state.base_zero_match==1 && state.exact_recorded==1);
        for(unsigned i=0;i<3;++i) memory[2*i]=static_cast<unsigned char>(i+1);
        variant=b; variant.base_vertex=UINT32_MAX;
        draw(3,0x1000,variant); assert(state.rebased_recorded==1);
        memory=original;
        Begin(true,3); Observe(b); Recorded(1,2,3,1); assert(state.missing_index==1);
        Begin(true,3); IndexBound(0x1000,2,0x1000,0); Recorded(1,2,3,1); assert(state.no_bindings==1);
        Begin(false,0,true); assert(state.indirect==1 && !state.bound && !state.binding_count);
        // Only a later recorded callback can validate the captured control.
        for(unsigned i=0;i<12;++i) memory[2*i]=static_cast<unsigned char>(i%3);
        FILE* flag=std::fopen(trigger.c_str(),"wb"); assert(flag); std::fclose(flag);
        state.poll-=std::chrono::seconds(2);
        draw(12,0x1000,b); assert(state.control.captured && !state.control.validated);
        draw(12,0x1000,b); assert(state.control.validated);
        auto attempts=state.control.attempts;
        draw(12,0x1000,b); assert(state.control.attempts==attempts);
        state.control={}; state.control.armed=true; state.control.attempts=4096;
        before=reads; draw(12,0x1000,b); assert(reads==before);
        // All snapshots and matching are read-only.
        memory=original;
        const auto main_lane=state.lane;
        const auto main_serial=state.serial;
        unsigned worker_lane=main_lane;
        std::thread worker([&] {
            assert(state.serial==0 && !state.control.captured);
            worker_lane=state.lane;
            draw(3,0x1000,b);
            assert(state.exact_recorded==1);
        });
        worker.join();
        assert(worker_lane!=main_lane && state.serial==main_serial);
        assert(std::filesystem::exists(trigger) && std::filesystem::file_size(trigger)==0);
        state.last-=std::chrono::seconds(6); Progress();
    } else {
        before=reads;
        Begin(true,3); Observe(b); IndexBound(0x1000,2,0x1000,0); Recorded(1,2,3,1);
        assert(!Configuration().enabled && reads==before);
    }
    assert(memory==original);
    std::filesystem::remove_all(root);
    std::puts("mesh probe passed: progressive gates, rebasing, bounded reads, capture/later replay, budgets, build gate");
}
