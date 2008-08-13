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


char *MOJOCRASH_StringChar(const char *str, const char ch)
{
    while (1)
    {
        const char strch = *str;
        if (strch == ch)
            return (char *) str;
        else if (strch == '\0')
            return NULL;
        else
            str++;
    } /* while */
} /* MOJOCRASH_StringChar */


int MOJOCRASH_StringNCompare(const char *a, const char *b, const int n)
{
    int i;
    for (i = 0; i < n; i++)
    {
        const char ch1 = a[i];
        const char ch2 = b[i];
        if (ch1 < ch2)
            return -1;
        else if (ch1 > ch2)
            return 1;
        else if (ch1 == '\0')
            return 0;

        /* we're equal and neither string is terminated. Go on. */
    } /* for */
        
    return 0;  /* matched to n chars, without a terminator. */
} /* MOJOCRASH_StringNCompare */


int MOJOCRASH_StringCompare(const char *a, const char *b)
{
    while (1)
    {
        const char ch1 = *a;
        const char ch2 = *b;
        if (ch1 < ch2)
            return -1;
        else if (ch1 > ch2)
            return 1;
        else if (ch1 == '\0')
            return 0;

        /* we're equal and neither string is terminated. Go on. */
        a++;
        b++;
    } /* for */

    return -1;  /* shouldn't happen. */
} /* MOJOCRASH_StringCompare */


void MOJOCRASH_StringAppend(char **_dst, const char *src, int *avail)
{
    if (*avail > 0)
    {
        char *dst = *_dst;
        while (*src && (*avail > 1))
        {
            *(dst++) = *(src++);
            (*avail)--;
        } /* while */

        *dst = '\0';
        *_dst = dst;
    } /* if */
} /* MOJOCRASH_StringAppend */


static char *flipstring(char *strstart, char *str)
{
    char *retval = strstart;
    while (str > strstart)
    {
        const char tmp = *str;
        *(str--) = *strstart;
        *(strstart++) = tmp;
    } /* while */

    return retval;
} /* flipstring */


long MOJOCRASH_StringToLong(const char *str)
{
    long retval = 0;
    long mult = 1;
    int i = 0;

    while (*str == ' ')
        str++;

    if (*str == '-')
    {
        mult = -1;
        str++;
    } /* if */

    while (1)
    {
        const char ch = str[i];
        if ((ch < '0') || (ch > '9'))
            break;
        i++;
    } /* for */

    while (--i >= 0)
    {
        const char ch = str[i];
        retval += ((long) (ch - '0')) * mult;
        mult *= 10;
    } /* while */

    return retval;
} /* MOJOCRASH_StringToLong */


char *MOJOCRASH_LongToString(long num, char *str)
{
    char *strstart = str;
    const int negative = (num < 0);

    if (negative)
        num = -num;

    /* write out the string reversed. */
    *(str++) = '\0';   /* write out the string reversed. */
    do
    {
        *(str++) = ('0' + (num % 10));
        num /= 10;
    } while (num);

    if (negative)
        *(str++) = '-';

    return flipstring(strstart, str-1);
} /* MOJOCRASH_LongToString */


char *MOJOCRASH_ULongToString(unsigned long num, char *str)
{
    char *strstart = str;

    /* write out the string reversed. */
    *(str++) = '\0';   /* write out the string reversed. */
    do
    {
        *(str++) = ('0' + (num % 10));
        num /= 10;
    } while (num);

    return flipstring(strstart, str-1);
} /* MOJOCRASH_ULongToString */


char *MOJOCRASH_PtrToString(const void *ptr, char *str)
{
    /* !!! FIXME: "long" isn't right on win64, probably other places. */
    return MOJOCRASH_ULongToString((const unsigned long) ptr, str);
} /* MOJOCRASH_PtrToString */


static int installed = 0;
static MOJOCRASH_hooks hooks;
static char scratch[256];
static char numcvt[32];
static char appname[MOJOCRASH_MAX_APPNAME_STRING];
static char version[MOJOCRASH_MAX_VERSION_STRING];


static int callstack_callback(void *addr)
{
    char *str = scratch;
    int avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "CALLSTACK ", &avail);
    MOJOCRASH_StringAppend(&str, MOJOCRASH_PtrToString(addr, numcvt), &avail);
    return hooks.new_crashlog_line(scratch);
} /* callstack_callback */

static int objects_callback(const char *fname, const void *addr,
                            unsigned long len)
{
    char *str = scratch;
    int avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "OBJECT ", &avail);
    MOJOCRASH_StringAppend(&str, fname, &avail);
    MOJOCRASH_StringAppend(&str, "/", &avail);
    MOJOCRASH_StringAppend(&str, MOJOCRASH_PtrToString(addr, numcvt), &avail);
    MOJOCRASH_StringAppend(&str, "/", &avail);
    MOJOCRASH_StringAppend(&str, MOJOCRASH_ULongToString(len, numcvt), &avail);
    return hooks.new_crashlog_line(scratch);
} /* objects_callback */

static int etc_callback(const char *key, const char *value)
{
    char *str = NULL;
    int avail = 0;

    /* !!! FIXME: escape newlines in keys and values? */

    str = scratch;
    avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "ETC_KEY ", &avail);
    MOJOCRASH_StringAppend(&str, key, &avail);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    str = scratch;
    avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "ETC_VALUE ", &avail);
    MOJOCRASH_StringAppend(&str, value, &avail);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    return 1;
} /* etc_callback */


