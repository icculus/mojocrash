/**
 * MojoCrash; a problem-reporting tool.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCLUDE_MOJOCRASH_H_
#define _INCLUDE_MOJOCRASH_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MOJOCRASH_VER_MAJOR 0
#define MOJOCRASH_VER_MINOR 0
#define MOJOCRASH_VER_PATCH 1


#define MOJOCRASH_MAX_APPNAME_STRING 64
#define MOJOCRASH_MAX_VERSION_STRING 32
#define MOJOCRASH_MAX_URL_STRING 128


typedef void (*MOJOCRASH_catcher)(int sig);
typedef int (*MOJOCRASH_get_callstack_callback)(void *addr);
typedef int (*MOJOCRASH_get_objects_callback)(const char *fname, void *addr,
                                              unsigned long len);
typedef int (*MOJOCRASH_get_etc_callback)(const char *key, const char *value);

typedef struct MOJOCRASH_hooks
{
    int (*install_crash_catcher)(MOJOCRASH_catcher catcher);
    int (*preflight)(int sig, int crash_count);
    int (*start_crashlog)(void);
    int (*new_crashlog_line)(const char *str);
    int (*get_callstack)(MOJOCRASH_get_callstack_callback callback);
    int (*get_objects)(MOJOCRASH_get_objects_callback callback);
    int (*get_etc)(MOJOCRASH_get_etc_callback callback);
    int (*end_crashlog)(void);
    int (*postflight)(void);
    void (*die)(void);
} MOJOCRASH_hooks;


int MOJOCRASH_install(const char *appname, const char *version,
                      const char *url, const MOJOCRASH_hooks *hooks);

#ifdef __cplusplus
}
#endif

#endif /* include-once blocker */

/* end of mojocrash.h ... */

