#include "vmmap.h"

#include "error.h"
#include "format.h"
#include "initialize.h"

#include "core.h"
#include "../memctl/memctl_signal.h"
#include <mach/mach_vm.h>
#include "../mach/mach_vm.h"
#include <stdio.h>

bool
memctl_vmregion(kaddr_t *start, size_t *size, kaddr_t address) {
	if (!initialize(KERNEL_TASK)) {
		return false;
	}
	mach_vm_address_t vmaddress = address;
	mach_vm_size_t vmsize = 0;
	struct vm_region_basic_info_64 info;
	mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
	mach_port_t object_name = MACH_PORT_NULL;
	kern_return_t kr = mach_vm_region(kernel_task, &vmaddress, &vmsize,
			VM_REGION_BASIC_INFO_64, (vm_region_recurse_info_t) &info, &count,
			&object_name);
	if (kr == KERN_INVALID_ADDRESS) {
		return true;
	}
	if (kr != KERN_SUCCESS) {
		error_internal("mach_vm_region error: %s", mach_error_string(kr));
		return false;
	}
	if (start != NULL) {
		*start = vmaddress;
	}
	if (size != NULL) {
		*size = vmsize;
	}
	return true;
}

/*
 * share_mode_name
 *
 * Description:
 * 	Get the name of the given share mode. The returned string is always 3 characters.
 */
static const char *
share_mode_name(unsigned char share_mode) {
	switch (share_mode) {
		case SM_COW:                    return "COW";
		case SM_PRIVATE:                return "PRV";
		case SM_EMPTY:                  return "NUL";
		case SM_SHARED:                 return "ALI";
		case SM_TRUESHARED:             return "SHR";
		case SM_PRIVATE_ALIASED:        return "P/A";
		case SM_SHARED_ALIASED:         return "S/A";
		case SM_LARGE_PAGE:             return "LPG";
		default:                        return "???";
	}
}

bool
memctl_vmmap(kaddr_t kaddr, kaddr_t end, uint32_t depth) {
	if (!initialize(KERNEL_TASK)) {
		return false;
	}
	for (bool first = true;; first = false) {
		mach_vm_address_t address = kaddr;
		mach_vm_size_t size = 0;
		uint32_t depth0 = depth;
		vm_region_submap_info_data_64_t info;
		mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
		kern_return_t kr = mach_vm_region_recurse(kernel_task, &address, &size,
				&depth0, (vm_region_recurse_info_t)&info, &count);
		if (interrupted) {
			error_interrupt();
			return false;
		}
		if (kr != KERN_SUCCESS || address > end) {
			if (first) {
				if (kaddr == end) {
					printf("no virtual memory region contains address %p\n",
							(void *)kaddr);
				} else {
					printf("no virtual memory region intersects %p-%p\n",
							(void *)kaddr, (void *)end);
				}
			}
			break;
		}
		if (first) {
			printf("          START - END             [ VSIZE ] "
					"PRT/MAX SHRMOD DEPTH RESIDENT REFCNT TAG\n");
		}
		char vsize[5];
		format_display_size(vsize, size);
		char cur_prot[4];
		format_memory_protection(cur_prot, info.protection);
		char max_prot[4];
		format_memory_protection(max_prot, info.max_protection);
		printf("%016llx-%016llx [ %5s ] %s/%s %6s %5u %8u %6u %3u\n",
				address, address + size,
				vsize,
				cur_prot, max_prot,
				share_mode_name(info.share_mode),
				depth0,
				info.pages_resident,
				info.ref_count,
				info.user_tag);
		kaddr = address + size;
	}
	return true;
}

bool
memctl_vmprotect(kaddr_t address, size_t size, int prot) {
	if (!initialize(KERNEL_TASK)) {
		return false;
	}
	kern_return_t kr = mach_vm_protect(kernel_task, address, size, FALSE, prot);
	if (kr != KERN_SUCCESS) {
		error_internal("mach_vm_protect failed: %s", mach_error_string(kr));
		return false;
	}
	return true;
}
