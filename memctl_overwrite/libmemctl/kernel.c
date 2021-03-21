#include "kernel.h"

#include "kernel_slide.h"
#include "memctl_error.h"

#include "algorithm.h"
#include "memctl_common.h"

#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

const char KERNEL_ID[] = "__kernel__";

struct kext kernel;

/*
 * struct kext_info
 *
 * Description:
 * 	Used to keep track of additional information about a kext.
 */
struct kext_info {
	// The kext.
	struct kext kext;
	// The reference count.
	unsigned refcount;
	// The following state is used to keep track of whether the kext is out-of-date.
#if !KERNELCACHE
	// Whether the current kext is outdated, and thus should be freed when its reference count
	// drops to 0.
	bool outdated;
	// The kext's UUID.
	uuid_t uuid;
	// The kext's parsed version number.
	uint64_t version;
#endif
	// The bundle identifier.
	char bundle_id[1];
};


/*
 * array_insert
 *
 * Description:
 * 	Make space for an element in an array at the specified index. A pointer to the element is
 * 	returned.
 */
static void *
array_insert(void **array, size_t width, size_t *count, size_t index) {
	size_t newcount = *count + 1;
	void *newarray = realloc(*array, newcount * width);
	if (newarray == NULL) {
		return NULL;
	}
	*array = newarray;
	*count = newcount;
	void *shift_src = (void *)((uintptr_t)newarray + index * width);
	void *shift_dst = (void *)((uintptr_t)shift_src + width);
	size_t shift_size = (newcount - 1 - index) * width;
	memmove(shift_dst, shift_src, shift_size);
	return shift_src;
}

#if !KERNELCACHE
/*
 * array_remove
 *
 * Description:
 * 	Remove an element from an array and move the rest of the elements forward.
 */
static void
array_remove(void *array, size_t width, size_t *count, size_t index) {
	size_t newcount = *count - 1;
	*count = newcount;
	void *shift_dst = (void *)((uintptr_t)array + index * width);
	void *shift_src = (void *)((uintptr_t)shift_dst + width);
	size_t shift_size = (newcount - index) * width;
	memmove(shift_dst, shift_src, shift_size);
}
#endif

// ---- Kext analyzers / symbol finders -----------------------------------------------------------

/*
 * struct kext_analyzers
 *
 * Description:
 * 	A struct to keep track of the analyzers for a particular kernel extension.
 */
struct kext_analyzers {
	// The bundle ID. This string is managed by the caller who created this analyzer.
	const char *bundle_id;
	// An array of symbol finders.
	kext_symbol_finder_fn *symbol_finders;
	// The number of elements in the symbol_finders array.
	size_t symbol_finders_count;
};

// The array of kext analyzers.
struct kext_analyzers *all_analyzers;

// The number of kext analyzers.
size_t all_analyzers_count;

/*
 * compare_kext_analyzers
 *
 * Description:
 * 	Compare the bundle ID of a kext_analyzers struct.
 */
static int
compare_kext_analyzers(const void *key, const void *element) {
	const char *bundle_id = key;
	const struct kext_analyzers *ka = element;
	return strcmp(bundle_id, ka->bundle_id);
}

/*
 * find_kext_analyzers_for_id
 *
 * Description:
 * 	Find the existing kext_analyzers struct for the given bundle ID.
 */
static struct kext_analyzers *
find_kext_analyzers_for_id(const char *bundle_id, size_t *insert_index) {
	return (struct kext_analyzers *) binary_search(all_analyzers, sizeof(*all_analyzers),
			all_analyzers_count, compare_kext_analyzers, bundle_id, insert_index);
}

/*
 * create_kext_analyzers
 *
 * Description:
 * 	Find the kext_analyzers struct for the given bundle ID, or create one if it doesn't already
 * 	exist.
 */
static struct kext_analyzers *
create_kext_analyzers(const char *bundle_id) {
	if (bundle_id == NULL) {
		bundle_id = "";
	}
	// Try to find the kext_analyzers struct if it already exists.
	size_t insert_index;
	struct kext_analyzers *ka = find_kext_analyzers_for_id(bundle_id, &insert_index);
	if (ka != NULL) {
		return ka;
	}
	// Insert space for a new kext_analyzers in the all_analyzers array.
	ka = array_insert((void **) &all_analyzers, sizeof(*all_analyzers), &all_analyzers_count,
			insert_index);
	if (ka == NULL) {
		error_out_of_memory();
		return NULL;
	}
	// Fill in the new kext_analyzers.
	ka->bundle_id            = bundle_id;
	ka->symbol_finders       = NULL;
	ka->symbol_finders_count = 0;
	return ka;
}

/*
 * clear_kext_analyzers
 *
 * Description:
 * 	Clear the specified kext_analyzers struct and free associated resources.
 */
