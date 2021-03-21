#ifndef MEMCTL__MEMCTL_COMMON_H_
#define MEMCTL__MEMCTL_COMMON_H_

#include "memctl_types.h"

#include <CoreFoundation/CoreFoundation.h>

// Files

/*
 * mmap_file
 *
 * Description:
 * 	Memory map the given file read-only.
 *
 * Parameters:
 * 		file			The file to memory map.
 * 	out	data			The address of the file data.
 * 	out	size			The size of the file data.
 *
 * Returns:
 * 	True if the mapping was successful.
 */
bool mmap_file(const char *file, const void **data, size_t *size);

// CoreFoundation helpers

// The recommended size of buffer for CFStringGetCStringOrConvert.
#define BUNDLE_ID_BUFFER_SIZE	2048

/*
 * CFStringGetCStringOrConvert
 *
 * Description:
 * 	Get the contents of the given CFString as a C-style string. If necessary, the string is
 * 	converted to UTF-8 and copied into the supplied buffer.
 *
 * Parameters:
 * 		string			The CFString.
 * 		buf			A conversion buffer.
 * 		size			The size of the conversion buffer.
 */
const char *CFStringGetCStringOrConvert(CFStringRef string, char *buf, size_t size);

#endif
