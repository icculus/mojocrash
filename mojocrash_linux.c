#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_platform.h"

#if MOJOCRASH_PLATFORM_LINUX

#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* this largely relies on Linux/ELF/glibc specific APIs. */
#define _GNU_SOURCE
#define __USE_GNU
#include <link.h>
#include <execinfo.h>

#include "mojocrash_internal.h"

static char exename[PATH_MAX+1];

int MOJOCRASH_platform_get_callstack(MOJOCRASH_get_callstack_callback cb)
{
    /* !!! FIXME: would be nice to have a backtrace_callback() in glibc. */
    /* We're memory-hungry, in hopes of avoiding malloc() during crash. */
    static void *callstack[1024];
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


int MOJOCRASH_platform_init(void)
{
    char logpath[PATH_MAX+1];
    const char *homedir = NULL;
    ssize_t rc = 0;

    rc = readlink("/proc/self/exe", exename, sizeof (exename) - 1);
    if (rc == -1)
        return 0;
    exename[rc] = '\0';

    homedir = getenv("HOME");
    if (homedir == NULL)
        homedir = ".";  /* !!! FIXME: read /etc/passwd? */

    len = snprintf(logpath, sizeof (logpath), "%s/.mojocrash_logs", homedir);
    if (len >= sizeof (logpath) - 16)
        return 0;

    return MOJOCRASH_unix_init(logpath);
} /* MOJOCRASH_platform_init */

#endif /* MOJOCRASH_PLATFORM_LINUX */

