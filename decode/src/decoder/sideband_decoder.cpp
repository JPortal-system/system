#include "decoder/sideband_decoder.hpp"
#include <stdio.h>
int SidebandDecoder::sideband_event(uint64_t time, struct pev_event &event) {
    int size;
    pev_event_init(&event);
    size = pev_read(&event, current, end, &config);
    if (size < 0 || time < event.sample.tsc) {
        return 0;
    }
    current += size;
    return 1;
}