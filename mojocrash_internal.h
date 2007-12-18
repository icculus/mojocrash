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

#ifdef __cplusplus
extern "C" {
#endif

#include "mojocrash.h"

#if !__MOJOCRASH_INTERNAL__
#error Please do not include this file from your program.
#endif

/*
 * These are all functions that are platform-specific. Usually they need
 *  unportable system APIs, or they handle things in unique ways, or both.
 */

int MOJOCRASH_platform_init(void);
int MOJOCRASH_platform_install_crash_catcher(void (*catcher)(int sig));
int MOJOCRASH_platform_get_callstack(MOJOCRASH_get_callstack_callback cb);
int MOJOCRASH_platform_get_objects(MOJOCRASH_get_objects_callback cb);

#ifdef __cplusplus
}
#endif

#endif  /* include-once blocker. */

/* end of mojocrash_internal.h ... */

