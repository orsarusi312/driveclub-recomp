/* One verified eight-byte request from the selected read window, NOT vertex data. */
#pragma once
#include <stdint.h>
typedef struct {
    uintptr_t source,destination;
    unsigned char before[8];
    int valid;
} RpkRequestTicket;
