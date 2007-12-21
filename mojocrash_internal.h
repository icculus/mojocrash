/**
 * MojoCrash; a problem-reporting tool.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* This file is for internal use only! Do not include from your app! */

#ifndef _INCLUDE_MOJOCRASH_INTERNAL_H_
#define _INCLUDE_MOJOCRASH_INTERNAL_H_

#if !__MOJOCRASH_INTERNAL__
#error Please do not include this file from your program.
#endif

/* This is mostly so NULL is defined. */
#include <stdio.h>

#include "mojocrash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reimplementations to avoid dependency on C runtime... */
#ifdef strlen
#undef strlen
#endif
#define strlen(x) MOJOCRASH_StringLength(x)
int MOJOCRASH_StringLength(const char *str);

#ifdef strcpy
#undef strcpy
#endif
#define strcpy(x,y) MOJOCRASH_StringCopy(x,y)
void MOJOCRASH_StringCopy(char *dst, const char *str);

/*
 * These are all functions that are platform-specific. Usually they need
 *  unportable system APIs, or they handle things in unique ways, or both.
 */

#if (defined(__APPLE__) && (__MACH__))
#define MOJOCRASH_PLATFORM_NAME "macosx"
#define MOJOCRASH_PLATFORM_MACOSX 1
#define MOJOCRASH_PLATFORM_UNIX 1
#elif __BEOS__
#define MOJOCRASH_PLATFORM_NAME "beos"
#define MOJOCRASH_PLATFORM_BEOS 1
#define MOJOCRASH_PLATFORM_UNIX 1
#elif __linux__
#define MOJOCRASH_PLATFORM_NAME "linux"
#define MOJOCRASH_PLATFORM_LINUX 1
#define MOJOCRASH_PLATFORM_UNIX 1
#elif WINDOWS
#define MOJOCRASH_PLATFORM_NAME "windows"
#define MOJOCRASH_PLATFORM_WINDOWS 1
#else
#error Unknown operating system.
#endif

#if defined(__powerpc64__)
#define MOJOCRASH_PLATFORM_64BIT 1
#define MOJOCRASH_PLATFORM_CPUARCH "powerpc64"
#elif defined(__ppc__) || defined(__powerpc__) || defined(__POWERPC__)
#define MOJOCRASH_PLATFORM_32BIT 1
#define MOJOCRASH_PLATFORM_CPUARCH "powerpc"
#elif defined(__x86_64__) || defined(_M_X64)
#define MOJOCRASH_PLATFORM_64BIT 1
#define MOJOCRASH_PLATFORM_CPUARCH "x86-64"
#elif defined(__X86__) || defined(__i386__) || defined(i386) || defined (_M_IX86) || defined(__386__)
#define MOJOCRASH_PLATFORM_32BIT 1
#define MOJOCRASH_PLATFORM_CPUARCH "x86"
#else
#error Unknown processor architecture.
#endif

int MOJOCRASH_platform_init(void);
int MOJOCRASH_platform_install_crash_catcher(MOJOCRASH_catcher catcher);
int MOJOCRASH_platform_get_callstack(MOJOCRASH_get_callstack_callback cb);
int MOJOCRASH_platform_get_objects(MOJOCRASH_get_objects_callback cb);
int MOJOCRASH_platform_start_crashlog(void);
int MOJOCRASH_platform_new_crashlog_line(const char *str);
int MOJOCRASH_platform_end_crashlog(void);
void MOJOCRASH_platform_die(int force);
const char *MOJOCRASH_platform_version(void);
long MOJOCRASH_platform_appuptime(void);
long MOJOCRASH_platform_now(void);

#ifdef __cplusplus
}
#endif

#endif  /* include-once blocker. */

/* end of mojocrash_internal.h ... */