static void
clear_kext_analyzers(struct kext_analyzers *ka) {
	free(ka->symbol_finders);
	ka->symbol_finders       = NULL;
	ka->symbol_finders_count = 0;
}

/*
 * clear_all_analyzers
 *
 * Description:
 * 	Remove all analyzers and free associated resources.
 */
static void
clear_all_analyzers() {
	for (size_t i = 0; i < all_analyzers_count; i++) {
		clear_kext_analyzers(&all_analyzers[i]);
	}
	free(all_analyzers);
	all_analyzers       = NULL;
	all_analyzers_count = 0;
}

/*
 * kext_analyzers_insert_symbol_finder
 *
 * Description:
 * 	Add a symbol finder to the kext_analyzers struct.
 */
static bool
kext_analyzers_insert_symbol_finder(struct kext_analyzers *ka,
		kext_symbol_finder_fn symbol_finder) {
	// Allocate space for the new symbol finder.
	size_t count = ka->symbol_finders_count + 1;
	kext_symbol_finder_fn *fn = realloc(ka->symbol_finders, count * sizeof(*fn));
	if (fn == NULL) {
		error_out_of_memory();
		return false;
	}
	// Insert the symbol finder.
	ka->symbol_finders       = fn;
	ka->symbol_finders_count = count;
	fn[count - 1] = symbol_finder;
	return true;
}

/*
 * run_symbol_finders
 *
 * Description:
 * 	Run the symbol finders from the given kext_analyzers struct.
 */
static void
run_symbol_finders(const struct kext_analyzers *ka, struct kext *kext) {
	kext_symbol_finder_fn *symbol_finder = ka->symbol_finders;
	kext_symbol_finder_fn *end = symbol_finder + ka->symbol_finders_count;
	error_stop();
	for (; symbol_finder < end; symbol_finder++) {
		(*symbol_finder)(kext);
	}
	error_start();
}

/*
 * init_kext_symbols
 *
 * Description:
 * 	Initialize the symbols in a kext. This includes initializing the symtab and running any
 * 	matching symbol finders on the kext.
 */
static bool
init_kext_symbols(struct kext *kext) {
	bool success = symbol_table_init_with_macho(&kext->symtab, &kext->macho);
	if (!success) {
		return false;
	}
	assert(kext->bundle_id != NULL);
	const char *bundle_ids[2] = { kext->bundle_id, "" };
	for (size_t i = 0; i < 2; i++) {
		struct kext_analyzers *ka = find_kext_analyzers_for_id(bundle_ids[i], NULL);
		if (ka != NULL) {
			run_symbol_finders(ka, kext);
		}
	}
	return true;
}

/*
 * deinit_kext_symbols
 *
 * Description:
 * 	Free resources allocated by init_kext_symbols.
 */
static void
deinit_kext_symbols(struct kext *kext) {
	symbol_table_deinit(&kext->symtab);
}

// ---- Mapping of bundle IDs to kext structs -----------------------------------------------------



// Load information for a kext, as per oskext_load_info.
struct load_info {
#if !KERNELCACHE
	kaddr_t  base;
	uuid_t   uuid;
	uint64_t version;
#endif
};

// An array of pointers to kext_info structs. These structs must live at the same address for their
// entire lifetime, so they are stored indirectly.
struct kext_info **kexts;

// The number of elements in the kexts array.
size_t kexts_count;


/*
 * deinit_kext_macho
 *
 * Description:
 * 	Clean up a macho struct initialized with init_kext_macho.
 */
static void
deinit_kext_macho(struct macho *macho) {
#if !KERNELCACHE
	if (macho->mh != kernel.macho.mh) {
		oskext_deinit_macho(macho);
	}
#endif
}

/*
 * create_kext_info
 *
 * Description:
 * 	Create a new kext_info structure to store a kext. The refcount is initialized to 1.
 *
 * Returns:
 * 	KEXT_SUCCESS, KEXT_ERROR, KEXT_NO_KEXT
 */

/*
 * destroy_kext_info
 *
 * Description:
 * 	Free the resources used by create_kext_info.
 */
static void
destroy_kext_info(struct kext_info *kext_info) {
	deinit_kext_symbols(&kext_info->kext);
	deinit_kext_macho(&kext_info->kext.macho);
	free(kext_info);
}

#if !KERNELCACHE
/*
 * is_outdated
 *
 * Description:
 * 	Returns true if the load_info for the kext indicates that the kext is outdated.
 */
static kext_result
is_outdated(const struct kext_info *ki, const struct load_info *li) {
	return (ki->kext.base != li->base
	        || ki->version != li->version
	        || memcmp(ki->uuid, li->uuid, sizeof(li->uuid)) != 0);
}
#endif

/*
 * handle_kext_release
 *
 * Description:
 * 	Destroy the kext_info struct if it is outdated and its reference count is zero.
 */
