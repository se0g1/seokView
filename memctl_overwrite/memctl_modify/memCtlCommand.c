#include <stdio.h>
#include <assert.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <mach/vm_page_size.h>
#include <string.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "kernel_call.h"
#include "memCtlCommand.h"
#include "memCtlRead.h"
#include "../libmemctl/format.h"
#include "../libmemctl/memory.h"
#include "../libmemctl/error.h"
#include "../libmemctl/vmmap.h"
#include "../libmemctl/find.h"
#include "../kernel/kernel_memory.h"
#include "../ktrr/ktrr_bypass_parameters.h"
#include "../kernel/kernel_slide.h"
#include "../system/platform.h"


static memflags
make_memflags(bool force, bool physical) {
	return (force ? MEM_FORCE : 0) | (physical ? MEM_PHYS : 0);
}

#define OPT_GET_OR_(n_, opt_, arg_, val_, type_, field_)		\
	({ const struct argument *a = &_arguments[n_];			\
	   assert(strcmp(a->option, opt_) == 0);			\
	   assert(strcmp(a->argument, arg_) == 0);			\
	   assert(a->type == type_ || a->type == ARG_NONE);		\
	   (a->present ? a->field_ : val_); })

#define ARG_GET_(n_, arg_, type_, field_)				\
	({ const struct argument *a = &_arguments[n_];			\
	   assert(a->present);						\
	   assert(a->option == ARGUMENT || a->option == OPTIONAL);	\
	   assert(strcmp(a->argument, arg_) == 0);			\
	   assert(a->type == type_);					\
	   a->field_; })

#define ARG_GET_OR_(n_, arg_, val_, type_, field_)			\
	({ const struct argument *a = &_arguments[n_];			\
	   assert(a->option == ARGUMENT || a->option == OPTIONAL);	\
	   assert(strcmp(a->argument, arg_) == 0);			\
	   assert(a->type == type_ || a->type == ARG_NONE);		\
	   (a->present ? a->field_ : val_); })

#define OPT_PRESENT(n_, opt_)						\
	({ const struct argument *a = &_arguments[n_];			\
	   assert(strcmp(a->option, opt_) == 0);			\
	   a->present; })

#define ARG_PRESENT(n_, arg_)						\
	({ const struct argument *a = &_arguments[n_];			\
	   assert(a->option == OPTIONAL);				\
	   assert(strcmp(a->argument, arg_) == 0);			\
	   a->present; })

#define OPT_GET_INT_OR(n_, opt_, arg_, val_)		OPT_GET_OR_(n_, opt_, arg_, val_, ARG_INT, sint)
#define OPT_GET_UINT_OR(n_, opt_, arg_, val_)		OPT_GET_OR_(n_, opt_, arg_, val_, ARG_UINT, uint)
#define OPT_GET_WIDTH_OR(n_, opt_, arg_, val_)		OPT_GET_OR_(n_, opt_, arg_, val_, ARG_WIDTH, width)
#define OPT_GET_DATA_OR(n_, opt_, arg_, val_)		OPT_GET_OR_(n_, opt_, arg_, val_, ARG_DATA, data)
#define OPT_GET_STRING_OR(n_, opt_, arg_, val_)		OPT_GET_OR_(n_, opt_, arg_, val_, ARG_STRING, string)
#define OPT_GET_ARGV_OR(n_, opt_, arg_, val_)		OPT_GET_OR_(n_, opt_, arg_, val_, ARG_ARGV, argv)
#define OPT_GET_SYMBOL_OR(n_, opt_, arg_, val_)		OPT_GET_OR_(n_, opt_, arg_, val_, ARG_SYMBOL, symbol)
#define OPT_GET_ADDRESS_OR(n_, opt_, arg_, val_)	OPT_GET_OR_(n_, opt_, arg_, val_, ARG_ADDRESS, address)
#define OPT_GET_RANGE_OR(n_, opt_, arg_, start_, end_)	\
	OPT_GET_OR_(n_, opt_, arg_, ((struct argrange) { start_, end_, true, true }), ARG_RANGE, range)
