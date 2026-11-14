// Buffer cache and rasterizer include the observer from separate translation units.
#include "../gpu/shim/bbport_buffer_path.h"
bool buffer_path_peer() {
    if(!BbBufferPath::Active()) return false;
    BbBufferPath::Add({BbBufferPath::Kind::Obtain,100,100});
    return true;
}
