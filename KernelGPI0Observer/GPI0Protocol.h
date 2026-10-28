#pragma once
#include <stdint.h>
// Fixed ABI: no user-supplied address, length, map handle, or register offset.
enum { kG0RunObservation = 0, kG0ProtocolVersion = 2 };
typedef struct {
    uint64_t resource_phys, resource_length;
    uint32_t map_options, revid, caplist, padbar, stride, cfg_offset;
    uint32_t cfg0, cfg1, padown, hostsw, cfglock, txlock;
    uint32_t protocol_version, status, map_options_requested, read_count;
} GPI0ObservationResult;
#ifdef __cplusplus
static_assert(sizeof(GPI0ObservationResult) == 80, "G0 fixed ABI size");
#else
_Static_assert(sizeof(GPI0ObservationResult) == 80, "G0 fixed ABI size");
#endif