#define OPT_GET_WORD_OR(n_, opt_, arg_, width_, value_)	\
	OPT_GET_OR_(n_, opt_, arg_, ((struct argword) { width_, value_ }), ARG_WORD, word)

#define ARG_GET_INT(n_, arg_)				ARG_GET_(n_, arg_, ARG_INT, sint)
#define ARG_GET_UINT(n_, arg_)				ARG_GET_(n_, arg_, ARG_UINT, uint)
#define ARG_GET_WIDTH(n_, arg_)				ARG_GET_(n_, arg_, ARG_WIDTH, width)
#define ARG_GET_DATA(n_, arg_)				ARG_GET_(n_, arg_, ARG_DATA, data)
#define ARG_GET_STRING(n_, arg_)			ARG_GET_(n_, arg_, ARG_STRING, string)
#define ARG_GET_ARGV(n_, arg_)				ARG_GET_(n_, arg_, ARG_ARGV, argv)
#define ARG_GET_SYMBOL(n_, arg_)			ARG_GET_(n_, arg_, ARG_SYMBOL, symbol)
#define ARG_GET_ADDRESS(n_, arg_)			ARG_GET_(n_, arg_, ARG_ADDRESS, address)
#define ARG_GET_RANGE(n_, arg_)				ARG_GET_(n_, arg_, ARG_RANGE, range)
#define ARG_GET_WORD(n_, arg_)				ARG_GET_(n_, arg_, ARG_WORD, word)
#define ARG_GET_WORDS(n_, arg_)				ARG_GET_(n_, arg_, ARG_WORDS, words)

#define ARG_GET_INT_OR(n_, arg_, val_)			ARG_GET_OR_(n_, arg_, val_, ARG_INT, sint)
#define ARG_GET_UINT_OR(n_, arg_, val_)			ARG_GET_OR_(n_, arg_, val_, ARG_UINT, uint)
#define ARG_GET_WIDTH_OR(n_, arg_, val_)		ARG_GET_OR_(n_, arg_, val_, ARG_WIDTH, width)
#define ARG_GET_DATA_OR(n_, arg_, val_)			ARG_GET_OR_(n_, arg_, val_, ARG_DATA, data)
#define ARG_GET_STRING_OR(n_, arg_, val_)		ARG_GET_OR_(n_, arg_, val_, ARG_STRING, string)
#define ARG_GET_ARGV_OR(n_, arg_, val_)			ARG_GET_OR_(n_, arg_, val_, ARG_ARGV, argv)
#define ARG_GET_SYMBOL_OR(n_, arg_, val_)		ARG_GET_OR_(n_, arg_, val_, ARG_SYMBOL, symbol)
#define ARG_GET_ADDRESS_OR(n_, arg_, val_)		ARG_GET_OR_(n_, arg_, val_, ARG_ADDRESS, address)
#define ARG_GET_RANGE_OR(n_, arg_, start_, end_)	\
	ARG_GET_OR_(n_, arg_, ((struct argrange) { start_, end_, true, true }), ARG_RANGE, range)
#define ARG_GET_WORD_OR(n_, arg_, width_, value_)	\
	ARG_GET_OR_(n_, arg_, ((struct argword) { width_, value_ }), ARG_WORD, word)

// struct platform platform;
#define ARGSPEC(n)	n, (struct argspec *) &(struct argspec[n])

#define HANDLER(name)							\
	static bool name(const struct argument *_arguments)


// Default values for argrange start and end values.
kaddr_t range_default_virtual_start;
kaddr_t range_default_virtual_end;

static bool
looks_like_physical_address(paddr_t address) {
#if KERNEL_BITS == 32
	return true;
#else
	return ((address & 0xffff000000000000) == 0);
#endif
}

static bool
looks_like_kernel_address(kaddr_t address) {
#if KERNEL_BITS == 32
	return (address >= 0xc0000000);
#else
	return ((address >> 40) == 0xffffff);
#endif
}

