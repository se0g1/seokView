//
// Project: KTRW
// Author:  Brandon Azad <bazad@google.com>
//
// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "ktrr_bypass.h"

#include <assert.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "kernel_call.h"
#include "kernel_memory.h"
#include "kernel_slide.h"
#include "ktrr_bypass_parameters.h"
#include "log.h"


// The page table base.
static uint64_t ttbr1_el1;

// ---- Utility functions -------------------------------------------------------------------------

/*
 * kvtophys
 *
 * Description:
 * 	Convert a kernel virtual address to a physical address.
 */
// static uint64_t
// kvtophys(uint64_t kvaddr) {
// 	uint64_t ppnum = kernel_call_7(ADDRESS(pmap_find_phys), 2, kernel_pmap, kvaddr);
// 	return (ppnum << 14) | (kvaddr & ((1 << 14) - 1));
// }

/*
 * phys_read64
 *
 * Description:
 * 	Read a 64-bit value from the specified physical address.
 */
static uint64_t
phys_read64(uint64_t paddr) {
	union {
		uint32_t u32[2];
		uint64_t u64;
	} u;
	u.u32[0] = kernel_call_7(ADDRESS(ml_phys_read_data), 2, paddr, 4);
	u.u32[1] = kernel_call_7(ADDRESS(ml_phys_read_data), 2, paddr + 4, 4);
	return u.u64;
}

/*
 * phys_write64
 *
 * Description:
 * 	Write a 64-bit value to the specified physical address.
 */
static void
phys_write64(uint64_t paddr, uint64_t value) {
	kernel_call_7(ADDRESS(ml_phys_write_data), 3, paddr, value, 8);
}

// ---- KTRR Bypass -------------------------------------------------------------------------------

#define DBGWRAP_Restart		(1uL << 30)
#define DBGWRAP_HaltAfterReset	(1uL << 29)	// EDECR.RCE ?
#define DBGWRAP_DisableReset	(1uL << 26)	// EDPRCR.CORENPDRQ ?

// ---- Remapping RoRgn ---------------------------------------------------------------------------

/*
 * aarch64_page_table_lookup
 *
 * Description:
 * 	Perform a page table lookup. Returns the physical address.
 *
 * Parameters:
 * 	ttb		The translation table base address (from TTBR0_EL1 or TTBR1_EL1).
 * 	p_l1_tte	The address of the L1 TTE.
 * 	l1_tte		The L1 TTE.
 * 	p_l2_tte	The address of the L2 TTE.
 * 	l2_tte		The L2 TTE.
 * 	p_l3_tte	The address of the L3 TTE.
 * 	l3_tte		The L3 TTE.
 */
static uint64_t
aarch64_page_table_lookup(uint64_t ttb,
		uint64_t vaddr,
		uint64_t *p_l1_tte0, uint64_t *l1_tte0,
		uint64_t *p_l2_tte0, uint64_t *l2_tte0,
		uint64_t *p_l3_tte0, uint64_t *l3_tte0) {
	const uint64_t pg_bits = 14;
	const uint64_t l1_size = 3;
	const uint64_t l2_size = 11;
	const uint64_t l3_size = 11;
	const uint64_t tte_physaddr_mask = ((1uLL << 40) - 1) & ~((1 << pg_bits) - 1);
	uint64_t l1_table = ttb;
	uint64_t l1_index = (vaddr >> (l2_size + l3_size + pg_bits)) & ((1 << l1_size) - 1);
	uint64_t l2_index = (vaddr >> (l3_size + pg_bits)) & ((1 << l2_size) - 1);
	uint64_t l3_index = (vaddr >> pg_bits) & ((1 << l3_size) - 1);
	uint64_t pg_offset = vaddr & ((1 << pg_bits) - 1);
	uint64_t p_l1_tte = l1_table + 8 * l1_index;
	if (p_l1_tte0 != NULL) {
		*p_l1_tte0 = p_l1_tte;
	}
	uint64_t l1_tte = phys_read64(p_l1_tte);
	if (l1_tte0 != NULL) {
		*l1_tte0 = l1_tte;
	}
	if ((l1_tte & 3) != 3) {
		return -1;
	}
	uint64_t l2_table = l1_tte & tte_physaddr_mask;
	uint64_t p_l2_tte = l2_table + 8 * l2_index;
	if (p_l2_tte0 != NULL) {
		*p_l2_tte0 = p_l2_tte;
	}
	uint64_t l2_tte = phys_read64(p_l2_tte);
	if (l2_tte0 != NULL) {
		*l2_tte0 = l2_tte;
	}
	if ((l2_tte & 3) != 3) {
		return -1;
	}
	uint64_t l3_table = l2_tte & tte_physaddr_mask;
	uint64_t p_l3_tte = l3_table + 8 * l3_index;
	if (p_l3_tte0 != NULL) {
		*p_l3_tte0 = p_l3_tte;
	}
	uint64_t l3_tte = phys_read64(p_l3_tte);
	if (l3_tte0 != NULL) {
		*l3_tte0 = l3_tte;
	}
	if ((l3_tte & 3) != 3) {
		return -1;
	}
	uint64_t frame = l3_tte & tte_physaddr_mask;
	return frame | pg_offset;
}

/*
 * clear_pxn_from_tte
 *
 * Description:
 * 	Clears the PXN bit from a TTE in a translation table page.
 */
static uint64_t
clear_pxn_from_tte(unsigned level, uint64_t tte) {
	if (0 <= level && level <= 2) {	// L0, L1, L2
		if ((tte & 0x3) == 0x3) {	// Table
			tte &= ~(1uLL << 59);	// PXNTable
		} else if (level == 2 && (tte & 0x3) == 0x1) {	// Block
			tte &= ~(1uLL << 53);	// PXN
		}
	}
	if (level == 3) {	// L3
		if ((tte & 0x3) == 0x3) {	// Page
			tte &= ~(1uLL << 53);	// PXN
		}
	}
	return tte;
}


// ---- Public API --------------------------------------------------------------------------------

bool
have_ktrr_bypass() {
	return ktrr_bypass_parameters_init();
}

bool
ktrr_bypass() {
	bool ok = ktrr_bypass_parameters_init();
	return ok;
}

void
ktrr_vm_protect(uint64_t address, size_t size, int prot) {
	kernel_vm_protect(address, size, prot);
	if (prot & VM_PROT_EXECUTE) {
		const uint64_t page_mask = ~(page_size - 1);
		uint64_t start = address & page_mask;
		uint64_t end = (address + size + page_size - 1) & page_mask;
		for (uint64_t page = start; page < end; page += page_size) {
			uint64_t p_l3_tte = 0, l3_tte = 0;
			uint64_t page_p = aarch64_page_table_lookup(ttbr1_el1, page,
					NULL, NULL, NULL, NULL, &p_l3_tte, &l3_tte);
			if (page_p != -1) {
				uint64_t new_l3_tte = clear_pxn_from_tte(3, l3_tte);
				phys_write64(p_l3_tte, new_l3_tte);
			}
		}
	}
}
