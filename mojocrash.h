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

#define MOJOCRASH_VERSION_MAJOR 0
#define MOJOCRASH_VERSION_MINOR 0
#define MOJOCRASH_VERSION_PATCH 1

#define MOJOCRASH_LOG_FORMAT_VERSION 1
#define MOJOCRASH_MAX_APPNAME_STRING 64
#define MOJOCRASH_MAX_VERSION_STRING 32


typedef void (*MOJOCRASH_catcher)(int sig);
typedef int (*MOJOCRASH_get_callstack_callback)(const void *addr);
typedef int (*MOJOCRASH_get_objects_callback)(const char *fname,
                                          const void *addr, unsigned long len);
typedef int (*MOJOCRASH_get_etc_callback)(const char *key, const char *value);

typedef struct MOJOCRASH_hooks
{
    int (*install_crash_catcher)(MOJOCRASH_catcher catcher);
    int (*preflight)(int sig, int crash_count);
    int (*start_crashlog)(const char *appname);
    int (*new_crashlog_line)(const char *str);
    int (*get_callstack)(MOJOCRASH_get_callstack_callback callback);
    int (*get_objects)(MOJOCRASH_get_objects_callback callback);
    int (*get_etc)(MOJOCRASH_get_etc_callback callback);
    int (*end_crashlog)(void);
    int (*postflight)(void);
    void (*die)(void);
} MOJOCRASH_hooks;


int MOJOCRASH_install(const char *appname, const char *version,
                      const MOJOCRASH_hooks *hooks);


typedef enum
{
    MOJOCRASH_GUISHOW_IGNORE,
    MOJOCRASH_GUISHOW_REJECT,
    MOJOCRASH_GUISHOW_SEND,
    MOJOCRASH_GUISHOW_SEND_BACKGROUND,
} MOJOCRASH_GuiShowValue;

typedef struct MOJOCRASH_report_hooks
{
    const char **(*load_reports)(const char *appname, int *total);
    void (*delete_report)(const char *appname, const int idx);
    void (*free_reports)(const char **reports, const int total);
    int (*gui_init)(void);
    MOJOCRASH_GuiShowValue (*gui_show)(const char **reports, const int total);
    int (*gui_status)(const char *statustext, int percent);
    void (*gui_quit)(const int success, const char *statustext);
} MOJOCRASH_report_hooks;

void MOJOCRASH_report(const char *appname, const char *url,
                      const MOJOCRASH_report_hooks *h);

int MOJOCRASH_reporting(void);

#ifdef __cplusplus
}
#endif

#endif /* include-once blocker */

/* end of mojocrash.h ... */