// static uint64_t
// phys_read64(uint64_t paddr) {
// 	union {
// 		uint32_t u32[2];
// 		uint64_t u64;
// 	} u;
// 	u.u32[0] = kernel_call_7(ADDRESS(ml_phys_read_data), 2, paddr, 4);
// 	u.u32[1] = kernel_call_7(ADDRESS(ml_phys_read_data), 2, paddr + 4, 4);
// 	return u.u64;
// }

// static void
// phys_write64(uint64_t paddr, uint64_t value) {
// 	kernel_call_7(ADDRESS(ml_phys_write_data), 3, paddr, value, 8);
// }


/* @cdPython Code
uint64_t kvtophys(uint64_t kvaddr) {
  if (!ADDRESS(kernelPmap) || !ADDRESS(pmap_find_phys)) {
    WARNING("kernelPmap or pmap_find_phys is not initialized");
    return false;
  }
  if (!kvaddr)
    return false;
  uint64_t paddr;
  if (ios13) {
    if (!ADDRESS(kvtophys)) {
      WARNING("kvtophys  is not initialized");
      return false;
    }
    // kvtophys function call
    uint64_t pa = kernel_call_7(ADDRESS(kvtophys), 1, kvaddr);
    uint64_t physBase = 0x800000000;
    paddr = physBase + pa;
  } else {
    uint64_t kernel_pmap = kernel_read64(ADDRESS(kernelPmap));
    // INFO("kernel_pmap: 0x%llx", kernel_pmap);
    uint64_t ppnum =
        kernel_call_7(ADDRESS(pmap_find_phys), 2, kernel_pmap, kvaddr);
    paddr = (ppnum << 14) | (kvaddr & ((1 << 14) - 1));
  }

  // INFO("paddr: 0x%llx", paddr);
  return paddr;
}
*/


bool
write_kernel(kaddr_t address, size_t *size, const void *data, memflags flags, size_t access) {
	return kernel_write(address, &data, sizeof(data));
}

static uint64_t
kvtophys(kaddr_t kvaddr) {
	bool ios13 = true;
	uint64_t paddr;
	if(ios13){ 
		uint64_t ppnum = kernel_call_7(ADDRESS(kvtophys), 1, kvaddr);
		// printf("[*] ================================================\n");
		// printf("[+] ADDRESS(kvtophys) => 0x%llx\n",ADDRESS(kvtophys));
		// printf("[+] ADDRESS(pmap_find_phys) => 0x%llx\n",ADDRESS(pmap_find_phys));
		uint64_t physBase = 0x800000000;
		paddr = physBase + ppnum;
		if(ppnum == 0){
			printf("[*] Non-existent Address\n");
			return false;
		}
		// uint64_t checkPhyaddress = phys_read64(paddr);
		// printf("[+] paddr Address => 0x%llx\n",paddr);
		// printf("[+] Physical Address => 0x%llx\n",checkPhyaddress);
		// uint64_t paddrcheck = phys_read64(paddr + 0x8);
		// uint64_t paddrcheck2 = phys_read64(paddr - 0x3734000);
		// printf("[+] not Address Check => 0x%llx\n", paddrcheck);
		// printf("[+] not Address Check => 0x%llx\n", paddrcheck2);
		// printf("[*] ================================================\n");
		return paddr;
	}
	else{ // ios12
		uint64_t ppnum = kernel_call_7(ADDRESS(pmap_find_phys), 2, kernel_pmap, kvaddr);
		return (ppnum << 14) | (kvaddr & ((1 << 14) - 1));
	}
}

bool safeacess(kaddr_t address){
	uint64_t phyAddress = kvtophys(address);
	//uint64_t result;
	if(phyAddress){
		return true;
	}
	
	return false;
}

static bool
check_address(kaddr_t address, size_t length, bool physical) {
	if (address + length < address) {
		ERROR(NULL, NULL, "overflow at address "KADDR_XFMT, address);
		return false;
	}
	if (physical) {
		if (!looks_like_physical_address(address)) {
			ERROR(NULL, NULL, "address "KADDR_XFMT" does not look like a "
			            "physical address", address);
			return false;
		}
	} else {
		if (!looks_like_kernel_address(address)) {
			ERROR(NULL, NULL, "address "KADDR_XFMT" does not look like a "
			            "kernel virtual address", address);
			return false;
		}
	}
	return true;
}


