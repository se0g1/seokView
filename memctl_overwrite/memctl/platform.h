#ifndef MEMCTL__PLATFORM_H_
#define MEMCTL__PLATFORM_H_

#include <mach/machine.h>
#include <stdlib.h>

/*
 * macro PLATFORM_XNU_VERSION_GE
 *
 * Description:
 * 	A helper macro to test whether the current XNU platform release version is at least the
 * 	specified version number.
 */
#define PLATFORM_XNU_VERSION_GE(_major, _minor, _patch)		\
	(platform.release.major > _major			\
	 || (platform.release.major == _major &&		\
	     (platform.release.minor > _minor			\
	      || (platform.release.minor == _minor &&		\
	          platform.release.patch >= _patch))))

/*
 * macro PLATFORM_XNU_VERSION_LT
 *
 * Description:
 * 	A helper macro to test whether the current XNU platform release version is less than the
 * 	specified version number.
 */
#define PLATFORM_XNU_VERSION_LT(_major, _minor, _patch)		\
	(!PLATFORM_XNU_VERSION_GE(_major, _minor, _patch))

#endif