static void
handle_kext_release(struct kext_info *kext_info) {
#if !KERNELCACHE
	if (kext_info->refcount == 0 && kext_info->outdated) {
		destroy_kext_info(kext_info);
	}
#endif
}

/*
 * clear_kexts
 *
 * Description:
 * 	Free all the kexts in the kexts array.
 */
static void
clear_kexts() {
	for (size_t i = 0; i < kexts_count; i++) {
		struct kext_info *ki = kexts[i];
		if (ki->refcount > 0) {
			memctl_warning("kext %s has outstanding reference during deinitialization",
			               ki->bundle_id);
		}
		destroy_kext_info(ki);
	}
	free(kexts);
	kexts = NULL;
	kexts_count = 0;
}

// ---- Public API --------------------------------------------------------------------------------

// The path of the currently initialized kernel.
static const char *initialized_kernel = NULL;

/*
 * kernel_init_macos
 *
 * Description:
 * 	Initialize kernel.macho and kernel.base.
 */
static bool
kernel_init_macos(const char *kernel_path) {
	// mmap the kernel file.
	if (!mmap_file(kernel_path, (const void **)&kernel.macho.mh, &kernel.macho.size)) {
		// kernel_deinit will be called.
		assert(kernel.macho.mh == NULL);
		return false;
	}
	macho_result mr = macho_validate(kernel.macho.mh, kernel.macho.size);
	if (mr != MACHO_SUCCESS) {
		error_internal("%s is not a valid Mach-O file", kernel_path);
		return false;
	}
	// Set the runtime base and slide.
	uint64_t static_base;
	mr = macho_find_base(&kernel.macho, &static_base);
	if (mr != MACHO_SUCCESS) {
		error_internal("%s does not have a Mach-O base address", kernel_path);
		return false;
	}
	kernel.base = static_base + kernel_slide;
	return true;
}


bool
kernel_init(const char *kernel_path) {
	if (kernel_path == NULL) {
		kernel_path = KERNEL_PATH;
	}
	if (initialized_kernel != NULL) {
		if (strcmp(kernel_path, initialized_kernel) == 0) {
			// Reset the runtime base and slide based on the new value of kernel_slide.
			kaddr_t static_base = kernel.base - kernel.slide;
			kernel.base  = static_base + kernel_slide;
			kernel.slide = kernel_slide;
			return true;
		}
		kernel_deinit();
	}

	if (!kernel_init_macos(kernel_path)) {
		goto fail;
	}

	kernel.slide = kernel_slide;
	kernel.bundle_id = KERNEL_ID;
	// Initialize the symtab.
	if (!init_kext_symbols(&kernel)) {
		goto fail;
	}
	initialized_kernel = kernel_path;
	return true;
fail:
	kernel_deinit();
	return false;
}

void
kernel_deinit() {
	initialized_kernel = NULL;
	if (kernel.macho.mh != NULL) {
		munmap(kernel.macho.mh, kernel.macho.size);
		kernel.macho.mh = NULL;
	}
	deinit_kext_symbols(&kernel);
	clear_all_analyzers();
	clear_kexts();
}

void
kext_release(const struct kext *kext) {
	assert(kext != NULL);
	if (kext->macho.mh == kernel.macho.mh) {
		return;
	}
	struct kext_info *ki = (struct kext_info *)(kext);
	assert(kext->bundle_id == ki->bundle_id);
	assert(ki->refcount >= 1);
	ki->refcount -= 1;
	handle_kext_release(ki);
}


bool
kext_add_symbol_finder(const char *bundle_id, kext_symbol_finder_fn symbol_finder) {
	struct kext_analyzers *ka = create_kext_analyzers(bundle_id);
	if (ka == NULL) {
		return false;
	}
	return kext_analyzers_insert_symbol_finder(ka, symbol_finder);
}

kext_result
kext_find_symbol(const struct kext *kext, const char *symbol, kaddr_t *address, size_t *size) {
	uint64_t static_address;
	bool found = symbol_table_resolve_symbol(&kext->symtab, symbol, &static_address, size);
	if (!found) {
		return KEXT_NOT_FOUND;
	}
	*address = static_address + kext->slide;
	return KEXT_SUCCESS;
}

kext_result
kext_resolve_address(const struct kext *kext, kaddr_t address, const char **name, size_t *size,
		size_t *offset) {
	uint64_t static_address = address - kext->slide;
	bool found = symbol_table_resolve_address(&kext->symtab, static_address, name, size,
			offset);
	if (!found) {
		if (name != NULL) {
			*name = NULL;
		}
		if (size != NULL) {
			*size = 0;
		}
		if (offset != NULL) {
			*offset = address - kext->base;
		}
		return KEXT_NOT_FOUND;
	}
	return KEXT_SUCCESS;
}

