#include "../gpu/shim/bbport_mesh_probe.h"
#include <cassert>
#include <string>
static std::array<unsigned char,512> memory{};
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
    if(argc==2) {
        FILE* f=std::fopen(argv[1],"rb"); assert(f);
        char line[512]; assert(std::fgets(line,sizeof(line),f)); std::fclose(f);
        line[std::strcspn(line,"\r\n")]=0;
        BbMeshProbe::Spec parsed; assert(BbMeshProbe::Parse(line,parsed));
        return 0;
    }
    BbMeshProbe::Spec spec;
    for(unsigned i=0;i<36;++i) spec.positions[i]=static_cast<unsigned char>(i+1);
    spec.indices[2]=1; spec.indices[4]=2;
    std::string text="v1:16:12:3:3:0:"+Hex(spec.positions.data(),36)+":"+Hex(spec.indices.data(),6);
    assert(BbMeshProbe::Parse(text,spec));
    for(const auto& bad:{"", "v2:16:12:3:3:", "v1:42949672960:12:3:3:", "v1:1:12:3:3:"})
        assert(!BbMeshProbe::Parse(bad,spec));
    assert(!BbMeshProbe::Parse(text+"z",spec));
    assert(!BbMeshProbe::Parse(text.substr(0,text.size()-1),spec));
    for(unsigned i=0;i<3;++i) std::memcpy(memory.data()+64+i*16,spec.positions.data()+i*12,12);
    std::memcpy(memory.data(),spec.indices.data(),6);
    const auto original=memory;
    assert(BbMeshProbe::IndexMatch(spec,0x1000,2,runtime_memory_read_backing));
    memory[132]=1; memory[136]=2;
    assert(BbMeshProbe::IndexMatch(spec,0x1080,4,runtime_memory_read_backing));
    assert(BbMeshProbe::PositionMatch(spec,0x1040,48,16,0,runtime_memory_read_backing));
    assert(BbMeshProbe::PositionMatch(spec,0x1030,64,16,1,runtime_memory_read_backing));
    spec.first=1;
    assert(BbMeshProbe::PositionMatch(spec,0x1030,64,16,0,runtime_memory_read_backing));
    assert(BbMeshProbe::PositionMatch(spec,0x1040,48,16,UINT32_MAX,runtime_memory_read_backing));
    spec.first=0;
    unsigned before=reads;
    assert(!BbMeshProbe::PositionMatch(spec,UINTPTR_MAX-10,48,16,0,runtime_memory_read_backing));
    assert(!BbMeshProbe::PositionMatch(spec,0x1040,43,16,0,runtime_memory_read_backing));
    assert(!BbMeshProbe::PositionMatch(spec,0x1040,48,16,UINT32_MAX,runtime_memory_read_backing));
    assert(!BbMeshProbe::PositionMatch(spec,0x1040,48,12,0,runtime_memory_read_backing));
    assert(reads==before);
    assert(!BbMeshProbe::IndexMatch(spec,UINTPTR_MAX-2,2,runtime_memory_read_backing));
    assert(!BbMeshProbe::IndexMatch(spec,0x8000,2,runtime_memory_read_backing));
    memory=original;
#ifdef _WIN32
    _putenv_s("BB_MESH_PROBE",text.c_str());
#else
    setenv("BB_MESH_PROBE",text.c_str(),1);
#endif
    if constexpr (BB_DIAGNOSTICS) {
        BbMeshProbe::Begin(true,3,0x1000,2);
        BbMeshProbe::Observe(0x1040,48,16,0);
        BbMeshProbe::Recorded(1,2,3,1);
        assert(BbMeshProbe::state.recorded==1);
        BbMeshProbe::Recorded(1,2,3,1);
        assert(BbMeshProbe::state.recorded==1);
        BbMeshProbe::Begin(true,3,0x1000,2);
        BbMeshProbe::Recorded(1,2,3,1);
        assert(BbMeshProbe::state.recorded==1);
        BbMeshProbe::Begin(false,0,0,0,true);
        assert(BbMeshProbe::state.indirect==1 && !BbMeshProbe::state.eligible);
        BbMeshProbe::Begin(true,6,0x1000,2);
        assert(!BbMeshProbe::state.eligible);
    } else {
        before=reads;
        BbMeshProbe::Begin(true,3,0x1000,2);
        assert(!BbMeshProbe::Configuration().enabled && reads==before);
    }
    assert(memory==original);
    std::puts("mesh probe passed: parse, 16/32-bit indices, offsets, bounds, read-only, lifecycle, build gate");
}
