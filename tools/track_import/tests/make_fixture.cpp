// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic, original RPK: two parts sharing position/attribute streams.
#include "../../modkit/src/rpk.hpp"
using namespace dcmod;
RpkResource resource(uint64_t id) {
    RpkResource r; r.id=id;
    r.data=Bytes{'E','V','O','S','L','I','T','L'};
    Wr64(r.data,id); Wr32(r.data,1); return r;
}
int main(int argc,char**argv) {
    if(argc!=2) return 2;
    RpkPack p; p.info_id=0x0001000000000001;
    const uint64_t pos=0x0003000000000001, attr=0x0003000000000002, ib=0x0004000000000001;
    const uint64_t fmt=0x0002000000000001, mid=0x0008000000000001;
    auto f=resource(fmt); f.data.push_back(2);
    for(uint8_t b: {2,15,0,0,0,5,8,0,0,0}) f.data.push_back(b);
    p.Add(f);
    auto v=resource(pos); v.deps={0x0002ea626d3b6d17ull};
    Wr16(v.data,0); Wr16(v.data,12); Wr32(v.data,12);
    for(int i=0;i<12;i++) { float xyz[3]={float(i),0,float(i%2)}; auto q=reinterpret_cast<uint8_t*>(xyz); v.data.insert(v.data.end(),q,q+12); }
    p.Add(v);
    auto a=resource(attr); a.deps={fmt};
    Wr16(a.data,0); Wr16(a.data,12); Wr32(a.data,12);
    for(int i=0;i<12;i++) { Wr32(a.data,511u<<10); Wr32(a.data,0); Wr32(a.data,0); }
    p.Add(a);
    auto ix=resource(ib); Wr16(ix.data,0); Wr16(ix.data,2); Wr32(ix.data,6);
    for(int i: {0,1,2,0,1,2}) Wr16(ix.data,i);
    p.Add(ix);
    auto m=resource(mid); m.names={"road_slot"}; m.deps={pos,attr,ib};
    for(int i=0;i<2;i++) { Wr64(m.data,pos); Wr64(m.data,attr); Wr64(m.data,ib);
        m.data.push_back(2);m.data.push_back(3); Wr32(m.data,0);Wr32(m.data,11);Wr32(m.data,i*3);Wr32(m.data,3); }
    for(int i=0;i<2;i++) { Wr32(m.data,3);Wr32(m.data,0);Wr32(m.data,0);Wr32(m.data,i*3);Wr32(m.data,3); }
    p.Add(m);p.root=mid; WriteFile(argv[1],p.Write());
}
