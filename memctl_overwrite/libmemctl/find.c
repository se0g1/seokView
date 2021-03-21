#include "find.h"

#include "format.h"
#include "initialize.h"

#include "../libmemctl/kernel_memory.h"
#include "../memctl/memctl_signal.h"
#include "../memctl/utility.h"

#include <stdio.h>

bool
memctl_find(kaddr_t start, kaddr_t end, kword_t value, size_t width, bool physical, bool heap,
		size_t access, size_t alignment) {
	// Initialize the read function.
	if (!initialize(KERNEL_MEMORY)) {
		return false;
	}
	// Select the read function.
	kernel_read_fn read;
	if (physical) {
		read = physical_read_safe;
	} else {
		read = kernel_read_all;
		if (heap || read == NULL) {
			read = kernel_read_safe;
		}
	}
	if (read == NULL) {
		error_internal("cannot scan %s memory", (physical ? "physical" : "kernel"));
		return false;
	}
	// Initialize the loop.
	start = round2_up(start, alignment);
	if (start + width - 1 >= end) {
		return true;
	}
	uint8_t buf[sizeof(kword_t) + page_size];
	uint8_t *const data = buf + sizeof(kword_t);
	uint8_t *p = data;
	kaddr_t address = start;
	// Iterate over all addresses.
	for (;;) {
		size_t rsize = min(end - address, page_size);
		kaddr_t next;
		error_stop();
		read(address, &rsize, data, access, &next);
		error_start();
		if (interrupted) {
			error_interrupt();
			return false;
		}
		if (rsize > 0) {
			uint8_t *const e = data + rsize;
			for (; p + width <= e; p += alignment) {
				if (unpack_uint(p, width) == value) {
					printf(KADDR_XFMT"\n", address + (p - data));
				}
			}
			if (alignment < width && rsize == page_size) {
				p = data - width + alignment;
				*(kword_t *)buf = *(kword_t *)(data + page_size - sizeof(kword_t));
			} else {
				p = data;
			}
		} else {
			p = data;
		}
		if (next == 0 || next >= end) {
			break;
		}
		address = next;
	}
	return true;
}
