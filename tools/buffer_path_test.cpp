#include "../gpu/shim/bbport_buffer_path.h"
#include <cassert>
#include <filesystem>
#include <thread>
#include <string>
using namespace BbBufferPath;
bool buffer_path_peer();
int main(int argc,char** argv) {
    Pair p;
    assert(Parse("p1:2a937475:d1e35cbc",p));
    for(const auto* bad : {"", "p1:","p1:0:1","p1:1:0","p1:1:2:3","p1:-1:2","p1:xyz:1","p1:10000000000000000:1"}) assert(!Parse(bad,p));
    const bool invalid=argc>1 && std::string_view(argv[1])=="--invalid";
    const bool absent=argc>1 && std::string_view(argv[1])=="--absent";
    const bool early=argc>1 && std::string_view(argv[1])=="--early";
    auto dir=std::filesystem::temp_directory_path()/("buffer-path-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(dir)); auto trigger=(dir/"arm").string();
#ifdef _WIN32
    _putenv_s("BB_BUFFER_PATH",absent ? "":invalid ? "bad":"p1:2a937475:d1e35cbc");
    _putenv_s("BB_BUFFER_PATH_TRIGGER",trigger.c_str());
    _putenv_s("BB_BUFFER_PATH_EARLY",early ? "1":"0");
#else
    if(absent) unsetenv("BB_BUFFER_PATH"); else setenv("BB_BUFFER_PATH",invalid ? "bad":"p1:2a937475:d1e35cbc",1);
    setenv("BB_BUFFER_PATH_TRIGGER",trigger.c_str(),1);
    setenv("BB_BUFFER_PATH_EARLY",early ? "1":"0",1);
#endif
    { Scope s(p.vs,p.ps,false);
      if(early && BB_DIAGNOSTICS) { assert(Active() && !state.armed); Add({Kind::Vertex,100,100}); s.Recorded(3,1,0); }
      else assert(!Active()); }
    if(early && BB_DIAGNOSTICS) { assert(state.emitted==1); state.emitted=16; state.attempts=256; }
    FILE* f=std::fopen(trigger.c_str(),"wb"); assert(f); std::fclose(f);
    state.poll-=std::chrono::seconds(2);
    { Scope wrong(p.vs+1,p.ps,false); assert(!Active()); }
    if(!BB_DIAGNOSTICS || invalid || absent) {
        Scope s(p.vs,p.ps,false); Add({Kind::Vertex,100,100}); s.Recorded(3,1,0);
        assert(!Active() && !state.emitted && !state.count);
    } else {
        // Aborted draws emit nothing and do not leave a live scope.
        { Scope s(p.vs,p.ps,false); assert(Active()); Add({Kind::Vertex,100,100}); }
        assert(!Active() && !state.emitted && state.armed);
        for(unsigned pass=0;pass<2;++pass) {
            Scope s(p.vs,p.ps,false);
            { Scope nested(p.vs,p.ps,false); assert(!nested.owner && Active()); }
            assert(Active());
            const auto count=state.count; assert(buffer_path_peer() && state.count==count+1);
            Add({Kind::Resource,200+pass,64}); // changing constants don't defeat binding-set dedup
            Add({Kind::Vertex,100,100,0,0,0,0,44,106});
            Add({Kind::UploadQueued,100,100,42,12});
            Add({Kind::Arena,100,100,42,12});
            Add({Kind::Index,300,6});
            s.Recorded(3,1,-2); s.Recorded(3,1,-2); assert(state.emitted==1);
        }
        // Indirect recording is separate, with argument addresses only.
        { Scope s(p.vs,p.ps,true); Add({Kind::IndirectArgs,500,20});
          for(unsigned i=0;i<80;++i) Add({Kind::Vertex,600+i,44});
          assert(state.count==64 && state.overflow==17); s.Recorded(0,0,0); }
        assert(state.emitted==2 && !Active());
        std::thread t([] { assert(!Active() && !state.emitted && !state.count); }); t.join();
        // Repeated sets cannot cause unlimited collection even below the unique-set cap.
        state.attempts=255;
        { Scope s(p.vs,p.ps,false); assert(Active()); s.Recorded(0,0,0); }
        { Scope s(p.vs,p.ps,false); assert(!Active()); }
        state.attempts=0; state.emitted=16;
        { Scope s(p.vs,p.ps,false); assert(!Active()); }
    }
    std::filesystem::remove_all(dir);
    puts("Buffer path: filter, trigger, OFF/invalid gating, aborted draws, dedup, budgets, indirect and TLS passed");
}
