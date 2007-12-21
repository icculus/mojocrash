/**
 * MojoCrash; a problem-reporting tool.
 *
 * Documentation is in mojocrash.h. It's verbose, honest.  :)
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_internal.h"

int MOJOCRASH_StringLength(const char *str)
{
    int retval = 0;
    while (*(str++))
        retval++;
    return retval;
} /* MOJOCRASH_StringLength */


void MOJOCRASH_StringCopy(char *dst, const char *src)
{
    while (1)
    {
        const char ch = *(src++);
        *(dst++) = ch;
        if (ch == '\0')
            break;
    } /* while */
} /* MOJOCRASH_StringCopy */



static int installed = 0;
static MOJOCRASH_hooks hooks;
static char scratch[256];
static char appname[MOJOCRASH_MAX_APPNAME_STRING];
static char version[MOJOCRASH_MAX_VERSION_STRING];
static char url[MOJOCRASH_MAX_URL_STRING];


static int callstack_callback(void *addr)
{
    /* !!! FIXME: c runtime dependency */
    snprintf(scratch, sizeof (scratch), "STACK %p", addr);
    return hooks.new_crashlog_line(scratch);
} /* callstack_callback */

static int objects_callback(const char *fname, void *addr, unsigned long len)
{
    snprintf(scratch, sizeof (scratch), "OBJECT %s/%p/%lu", fname, addr, len);
    return hooks.new_crashlog_line(scratch);
} /* objects_callback */

static int etc_callback(const char *key, const char *value)
{
    snprintf(scratch, sizeof (scratch), "ETCKEY %s", key);
    if (!hooks.new_crashlog_line(scratch))
        return 0;
    snprintf(scratch, sizeof (scratch), "ETCVALUE %s", value); /* !!! FIXME: escape newlines? */
    if (!hooks.new_crashlog_line(scratch))
        return 0;
    return 1;
} /* etc_callback */


static int get_basics(int sig)
{
    snprintf(scratch, sizeof (scratch),
             "MOJOCRASH %d.%d.%d", MOJOCRASH_VERSION_MAJOR,
             MOJOCRASH_VERSION_MINOR, MOJOCRASH_VERSION_PATCH);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    snprintf(scratch, sizeof (scratch),
             "CRASHLOG_VERSION %d",
             MOJOCRASH_LOG_FORMAT_VERSION);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    snprintf(scratch, sizeof (scratch), "APPLICATION_NAME %s", appname);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    snprintf(scratch, sizeof (scratch), "APPLICATION_VERSION %s", version);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    if (!hooks.new_crashlog_line("PLATFORM_NAME " MOJOCRASH_PLATFORM_NAME))
        return 0;

    if (!hooks.new_crashlog_line("CPUARCH_NAME " MOJOCRASH_PLATFORM_CPUARCH))
        return 0;

    snprintf(scratch, sizeof (scratch), "PLATFORM_VERSION %s",
             MOJOCRASH_platform_version());
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    snprintf(scratch, sizeof (scratch), "CRASH_SIGNAL %d", sig);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    snprintf(scratch, sizeof (scratch), "APPLICATION_UPTIME %ld",
             MOJOCRASH_platform_appuptime());
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    snprintf(scratch, sizeof (scratch), "CRASH_TIME %ld",
             MOJOCRASH_platform_now());
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    return 1;
} /* get_basics */

static int crash_catcher_internal(int sig, int crash_count)
{
    if (!hooks.preflight(sig, crash_count)) return 0;
    if (!hooks.start_crashlog()) return 0;
    if (!get_basics(sig)) return 0;
    if (!hooks.new_crashlog_line("")) return 0;
    if (!hooks.get_objects(objects_callback)) return 0;
    if (!hooks.new_crashlog_line("")) return 0;
    if (!hooks.get_callstack(callstack_callback)) return 0;
    if (!hooks.new_crashlog_line("")) return 0;
    if (!hooks.get_etc(etc_callback)) return 0;
    if (!hooks.new_crashlog_line("")) return 0;
    if (!hooks.end_crashlog()) return 0;
    if (!hooks.postflight()) return 0;
    return 1;
} /* crash_catcher_internal */


