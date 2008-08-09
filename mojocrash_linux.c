#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_platform.h"

#if MOJOCRASH_PLATFORM_LINUX

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/utsname.h>

/* this largely relies on Linux/ELF/glibc specific APIs. */
#define _GNU_SOURCE
#define __USE_GNU
#include <link.h>
#include <execinfo.h>

#include "mojocrash_internal.h"

#define STATICARRAYLEN(x) (sizeof (x) / sizeof ((x)[0]))

static char logpath[PATH_MAX+1];
static char exename[PATH_MAX+1];
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


int MOJOCRASH_platform_get_callstack(MOJOCRASH_get_callstack_callback cb)
{
    #if MOJOCRASH_PLATFORM_POWERPC || MOJOCRASH_PLATFORM_POWERPC_64
        #define MOJOCRASH_USE_GLIBC_BACKTRACE 1  /* !!! FIXME */
    #elif MOJOCRASH_PLATFORM_X86 || MOJOCRASH_PLATFORM_X86_64
        #define MOJOCRASH_USE_GLIBC_BACKTRACE 1  /* !!! FIXME */
    #else
        #error No backtrace support for this CPU arch.
        #error  ...Maybe you should define MOJOCRASH_USE_GLIBC_BACKTRACE...
    #endif

    #if MOJOCRASH_USE_GLIBC_BACKTRACE
        /* We're memory-hungry, in hopes of avoiding malloc() during crash. */
        static void *callstack[1024];

        /* !!! FIXME: would be nice to have a backtrace_callback() in glibc. */
        int frames = backtrace(callstack, STATICARRAYLEN(callstack));
        int i;

        for (i = 0; i < frames; i++)
        {
            if (!cb(callstack[i]))
                return 0;
        } /* for */
    #endif

    return 1;
} /* MOJOCRASH_platform_get_callstack */


static inline const char *fname_without_dirs(const char *path)
{
    const char *retval = strrchr(path);
    return ((retval) ? (retval+1) : path);
} /* fname_without_dirs */


static inline const char *dso_path(const char *dso)
{
    return ( ((dso == NULL) || (*dso == '\0')) ? exename : dso );
} /* dso_path */


/* glibc uses this callback as it iterates DSOs in the address space. */
static int glibc_dl_iterate(struct dl_phdr_info *info, size_t size, void *data)
{
    MOJOCRASH_get_objects_callback cb = (MOJOCRASH_get_objects_callback) data;
    const char *name = fname_without_dirs(dso_path(info->dlpi_name));
    int i;

    for (i = 0; i < info->dlpi_phnum; i++)
    {
        /* only list segments that can contain executable code. */
        if ((info->dlpi_phdr[i].p_flags & PF_X) == 0)
            continue;

        /* PT_PHDR segments are flagged executable for some reason. Skip! */
        else if (info->dlpi_phdr[i].p_type == PT_PHDR)
            continue;

        /* okay, add this one. */
        else
        {
            void *addr = (void *) (info->dlpi_addr+info->dlpi_phdr[i].p_vaddr);
            unsigned long len = info->dlpi_phdr[i].p_memsz;
            if (!cb(name, addr, len))
                return 1;  /* stop! */
        } /* else */
    } /* for */

    return 0;  /* Try getting more results if possible. Callback may refire. */
} /* glibc_dl_iterate */


int MOJOCRASH_platform_get_objects(MOJOCRASH_get_objects_callback cb)
{
    return (dl_iterate_phdr(glibc_dl_iterate, cb) == 0);
} /* MOJOCRASH_platform_get_objects */


void MOJOCRASH_platform_die(int force)
{
    if (force)
        _exit(86);
    else
        exit(86);
} /* MOJOCRASH_platform_die */


int MOJOCRASH_platform_start_crashlog(void)
{
    const char *MOJOCRASH_appname = "MyAppName";  /* !!! FIXME: change this. */
    char *path1 = logpath + strlen(logpath);
    char *path2 = path1 + 1 + strlen(MOJOCRASH_appname);
    int num = 0;

    strcpy(path1 + 1, MOJOCRASH_appname);

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


int MOJOCRASH_platform_init(void)
{
    const char *homedir = NULL;
    struct utsname un;
    int len = 0;
    ssize_t rc = 0;

    gettimeofday(&starttime, NULL);

    if (uname(&un) == 0)
        snprintf(osversion, sizeof (osversion), "%s", un.release);
    else
        strcpy(osversion, "???");

    rc = readlink("/proc/self/exe", exename, sizeof (exename) - 1);
    if (rc == -1)
        return 0;
    exename[rc] = '\0';

    homedir = getenv("HOME");
    if (homedir == NULL)
        homedir = ".";  /* !!! FIXME */

    len = snprintf(logpath, sizeof (logpath), "%s/.mojocrash_logs", homedir);
    if (len >= sizeof (logpath) - 16)
        return 0;
    return 1;
} /* MOJOCRASH_platform_init */

#endif /* MOJOCRASH_PLATFORM_LINUX */

