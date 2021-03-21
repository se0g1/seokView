#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <mach-o/loader.h>

#include "memCtlRead.h"
#include "memCtlWrite.h"
#include "../libmemctl/format.h"
#include "../memctl/memctl_signal.h"
#include "../memctl/utility.h"
#include "../kernel/kernel_memory.h"
#include "log.h"

typedef void (*fn_t)(void);


kaddr_t write_kernel(kaddr_t address, size_t *size, void *data, memflags flags,
                 size_t access) {
  return kernel_write(address, &data, sizeof(data));

}
