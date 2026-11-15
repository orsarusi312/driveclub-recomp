#include "../src/runtime_rpk_clear.h"
void rpk_clear_peer(uintptr_t destination,int value,uint64_t bytes,uintptr_t caller) {
    runtime_rpk_clear_before(destination,value,bytes,caller);
}
