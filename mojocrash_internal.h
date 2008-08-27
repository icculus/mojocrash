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

#ifdef strchr
#undef strchr
#endif
#define strchr(x,y) MOJOCRASH_StringChar(x,y)
char *MOJOCRASH_StringChar(const char *str, const char ch);

#ifdef strncmp
#undef strncmp
#endif
#define strncmp(x,y,z) MOJOCRASH_StringNCompare(x,y,z)
int MOJOCRASH_StringNCompare(const char *a, const char *b, const int n);

#ifdef strcmp
#undef strcmp
#endif
#define strcmp(x,y) MOJOCRASH_StringCompare(x,y)
int MOJOCRASH_StringCompare(const char *a, const char *b);

/* this is NOT a drop in replacement for strcat. */
void MOJOCRASH_StringAppend(char **_dst, int *avail, const char *src);
void MOJOCRASH_StringAppendMojoCrashVersion(char **dst, int *avail);

long MOJOCRASH_StringToLong(const char *str);
char *MOJOCRASH_ULongToString(unsigned long num, char *str);
char *MOJOCRASH_LongToString(long num, char *str);
char *MOJOCRASH_PtrToString(const void *ptr, char *str);

/* naturally, GCC and MSVC do this differently... */
#if (defined __GNUC__)
#define CONST_SI64(x) x##LL
#define CONST_UI64(x) x##ULL
#elif (defined _MSC_VER)
#define CONST_SI64(x) x##i64
#define CONST_UI64(x) x##ui64
#else
#error Please define your platform.
#endif


/* helper macro for counting items in a static array. */
#define STATICARRAYLEN(x) (sizeof (x) / sizeof ((x)[0]))

/* These are only available on Unix systems, as glue between two sources. */
int MOJOCRASH_unix_init(void);
void MOJOCRASH_unix_get_logpath(char *buf, size_t buflen, const char *appname);
void MOJOCRASH_unix_get_osver(char *buf, size_t buflen);

/*
 * These are all functions that are platform-specific. Usually they need
 *  unportable system APIs, or they handle things in unique ways, or both.
 */

int MOJOCRASH_platform_init(void);
int MOJOCRASH_platform_install_crash_catcher(MOJOCRASH_catcher catcher);
int MOJOCRASH_platform_get_callstack(MOJOCRASH_get_callstack_callback cb);
int MOJOCRASH_platform_get_objects(MOJOCRASH_get_objects_callback cb);
int MOJOCRASH_platform_start_crashlog(const char *appname);
int MOJOCRASH_platform_new_crashlog_line(const char *str);
int MOJOCRASH_platform_end_crashlog(void);
void MOJOCRASH_platform_die(int force);
const char *MOJOCRASH_platform_version(void);
long MOJOCRASH_platform_appuptime(void);
long MOJOCRASH_platform_now(void);
const char **MOJOCRASH_platform_load_reports(const char *appname, int *total);
void MOJOCRASH_platform_delete_report(const char *appname, const int idx);
void MOJOCRASH_platform_free_reports(const char **reports, const int total);
void *MOJOCRASH_platform_begin_dns(const char *host, const int port, const int blocking);
int MOJOCRASH_platform_check_dns(void *dns);
void MOJOCRASH_platform_free_dns(void *dns);
void *MOJOCRASH_platform_open_socket(void *dns, const int blocking);
int MOJOCRASH_platform_check_socket(void *sock);
void MOJOCRASH_platform_close_socket(void *sock);
int MOJOCRASH_platform_read_socket(void *sock, char *buf, const int l);
int MOJOCRASH_platform_write_socket(void *sock, const char *buf, const int l);
int MOJOCRASH_platform_get_http_proxy(char *buf, const int len);
int MOJOCRASH_platform_spin_thread(void (*fn)(void *), void *arg);

#ifdef __cplusplus
}
#endif

#endif  /* include-once blocker. */

/* end of mojocrash_internal.h ... */

