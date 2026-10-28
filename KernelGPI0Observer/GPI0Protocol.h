#pragma once
#include <stdint.h>
// Fixed ABI: no user-supplied address, length, map handle, or register offset.
enum { kG0RunObservation = 0 };
typedef struct {
    uint64_t resource_phys, resource_length;
    uint32_t map_options, revid, caplist, padbar, stride, cfg_offset;
    uint32_t cfg0, cfg1, padown, hostsw, cfglock, txlock;
} GPI0ObservationResult;
