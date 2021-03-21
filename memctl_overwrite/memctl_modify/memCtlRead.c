#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <mach-o/loader.h>

#include "memCtlRead.h"
#include "../libmemctl/format.h"
#include "../memctl/memctl_signal.h"
#include "../memctl/utility.h"
#include "../kernel/kernel_memory.h"
#include "log.h"

typedef void (*fn_t)(void);


kaddr_t read_kernel(kaddr_t address, size_t *size, void *data, memflags flags,
                 size_t access) {

  uint64_t value;
  bool ok = kernel_read(address, &value, sizeof(value));
  if (!ok) {
    return -1;
  }
  return value;
}

bool memctl_dump(kaddr_t address, size_t size, memflags flags, size_t width,
                 size_t access) {
  assert(ispow2(width) && 0 < width && width <= sizeof(kword_t));
  assert(ispow2(access) && access <= sizeof(kword_t));
  uint8_t data[page_size];
  uint8_t *p = data;
  uint8_t *end = p;
  width--;
  kaddr_t read_success = true;
  /* Iterate one line of output at a time. */
  while (size > 0) {
    char hex[64];
    char ascii[32];
    unsigned hexidx = 0;
    unsigned asciiidx = 0;
    unsigned off = address & 0xf;
    address -= off;
    unsigned i = 0;
    /* Format any leading blanks due to starting on a misaligned address. */
    for (; i < off; i++) {
      hexidx += sprintf(hex + hexidx, "  ");
      if ((i & width) == width) {
        hexidx += sprintf(hex + hexidx, " ");
      }
      asciiidx += sprintf(ascii + asciiidx, " ");
    }
    /* Format the hex and ascii data read from the kernel. */
    for (; size > 0 && i < 16; i++, size--, p++) {
      if (p == end) {
        /* If the last time we grabbed data there was an error, report it
           now. */
        if (!read_success) {
          return false;
        }
        /* Grab more data from the kernel. */
        size_t readsize = min(size, sizeof(data));
        read_success = read_kernel(address + i, &readsize, data, flags, access);
        if (interrupted) {
          error_interrupt();
          return false;
        }
        if (readsize == 0) {
          return false;
        }
        p = data;
        end = data + readsize;
      }
      hexidx += sprintf(hex + hexidx, "%02x", *p);
      if ((i & width) == width) {
        hexidx += sprintf(hex + hexidx, " ");
      }
      asciiidx += sprintf(ascii + asciiidx, "%c",
                          (isascii(*p) && isprint(*p) ? *p : '.'));
    }
    /* Format any trailing blanks due to ending on a misaligned address. */
    for (; i < 16; i++) {
      hexidx += sprintf(hex + hexidx, "  ");
      if ((i & width) == width) {
        hexidx += sprintf(hex + hexidx, " ");
      }
      asciiidx += sprintf(ascii + asciiidx, " ");
    }
    /* Print the dump line. */
    printf(KADDR_FMT ":  %s |%s|\n", address, hex, ascii);
    /* Advance. */
    address += 16;
  }
  return true;
}

bool memctl_read(kaddr_t address, size_t size, memflags flags, size_t width,
                 size_t access) {
  assert(ispow2(width) && 0 < width && width <= sizeof(kword_t));
  assert(ispow2(access) && access <= sizeof(kword_t));

  uint8_t data[page_size];
  unsigned n = min(16 / width, 8);
  while (size > 0) {
    // Read as many bytes as we can.
    size_t readsize = min(size, sizeof(data));
    // INFO("address => 0x%llx\n",address);
    // INFO("size => 0x%ld\n",size);
    // INFO("width => 0x%ld\n",width);
    // INFO("readsize => %ld",readsize);
    // Print each word.
    size_t left = readsize;
    
    for (size_t i = 0; left > 0; i++) {
      if (interrupted) {
        error_interrupt();
        return false;
      }
      kaddr_t read_success = read_kernel(address+(i), &readsize, data, flags, access);
      if(i > 0) {
        read_success = read_kernel(address+(i*8), &readsize, data, flags, access);
        
      }
      // Truncate the width to however many bytes are left.
      int w = min(width, left);
      // Extract the integer.
      uint8_t *p = data + width * i;
      //kword_t value = unpack_uint_e(p, w, host_is_little_endian());
      unpack_uint_e(p, w, host_is_little_endian());
      if (i % n == 0) {
        printf(KADDR_FMT ":  ", address);
      }
      left -= w;
      address += w;
      // Print a new line if either we've saturated the line or if we're out of
      // data to print.
      int newline = (((i + 1) % n == 0) || left == 0);
      // Add left padding if we're printing part of a little-endian value.
      int leftpad = (host_is_little_endian() ? 2 * (width - w) : 0);

      printf("%*s%0*llx%c", leftpad, "", 2 * w, read_success, (newline ? '\n' : ' '));
      
      if (!read_success) {
      return false;
    }
    }
    size -= readsize;
  }
  return true;
}

bool
memctl_dump_binary(kaddr_t address, size_t size, memflags flags, size_t access) {
  assert(ispow2(access) && access <= sizeof(kword_t));
  uint8_t data[page_size];
  while (size > 0) {
    size_t readsize = min(size, sizeof(data));
    bool read_success = read_kernel(address, &readsize, data, flags, access);
    uint8_t *p = data;
    size_t left = readsize;
    while (left > 0) {
      if (interrupted) {
        error_interrupt();
        return false;
      }
      size_t written = fwrite(p, 1, left, stdout);
      if (ferror(stdout)) {
        error_internal("could not write to stdout");
        return false;
      }
      p += written;
      left -= written;
    }
    if (!read_success) {
      return false;
    }
    size -= readsize;
  }
  return true;
}


bool
memctl_read_string(kaddr_t address, size_t size, memflags flags, size_t access) {
  assert(ispow2(access) && access <= sizeof(kword_t));
  uint8_t data[page_size + 1];
  bool have_printed = false;
  bool read_success = true;
  bool end = false;
  while (!end) {
    size_t readsize = min(size, sizeof(data) - 1);
    read_success = read_kernel(address, &readsize, data, flags, access);
    if (interrupted) {
      error_interrupt();
      return false;
    }
    data[readsize] = 0;
    size_t len = strlen((char *)data);
    size -= readsize;
    address += readsize;
    end = (len < readsize || size == 0 || !read_success);
    printf("%s%s", (char *)data, (end && (have_printed || len > 0) ? "\n" : ""));
    have_printed = true;
  }
  return read_success;
}
