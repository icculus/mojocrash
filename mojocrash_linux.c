#ifdef __linux__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

/* this all relies on Linux/ELF/glibc specific APIs. */
#define _GNU_SOURCE
#define __USE_GNU
#include <link.h>
#include <execinfo.h>

#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_internal.h"

/* We're memory-hungry here, in hopes of avoiding malloc() during a crash. */
#define MAX_CALLSTACKS 1024
static void *callstack[MAX_CALLSTACKS];
static char logpath[PATH_MAX+1];
static char exename[PATH_MAX+1];
static int crashlogfd = -1;

#define STATICARRAYLEN(x) (sizeof (x) / sizeof ((x)[0]))

int MOJOCRASH_platform_install_crash_catcher(void (*catcher)(int sig))
{
    signal(SIGSEGV, catcher);
    signal(SIGBUS, catcher);
    signal(SIGFPE, catcher);
    signal(SIGILL, catcher);
    signal(SIGABRT, catcher);
    return 1;
} /* MOJOCRASH_platform_install_crash_catcher */


int MOJOCRASH_platform_get_callstack(MOJOCRASH_get_callstack_callback cb)
{
    /* Ugh, is there a way to iterate this without having to pass an array? */
    int frames = backtrace(callstack, STATICARRAYLEN(callstack));
    int i;

    for (i = 0; i < frames; i++)
    {
        if (!cb(callstack[i]))
            return 0;
    } /* for */

    return 1;
} /* MOJOCRASH_platform_get_callstack */


static inline const char *fname_without_dirs(const char *path)
{
    const char *retval;
    for (retval = path; *path != '\0'; path++)
    {
        if (*path == '/')
            retval = path+1;
    } /* for */
    return retval;
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
    char *path2 = path1 + strlen(MOJOCRASH_appname);
    int num = 0;

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

    while (crashlogfd == -1)
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

    return 1;
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


int MOJOCRASH_platform_init(void)
{
    const char *homedir = getenv("HOME");
    int len = 0;
    ssize_t rc = readlink("/proc/self/exe", exename, sizeof (exename) - 1);
    if (rc == -1)
        return 0;
    exename[rc] = '\0';

    if (homedir == NULL)
        homedir = ".";  /* !!! FIXME */

    len = snprintf(logpath, sizeof (logpath), "%s/.mojocrash_logs", homedir);
    if (len >= sizeof (logpath) - 16)
        return 0;
    return 1;
} /* MOJOCRASH_platform_init */


#if TEST_PLATFORM

static int testobjscb(const char *fname, void *addr, unsigned long len)
{
    printf("DSO: %s/%p/%ld\n", fname, addr, len);
    return 1;
}

static int testcallstackcb(void *addr)
{
    printf("STACK: %p\n", addr);
    return 1;
}

static int z() { return MOJOCRASH_platform_get_callstack(testcallstackcb); }
static int y() { return z(); }
static int x() { return y(); }
static int test_callstack() { return x(); };

int main(int argc, char **argv)
{
    if (!MOJOCRASH_platform_init())
        return 1;
    if (!MOJOCRASH_platform_get_objects(testobjscb))
        printf("MOJOCRASH_platform_get_objects() failed\n");
    if (!test_callstack())
        printf("MOJOCRASH_platform_get_callstack() failed\n");
    return 0;
} /* main */

#endif /* TEST_PLATFORM */

#endif /* __linux__ */

