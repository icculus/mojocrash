#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_platform.h"

#if MOJOCRASH_PLATFORM_UNIX

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "mojocrash_internal.h"

static char logpath[PATH_MAX+1];
static char osversion[64];
static int crashlogfd = -1;
static struct timeval starttime;

typedef void (*MOJOCRASH_sighandler)(int sig);
static MOJOCRASH_sighandler orig_SIGSEGV_handler = NULL;
static MOJOCRASH_sighandler orig_SIGBUS_handler = NULL;
static MOJOCRASH_sighandler orig_SIGFPE_handler = NULL;
static MOJOCRASH_sighandler orig_SIGILL_handler = NULL;
static MOJOCRASH_sighandler orig_SIGABRT_handler = NULL;

int MOJOCRASH_platform_install_crash_catcher(void (*catcher)(int sig))
{
    #define INSTALL_SIGHANDLER(x) orig_##x##_handler = signal(x, catcher)
    INSTALL_SIGHANDLER(SIGSEGV);
    INSTALL_SIGHANDLER(SIGBUS);
    INSTALL_SIGHANDLER(SIGFPE);
    INSTALL_SIGHANDLER(SIGILL);
    INSTALL_SIGHANDLER(SIGABRT);
    #undef INSTALL_SIGHANDLER
    return 1;
} /* MOJOCRASH_platform_install_crash_catcher */


void MOJOCRASH_platform_die(int force)
{
    if (force)
        _exit(86);
    else
        exit(86);
} /* MOJOCRASH_platform_die */


int MOJOCRASH_platform_start_crashlog(const char *appname)
{
    char *path1 = logpath + strlen(logpath);
    char *path2 = path1 + 1 + strlen(appname);
    int num = 0;

    strcpy(path1 + 1, appname);

    /*
     * if crashlog isn't -1, we might be in a double-fault, but it might be
     *  that the crashing program wrote over the static variable, too...
     *  close it (ignore failure) and start again. If the file was really
     *  there, it was useless in the double-fault anyhow.
     */
    if (crashlogfd != -1)
    {
        close(crashlogfd);
        crashlogfd = -1;
    } /* if */

    /*
     * If there are 1000+ crashlogs, either they aren't being reported, or
     *  we have a problem here that is causing an infinite loop. Either way,
     *  give up at that point.
     */
    while ((crashlogfd == -1) && (num < 1000))
    {
        /*
         * Dir won't exist before first crash, and the reporter app may
         *  remove the storage dir at any time as it cleans up after emptying
         *  out old reports, so try to (re)create it on each iteration.
         */
        *path1 = *path2 = '\0';
        mkdir(logpath, 0700);
        *path1 = '/';
        mkdir(logpath, 0700);
        *path2 = '/';
        snprintf(path2 + 1, sizeof (logpath) - (path2-logpath), "%d", num);
        crashlogfd = open(logpath, O_WRONLY | O_CREAT | O_EXCL, 0600);
        num++;
    } /* while */

    return (crashlogfd != -1);
} /* MOJOCRASH_platform_start_crashlog */


int MOJOCRASH_platform_new_crashlog_line(const char *str)
{
    const int len = strlen(str);
    if (write(crashlogfd, str, len) != len)
        return 0;
    if (write(crashlogfd, "\n", 1) != 1)
        return 0;
    return 1;
} /* MOJOCRASH_platform_new_crashlog_line */


int MOJOCRASH_platform_end_crashlog(void)
{
    if (close(crashlogfd) == -1)
        return 0;
    crashlogfd = -1;
    return 1;
} /* MOJOCRASH_platform_end_crashlog */


const char *MOJOCRASH_platform_version(void)
{
    return osversion;
} /* MOJOCRASH_platform_version */


long MOJOCRASH_platform_appuptime(void)
{
    struct timeval tv;
    long retval = 0;
    gettimeofday(&tv, NULL);
    retval = ( (tv.tv_sec - starttime.tv_sec) +
               ((tv.tv_usec - starttime.tv_usec) / 1000000) );
    return ((retval >= 0) ? retval : -1);
} /* MOJOCRASH_platform_appuptime */


long MOJOCRASH_platform_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
} /* MOJOCRASH_platform_now */


int MOJOCRASH_unix_init(const char *_logpath, const char *_osversion)
{
    int len = 0;

    gettimeofday(&starttime, NULL);

    len = snprintf(osversion, sizeof (osversion), "%s", _osversion);
    if (len >= sizeof (osversion))
        return 0;

    len = snprintf(logpath, sizeof (logpath), "%s", _logpath);
    if (len >= sizeof (logpath) - 16)
        return 0;

    return 1;
} /* MOJOCRASH_platform_init */


#endif /* MOJOCRASH_PLATFORM_UNIX */