kext_result
kext_search_data(const struct kext *kext, const void *data, size_t size, int minprot,
		kaddr_t *address) {
	uint64_t static_address;
	macho_result mr = macho_search_data(&kext->macho, data, size, minprot, &static_address);
	if (mr != MACHO_SUCCESS) {
		if (mr == MACHO_NOT_FOUND) {
			return KEXT_NOT_FOUND;
		}
		assert(mr == MACHO_ERROR);
		return KEXT_ERROR;
	}
	*address = static_address + kext->slide;
	return KEXT_SUCCESS;
}

/*
 * struct kext_for_each_kernelcache_context
 *
 * Description:
 * 	Callback context for kext_for_each.
 */
struct kext_for_each_kernelcache_context {
	kext_for_each_callback_fn callback;
	void *context;
};

/*
 * kext_for_each_kernelcache_callback
 *
 * Description:
 * 	Callback for kext_for_each. This callback only exists in order to add the kernel_slide to
 * 	the base parameter.
 */


kext_result
kext_id_find_symbol(const char *bundle_id, const char *symbol, kaddr_t *addr, size_t *size) {
	const struct kext *kext;
	kext_result kr = kernel_kext(&kext, bundle_id);
	if (kr == KEXT_SUCCESS) {
		kr = kext_find_symbol(kext, symbol, addr, size);
		kext_release(kext);
	}
	return kr;
}

/*
 * struct kernel_and_kexts_find_symbol_context
 *
 * Description:
 * 	Callback context for kernel_and_kexts_find_symbol.
 */
struct kernel_and_kexts_find_symbol_context {
	const char *symbol;
	kaddr_t addr;
	size_t size;
};

/*
 * kernel_and_kexts_find_symbol_callback
 *
 * Description:
 * 	A callback for kernel_and_kexts_find_symbol that tries to find a symbol in the given
 * 	kernel extension.
 */
static bool
kernel_and_kexts_find_symbol_callback(void *context, CFDictionaryRef info,
		const char *bundle_id, kaddr_t base0, size_t size0) {
	if (bundle_id == NULL) {
		return false;
	}
	struct kernel_and_kexts_find_symbol_context *c = context;
	error_stop();
	kext_result kr = kext_id_find_symbol(bundle_id, c->symbol, &c->addr, &c->size);
	error_start();
	return (kr == KEXT_SUCCESS);
}

kext_result
kernel_and_kexts_find_symbol(const char *symbol, kaddr_t *addr, size_t *size) {
	struct kernel_and_kexts_find_symbol_context context = { symbol };
	bool success = kext_for_each(kernel_and_kexts_find_symbol_callback, &context);
	if (!success) {
		return KEXT_ERROR;
	}
	if (context.addr == 0 && context.size == 0) {
		return KEXT_NOT_FOUND;
	}
	*addr = context.addr;
	if (size != NULL) {
		*size = context.size;
	}
	return KEXT_SUCCESS;
}

/*
 * struct kernel_and_kexts_search_data_context
 *
 * Description:
 * 	Callback context for kernel_and_kexts_search_data.
 */
struct kernel_and_kexts_search_data_context {
	const void *data;
	size_t size;
	int minprot;
	kaddr_t addr;
};

/*
 * kernel_and_kexts_search_data_callback
 *
 * Description:
 * 	A callback for kernel_and_kexts_search_data that tries to find the data in the given
 * 	kernel extension.
 */
static bool
kernel_and_kexts_search_data_callback(void *context, CFDictionaryRef info, const char *bundle_id,
		kaddr_t base0, size_t size0) {
	if (bundle_id == NULL) {
		return false;
	}
	struct kernel_and_kexts_search_data_context *c = context;
	const struct kext *kext;
	bool found = false;
	error_stop();
	kext_result kr = kernel_kext(&kext, bundle_id);
	if (kr != KEXT_SUCCESS) {
		goto fail;
	}
	kr = kext_search_data(kext, c->data, c->size, c->minprot, &c->addr);
	kext_release(kext);
	found = (kr == KEXT_SUCCESS);
fail:
	error_start();
	return found;

}

kext_result
kernel_and_kexts_search_data(const void *data, size_t size, int minprot, kaddr_t *addr) {
	struct kernel_and_kexts_search_data_context context = { data, size, minprot };
	bool success = kext_for_each(kernel_and_kexts_search_data_callback, &context);
	if (!success) {
		return KEXT_ERROR;
	}
	if (context.addr == 0) {
		return KEXT_NOT_FOUND;
	}
	*addr = context.addr;
	return KEXT_SUCCESS;
}

kext_result
resolve_symbol(const char *bundle_id, const char *symbol, kaddr_t *addr, size_t *size) {
	if (bundle_id == NULL) {
		return kernel_and_kexts_find_symbol(symbol, addr, size);
	} else {
		return kext_id_find_symbol(bundle_id, symbol, addr, size);
	}
}