HANDLER(i_handler) {
	return i_command();
}

HANDLER(r_handler) {	
	size_t width    = OPT_GET_WIDTH_OR(0, "", "width", sizeof(kword_t));
	bool dump       = OPT_PRESENT(1, "d");
	bool force      = OPT_PRESENT(2, "f");
	bool physical   = OPT_PRESENT(3, "p");
	size_t access   = OPT_GET_WIDTH_OR(4, "x", "access", 0);
	kaddr_t address = ARG_GET_ADDRESS(5, "address");
	size_t length;
	if (ARG_PRESENT(6, "length")) {
		length = ARG_GET_UINT(6, "length");
	} else if (dump) {
		length = 256;
	} else {
		length = width;
	}
	bool checkSafe = safeacess(address);
	if(checkSafe){
		return r_command(address, length, force, physical, width, access, dump);
	}

	return false;
}

HANDLER(rb_handler) {
	bool force      = OPT_PRESENT(0, "f");
	bool physical   = OPT_PRESENT(1, "p");
	size_t access   = OPT_GET_WIDTH_OR(2, "x", "access", 0);
	kaddr_t address = ARG_GET_ADDRESS(3, "address");
	size_t length   = ARG_GET_UINT(4, "length");

	bool checkSafe = safeacess(address);
	if(checkSafe){
		return rb_command(address, length, force, physical, access);
	}
	
	return false;
	
}

HANDLER(rs_handler) {
	bool force      = OPT_PRESENT(0, "f");
	bool physical   = OPT_PRESENT(1, "p");
	size_t access   = OPT_GET_WIDTH_OR(2, "x", "access", 0);
	kaddr_t address = ARG_GET_ADDRESS(3, "address");
	size_t length   = ARG_GET_UINT_OR(4, "length", -1);
	
	bool checkSafe = safeacess(address);
	if(checkSafe){
		return rs_command(address, length, force, physical, access);
	}
	
	return false;
}

HANDLER(w_handler) {
	size_t width    = OPT_GET_WIDTH_OR(0, "", "width", sizeof(kword_t));
	bool force      = OPT_PRESENT(1, "f");
	bool physical   = OPT_PRESENT(2, "p");
	size_t access   = OPT_GET_WIDTH_OR(3, "x", "access", 0);
	kaddr_t address = ARG_GET_ADDRESS(4, "address");
	kword_t value   = ARG_GET_UINT(5, "value");
	
	bool checkSafe = safeacess(address);
	if(checkSafe){
		return w_command(address, value, force, physical, width, access);
	}
	
	return false;
}

HANDLER(wd_handler) {
	bool force          = OPT_PRESENT(0, "f");
	bool physical       = OPT_PRESENT(1, "p");
	size_t access       = OPT_GET_WIDTH_OR(2, "x", "access", 0);
	kaddr_t address     = ARG_GET_ADDRESS(3, "address");
	struct argdata data = ARG_GET_DATA(4, "data");

	bool checkSafe = safeacess(address);
	if(checkSafe){
		return wd_command(address, data.data, data.length, force, physical, access);
	}
	
	return false;
}

HANDLER(ws_handler) {
	bool force         = OPT_PRESENT(0, "f");
	bool physical      = OPT_PRESENT(1, "p");
	size_t access      = OPT_GET_WIDTH_OR(2, "x", "access", 0);
	kaddr_t address    = ARG_GET_ADDRESS(3, "address");
	const char *string = ARG_GET_STRING(4, "string");
	
	bool checkSafe = safeacess(address);
	if(checkSafe){
		return ws_command(address, string, force, physical, access);
	}
	
	return false;
}


bool
default_action(void) {
	return true;
}

// Command Setting //

