#ifndef SIDEBAND_DECODER
#define SIDEBAND_DECODER 

#include <inttypes.h>
#include <stdio.h>
#include "pevent.hpp"

class SidebandDecoder{

private:
	uint8_t *begin;
	uint8_t *end;
	uint8_t *current;

    struct pev_config config;

	int (*sideband_callback)(void *priv, struct pev_event *event);
public:
	SidebandDecoder(uint8_t * buffer, size_t size) {
		begin = buffer;
		end = buffer+size;
		current = begin;
		sideband_callback = nullptr;
	}
	uint8_t *get_begin() {
		return begin;
	}
	void set_config(struct pev_config pevent) {
		config = pevent;
	}
	/* return 0 if there is no sideband event
	 * return 1 if there is a sideband event
	 */
	int sideband_event(uint64_t time, struct pev_event &event);
};

#endif