static int get_basics(int sig)
{
    char *str = NULL;
    int avail = 0;

    #define INTRO(x) if (!hooks.new_crashlog_line(x)) return 0
    INTRO("");
    INTRO("# This is a log of a program crash. It is meant to help the");
    INTRO("#  developers debug the problem that caused the crash.");
    INTRO("#");
    INTRO("# This log was generated by software called \"MojoCrash\" but");
    INTRO("#  please note that MojoCrash did NOT cause the crash in the");
    INTRO("#  first place. It only helped report the problem. Please do not");
    INTRO("#  contact the MojoCrash developers about this problem, we didn't");
    INTRO("#  cause it and probably haven't heard of the crashing program.");
    INTRO("#");
    INTRO("# If you need to contact someone about a bug, please contact the");
    INTRO("#  original vendor of your program instead. Thanks!");
    INTRO("");
    #undef INTRO

    str = scratch;
    avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "MOJOCRASH ", &avail);
    MOJOCRASH_StringAppend(&str,
                MOJOCRASH_ULongToString(MOJOCRASH_VERSION_MAJOR, numcvt),
                &avail);
    MOJOCRASH_StringAppend(&str, ".", &avail);
    MOJOCRASH_StringAppend(&str,
                MOJOCRASH_ULongToString(MOJOCRASH_VERSION_MINOR, numcvt),
                &avail);
    MOJOCRASH_StringAppend(&str, ".", &avail);
    MOJOCRASH_StringAppend(&str,
                MOJOCRASH_ULongToString(MOJOCRASH_VERSION_PATCH, numcvt),
                &avail);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    str = scratch;
    avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "CRASHLOG_VERSION ", &avail);
    MOJOCRASH_StringAppend(&str,
                MOJOCRASH_ULongToString(MOJOCRASH_LOG_FORMAT_VERSION, numcvt),
                &avail);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    str = scratch;
    avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "APPLICATION_NAME ", &avail);
    MOJOCRASH_StringAppend(&str, appname, &avail);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    str = scratch;
    avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "APPLICATION_VERSION ", &avail);
    MOJOCRASH_StringAppend(&str, version, &avail);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    if (!hooks.new_crashlog_line("PLATFORM_NAME " MOJOCRASH_PLATFORM_NAME))
        return 0;

    if (!hooks.new_crashlog_line("CPUARCH_NAME " MOJOCRASH_PLATFORM_CPUARCH))
        return 0;

    str = scratch;
    avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "PLATFORM_VERSION ", &avail);
    MOJOCRASH_StringAppend(&str, MOJOCRASH_platform_version(), &avail);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    str = scratch;
    avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "CRASH_SIGNAL ", &avail);
    MOJOCRASH_StringAppend(&str, MOJOCRASH_LongToString(sig, numcvt), &avail);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    str = scratch;
    avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "APPLICATION_UPTIME ", &avail);
    MOJOCRASH_StringAppend(&str,
                MOJOCRASH_LongToString(MOJOCRASH_platform_appuptime(), numcvt),
                &avail);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    str = scratch;
    avail = sizeof (scratch);
    MOJOCRASH_StringAppend(&str, "CRASH_TIME ", &avail);
    MOJOCRASH_StringAppend(&str,
                MOJOCRASH_LongToString(MOJOCRASH_platform_now(), numcvt),
                &avail);
    if (!hooks.new_crashlog_line(scratch))
        return 0;

    return 1;
} /* get_basics */

static int crash_catcher_internal(int sig, int crash_count)
{
    if (!hooks.preflight(sig, crash_count)) return 0;
    if (!hooks.start_crashlog(appname)) return 0;
    if (!get_basics(sig)) return 0;
    if (!hooks.new_crashlog_line("")) return 0;
    if (!hooks.get_objects(objects_callback)) return 0;
    if (!hooks.new_crashlog_line("")) return 0;
    if (!hooks.get_callstack(callstack_callback)) return 0;
    if (!hooks.new_crashlog_line("")) return 0;
    if (!hooks.get_etc(etc_callback)) return 0;
    if (!hooks.new_crashlog_line("")) return 0;
    if (!hooks.new_crashlog_line("END")) return 0;
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
    crash_count--;  /* in case we did... */
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

static int defhook_start_crashlog(const char *appname)
{
    return MOJOCRASH_platform_start_crashlog(appname);
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

static void init_hooks(const MOJOCRASH_hooks *h)
{
    #define INIT_HOOK(H) hooks.H = ((h && h->H) ? h->H : defhook_##H)
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
                      const MOJOCRASH_hooks *_hooks)
{
    if (installed)
        return 0;  /* don't double-call this! */

    /* sanity-check parameters. */
    if (_appname == NULL) return 0;
    if (_version == NULL) return 0;
    if (strlen(_appname) >= MOJOCRASH_MAX_APPNAME_STRING) return 0;
    if (strlen(_version) >= MOJOCRASH_MAX_VERSION_STRING) return 0;

    /* set up state. */
    strcpy(appname, _appname);
    strcpy(version, _version);
    init_hooks(_hooks);

    if (!MOJOCRASH_platform_init())
        return 0;

    if (!hooks.install_crash_catcher(crash_catcher))
        return 0;

    installed = 1;
    return 1;  /* success! */
} /* MOJOCRASH_install */

/* end of mojocrash.c ... */