bool i_command() {
	// Only initialize once.
	char memory_size_str[5];
	format_display_size(memory_size_str, platform.memory_size);
	char page_size_str[5];
	format_display_size(page_size_str, page_size);
	char *cpu_type_name;
	char *cpu_subtype_name;
	slot_name(platform.cpu_type, platform.cpu_subtype, &cpu_type_name, &cpu_subtype_name);
	// Set the page size.
	platform.page_size = vm_kernel_page_size;
	page_size = platform.page_size;
	// Get the machine name (e.g. iPhone11,8).
	struct utsname u = {};
	int error = uname(&u);
	assert(error == 0);
	char *version = strstr(u.version, "root:");
	strncpy((char *)platform.machine, u.machine, sizeof(platform.machine));
	strncpy(platform.version, version, sizeof(platform.version));
	// Get the build (e.g. 16C50).
	size_t osversion_size = sizeof(platform.osversion);
	error = sysctlbyname("kern.osversion",
			(void *)platform.osversion, &osversion_size, NULL, 0);
	assert(error == 0);
	// Get basic host info.
	mach_port_t host = mach_host_self();
	assert(MACH_PORT_VALID(host));
	//INFO("MACH_PORT_VALID(host) => %x\n",MACH_PORT_VALID(host));
	host_basic_info_data_t basic_info;
	mach_msg_type_number_t count = HOST_BASIC_INFO_COUNT;
	kern_return_t kr = host_info(host, HOST_BASIC_INFO, (host_info_t) &basic_info, &count);
	assert(kr == KERN_SUCCESS);
	platform.cpu_type     = basic_info.cpu_type;
	platform.cpu_subtype  = basic_info.cpu_subtype;
	platform.physical_cpu = basic_info.physical_cpu;
	platform.logical_cpu  = basic_info.logical_cpu;
	platform.memory_size  = basic_info.max_mem;
	mach_port_deallocate(mach_task_self(), host);
	// Log basic platform info.
	INFO("release:		 %s %s", platform.machine, platform.osversion);
	INFO("machine: 		 %s",platform.version);
	INFO("cpu type: 	 	 0x%x(%s)",platform.cpu_type,cpu_type_name);
	INFO("cpu subtype: 	 0x%x(%s)",platform.cpu_subtype,cpu_subtype_name);
	INFO("cpus: 		 0%u cores / %u threads",platform.physical_cpu,platform.logical_cpu);
	INFO("memory:		 0x%zx(%s)", platform.memory_size,memory_size_str);
	INFO("page size: 	 	 0x%lx(%s)", page_size, page_size_str);
	return true;
}


bool r_command(kaddr_t address, size_t length, bool force, bool physical, size_t width, size_t access,
		bool dump) {
	//bool checkSafe = safeacess(address);
	if (!force && !check_address(address, length, physical)) {
		return false;
	}
	memflags flags = make_memflags(force, physical);
	if (dump) {
		return memctl_dump(address, length, flags, width, access);
	} else {
		return memctl_read(address, length, flags, width, access);
	}
}

bool
rb_command(kaddr_t address, size_t length, bool force, bool physical, size_t access) {
	if (!force && !check_address(address, length, physical)) {
		return false;
	}
	memflags flags = make_memflags(force, physical);
	return memctl_dump_binary(address, length, flags, access);
}

bool
rs_command(kaddr_t address, size_t length, bool force, bool physical, size_t access) {
	// If the user didn't specify a length, then length is -1, which will result in an overflow
	// error. Instead we check for one page of validity.
	if (!force && !check_address(address, page_size, physical)) {
		return false;
	}
	memflags flags = make_memflags(force, physical);
	return memctl_read_string(address, length, flags, access);
}

bool
w_command(kaddr_t address, kword_t value, bool force, bool physical, size_t width, size_t access) {
	return wd_command(address, &value, width, force, physical, access);
}

bool
wd_command(kaddr_t address, const void *data, size_t length, bool force, bool physical,
		size_t access) {
	if (!force && !check_address(address, length, physical)) {
		return false;
	}
	memflags flags = make_memflags(force, physical);
	return write_kernel(address, &length, data, flags, access);
}

bool
ws_command(kaddr_t address, const char *string, bool force, bool physical, size_t access) {
	size_t length = strlen(string) + 1;
	return wd_command(address, string, length, force, physical, access);
}

// Command Code 

// Command Definition

