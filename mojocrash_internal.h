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
#include "mojocrash_platform.h"

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

char *MOJOCRASH_LongToString(long num, char *str);
char *MOJOCRASH_PtrToString(const void *ptr, char *str);

/* helper macro for counting items in a static array. */
#define STATICARRAYLEN(x) (sizeof (x) / sizeof ((x)[0]))

/* obviously only available on Unix. Call it from your platform_init()... */
int MOJOCRASH_unix_init(const char *_logpath, const char *_osversion);


/*
 * These are all functions that are platform-specific. Usually they need
 *  unportable system APIs, or they handle things in unique ways, or both.
 */

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

