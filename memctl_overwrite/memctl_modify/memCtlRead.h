#include "../libmemctl/memctl_types.h"
#include "../libmemctl/memory.h"
#include "../memctl/disassemble.h"
/*
 * memctl_read
 *
 * Description:
 * 	Read kernel memory and format the output to stdout.
 *
 * Parameters:
 * 		address			The kernel address to read.
 * 		size			The number of bytes to read.
 * 		flags			Memory access flags.
 * 		width			The formatting width.
 * 		access			The access width while reading.
 *
 * Returns:
 * 	True if the read was successful.
 */
bool memctl_read(uint64_t address, size_t size, memflags flags, size_t width, size_t access);

/*
 * memctl_dump
 *
 * Description:
 * 	Read kernel memory and write a dump output to stdout.
 *
 * Parameters:
 * 		address			The kernel address to read.
 * 		size			The number of bytes to read.
 * 		flags			Memory access flags.
 * 		width			The formatting width.
 * 		access			The access width while reading.
 *
 * Returns:
 * 	True if the read was successful.
 */
bool memctl_dump(uint64_t address, size_t size, memflags flags, size_t width, size_t access);

/*
 * memctl_dump_binary
 *
 * Description:
 * 	Dump raw kernel memory to stdout.
 *
 * Parameters:
 * 		address			The kernel address to read.
 * 		size			The number of bytes to read.
 * 		flags			Memory access flags.
 * 		access			The access width while reading.
 *
 * Returns:
 * 	True if the read was successful.
 */
bool memctl_dump_binary(kaddr_t address, size_t size, memflags flags, size_t access);

/*
 * memctl_disassemble
 *
 * Description:
 * 	Disassemble kernel memory to stdout.
 *
 * Parameters:
 * 		address			The kernel address to start disassembling at.
 * 		length			The number of bytes to read.
 * 		flags			Memory access flags.
 * 		access			The access width while reading.
 *
 * Returns:
 * 	True if the read was successful.
 */
bool memctl_disassemble(kaddr_t address, size_t length, memflags flags, size_t access);

/*
 * memctl_read_string
 *
 * Description:
 * 	Read the C-style string starting at the given address.
 *
 * Parameters:
 * 		address			The kernel address to read.
 * 		size			The maximum number of bytes to read.
 * 		flags			Memory access flags.
 * 		access			The access width while reading.
 *
 * Returns:
 * 	True if the read was successful.
 */
bool memctl_read_string(kaddr_t address, size_t size, memflags flags, size_t access);

extern mach_port_t tfp0;
typedef uint64_t kaddr_t;

kern_return_t mach_vm_read(
                           vm_map_t target_task,
                           mach_vm_address_t address,
                           mach_vm_size_t size,
                           vm_offset_t *data,
                           mach_msg_type_number_t *dataCnt);

kern_return_t mach_vm_write(
                            vm_map_t target_task,
                            mach_vm_address_t address,
                            vm_offset_t data,
                            mach_msg_type_number_t dataCnt);

kern_return_t mach_vm_read_overwrite(
                                     vm_map_t target_task,
                                     mach_vm_address_t address,
                                     mach_vm_size_t size,
                                     mach_vm_address_t data,
                                     mach_vm_size_t *outsize);

uint64_t read64(kaddr_t addr);
uint32_t read32(kaddr_t addr);

void *read_bytes_with_size(kaddr_t addr, vm_size_t rsize);
size_t write_bytes_with_size(uint64_t addr, const void *buf, size_t wsize);

uint64_t kmem_alloc(vm_size_t size);
void write64(uint64_t address,uint64_t value);
void write32(uint64_t address,uint32_t value);