/*
 * This is the actual crash catcher entry point that should be invoked from
 *  from a signal handler or whatnot.
 */
static void crash_catcher(int sig)
{
    static int crash_count = 0;
    crash_count++;
    crash_catcher_internal(sig, crash_count);
    hooks.die();  /* may not return. */
    crash_count = 0;  /* in case we did... */
} /* crash_catcher */



/* The default hooks... */

static int defhook_install_crash_catcher(MOJOCRASH_catcher catcher)
{
    return MOJOCRASH_platform_install_crash_catcher(catcher);
} /* defhook_install_crash_catcher */

static int defhook_preflight(int sig, int crash_count)
{
    if (crash_count == 1)
        return 1;  /* first crash, go forward. */

    else if (crash_count == 2)
        return 0;  /* try to fail. */

    else if (crash_count == 3)
        hooks.die();  /* try to die. */

    else if (crash_count == 4)
        MOJOCRASH_platform_die(0);  /* Really try to die. */

    else if (crash_count == 5)
        MOJOCRASH_platform_die(1);  /* REALLY try to die! */

    return 0;  /* oh well. */
} /* defhook_preflight */

static int defhook_start_crashlog(void)
{
    return MOJOCRASH_platform_start_crashlog();
} /* defhook_start_crashlog */

static int defhook_new_crashlog_line(const char *str)
{
    return MOJOCRASH_platform_new_crashlog_line(str);
} /* defhook_new_crashlog_line */

static int defhook_get_objects(MOJOCRASH_get_objects_callback callback)
{
    return MOJOCRASH_platform_get_objects(callback);
} /* defhook_get_objects */

static int defhook_get_callstack(MOJOCRASH_get_callstack_callback callback)
{
    return MOJOCRASH_platform_get_callstack(callback);
} /* defhook_get_callstack */

static int defhook_get_etc(MOJOCRASH_get_etc_callback callback)
{
    return 1;  /* nothing to report here, just go on. */
} /* defhook_get_etc */

static int defhook_end_crashlog(void)
{
    return MOJOCRASH_platform_end_crashlog();
} /* defhook_end_crashlog */

static int defhook_postflight(void)
{
    return 1;  /* tell normal processing to go forward. */
} /* defhook_postflight */

static void defhook_die(void)
{
    MOJOCRASH_platform_die(0);
} /* defhook_die */

static void init_hooks(const MOJOCRASH_hooks *_hooks)
{
    #define INIT_HOOK(H) hooks.H = _hooks->H ? _hooks->H : defhook_##H;
    INIT_HOOK(install_crash_catcher);
    INIT_HOOK(preflight);
    INIT_HOOK(start_crashlog);
    INIT_HOOK(new_crashlog_line);
    INIT_HOOK(get_objects);
    INIT_HOOK(get_callstack);
    INIT_HOOK(get_etc);
    INIT_HOOK(end_crashlog);
    INIT_HOOK(postflight);
    INIT_HOOK(die);
    #undef INIT_HOOK
} /* init_hooks */


/* Entry point(s) ... */

int MOJOCRASH_install(const char *_appname, const char *_version,
                      const char *_url, const MOJOCRASH_hooks *_hooks)
{
    if (installed)
        return 0;  /* don't double-call this! */

    /* sanity-check parameters. */
    if (_appname == NULL) return 0;
    if (_version == NULL) return 0;
    if (_url == NULL) return 0;
    if (_hooks == NULL) return 0;
    if (strlen(_appname) >= MOJOCRASH_MAX_APPNAME_STRING) return 0;
    if (strlen(_version) >= MOJOCRASH_MAX_VERSION_STRING) return 0;
    if (strlen(_url) >= MOJOCRASH_MAX_URL_STRING) return 0;

    /* set up state. */
    strcpy(appname, _appname);
    strcpy(version, _version);
    strcpy(url, _url);
    init_hooks(_hooks);

    if (!MOJOCRASH_platform_init())
        return 0;

    if (!hooks.install_crash_catcher(crash_catcher))
        return 0;

    installed = 1;
    return 1;  /* success! */
} /* MOJOCRASH_install */

/* end of mojocrash.c ... */