static struct command commands[] = {
	{
		"i", NULL, i_handler,
		"Print system information",
		"Print general information about the system.",
		0, NULL,
	}, {
		"r", NULL, r_handler,
		"Read and print formatted memory", "8 byte align"
		"Read data from kernel virtual or physical memory and print it with the specified "
		"formatting.",
		ARGSPEC(7) {
			{ "",       "width",   ARG_WIDTH,   "The width to display each value" },
			{ "d",      NULL,      ARG_NONE,    "Use dump format with ASCII"      },
			{ "f",      NULL,      ARG_NONE,    "Force read (unsafe)"             },
			{ "p",      NULL,      ARG_NONE,    "Read physical memory"            },
			{ "x",      "access",  ARG_WIDTH,   "The memory access width"         },
			{ ARGUMENT, "address", ARG_ADDRESS, "The address to read"             },
			{ OPTIONAL, "length",  ARG_UINT,    "The number of bytes to read"     },
		},
	}, {
		"rb", "r", rb_handler,
		"Print raw binary data from memory",
		"Read data from kernel virtual or physical memory and write the binary data "
		"directly to stdout.",
		ARGSPEC(5) {
			{ "f",      NULL,      ARG_NONE,    "Force read (unsafe)"         },
			{ "p",      NULL,      ARG_NONE,    "Read physical memory"        },
			{ "x",      "access",  ARG_WIDTH,   "The memory access width"     },
			{ ARGUMENT, "address", ARG_ADDRESS, "The address to read"         },
			{ ARGUMENT, "length",  ARG_UINT,    "The number of bytes to read" },
		},
	}, {
		"rs", "r", rs_handler,
		"Read a string from memory",
		"Read and print an ASCII string from kernel memory.",
		ARGSPEC(5) {
			{ "f",      NULL,      ARG_NONE,    "Force read (unsafe)"       },
			{ "p",      NULL,      ARG_NONE,    "Read physical memory"      },
			{ "x",      "access",  ARG_WIDTH,   "The memory access width"   },
			{ ARGUMENT, "address", ARG_ADDRESS, "The address to read"       },
			{ OPTIONAL, "length",  ARG_UINT,    "The maximum string length" },
		},
	}, {
		"w", NULL, w_handler,
		"Write an integer to memory",
		"Write an integer to kernel virtual or physical memory.",
		ARGSPEC(6) {
			{ "",       "width",   ARG_WIDTH,   "The width of the value"  },
			{ "f",      NULL,      ARG_NONE,    "Force write (unsafe)"    },
			{ "p",      NULL,      ARG_NONE,    "Write physical memory"   },
			{ "x",      "access",  ARG_WIDTH,   "The memory access width" },
			{ ARGUMENT, "address", ARG_ADDRESS, "The address to write"    },
			{ ARGUMENT, "value",   ARG_UINT,    "The value to write"      },
		}, 
	}, {
		"wd", "w", wd_handler,
		"Write arbitrary data to memory",
		"Write data (specified as a hexadecimal string) to kernel memory.",
		ARGSPEC(5) {
			{ "f",      NULL,      ARG_NONE,    "Force write (unsafe)"    },
			{ "p",      NULL,      ARG_NONE,    "Write physical memory"   },
			{ "x",      "access",  ARG_WIDTH,   "The memory access width" },
			{ ARGUMENT, "address", ARG_ADDRESS, "The address to write"    },
			{ ARGUMENT, "data",    ARG_DATA,    "The data to write"       },
		},
	}, {
		"ws", "w", ws_handler,
		"Write a string to memory",
		"Write a NULL-terminated ASCII string to kernel memory.",
		ARGSPEC(5) {
			{ "f",      NULL,      ARG_NONE,    "Force write (unsafe)"    },
			{ "p",      NULL,      ARG_NONE,    "Write physical memory"   },
			{ "x",      "access",  ARG_WIDTH,   "The memory access width" },
			{ ARGUMENT, "address", ARG_ADDRESS, "The address to write"    },
			{ ARGUMENT, "string",  ARG_STRING,  "The string to write"     },
		},
	}, 
};

struct cli cli = {
	default_action,
	sizeof(commands) / sizeof(commands[0]),
	commands,
};
