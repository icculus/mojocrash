#ifndef _INCLUDE_MOJOCRASH_PLATFORM_H_
#define _INCLUDE_MOJOCRASH_PLATFORM_H_

#if !__MOJOCRASH_INTERNAL__
#error Please do not include this file from your program.
#endif

#if (defined(__APPLE__) && (__MACH__))
#define MOJOCRASH_PLATFORM_NAME "macosx"
#define MOJOCRASH_PLATFORM_MACOSX 1
#define MOJOCRASH_PLATFORM_UNIX 1
#elif defined(__HAIKU__)
#define MOJOCRASH_PLATFORM_NAME "haiku"
#define MOJOCRASH_PLATFORM_BEOS 1
#define MOJOCRASH_PLATFORM_UNIX 1
#elif defined(__linux__)
#define MOJOCRASH_PLATFORM_NAME "linux"
#define MOJOCRASH_PLATFORM_LINUX 1
#define MOJOCRASH_PLATFORM_UNIX 1
#elif defined(_WIN32) || defined(_WIN64)
#define MOJOCRASH_PLATFORM_NAME "windows"
#define MOJOCRASH_PLATFORM_WINDOWS 1
#else
#error Unknown operating system.
#endif

#if defined(__powerpc64__)
#define MOJOCRASH_PLATFORM_POWERPC_64 1
#define MOJOCRASH_PLATFORM_64BIT 1
#define MOJOCRASH_PLATFORM_CPUARCH "powerpc64"
#elif defined(__ppc__) || defined(__powerpc__) || defined(__POWERPC__)
#define MOJOCRASH_PLATFORM_POWERPC 1
#define MOJOCRASH_PLATFORM_32BIT 1
#define MOJOCRASH_PLATFORM_CPUARCH "powerpc"
#elif defined(__x86_64__) || defined(_M_X64)
#define MOJOCRASH_PLATFORM_X86_64 1
#define MOJOCRASH_PLATFORM_64BIT 1
#define MOJOCRASH_PLATFORM_CPUARCH "x86-64"
#elif defined(__X86__) || defined(__i386__) || defined(i386) || defined (_M_IX86) || defined(__386__)
#define MOJOCRASH_PLATFORM_X86 1
#define MOJOCRASH_PLATFORM_32BIT 1
#define MOJOCRASH_PLATFORM_CPUARCH "x86"
#else
#error Unknown processor architecture.
#endif

#endif /* include-once blocker. */

/* end of mojocrash_platform.h ... */

