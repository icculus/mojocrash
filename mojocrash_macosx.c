#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_platform.h"

#if MOJOCRASH_PLATFORM_MACOSX

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <dlfcn.h>

#ifdef LP64
typedef struct mach_header_64 MachHeader;
typedef struct segment_command_64 SegmentCommand;
#else
typedef struct mach_header MachHeader;
typedef struct segment_command SegmentCommand;
#endif


#include "mojocrash_internal.h"

static int (*libc_backtrace)(void**,int) = NULL;

int MOJOCRASH_platform_get_callstack(MOJOCRASH_get_callstack_callback cb)
{
    /* If we're on 10.5 or later, use the system facilities. */
    if (libc_backtrace != NULL)
    {
        /* !!! FIXME: would be nice to have a backtrace_callback(). */
        /* We're memory-hungry, in hopes of avoiding malloc() during crash. */
        static void *callstack[1024];
        int frames = libc_backtrace(callstack, STATICARRAYLEN(callstack));
        int i;

        for (i = 0; i < frames; i++)
        {
            if (!cb(callstack[i]))
                return 0;
        } /* for */

        return 1;
    } /* if */

    /* !!! FIXME: roll your own. */

    return 0;  /* uh oh. */
} /* MOJOCRASH_platform_get_callstack */


static inline const char *fname_without_dirs(const char *path)
{
    const char *retval = strrchr(path);
    return ((retval) ? (retval+1) : path);
} /* fname_without_dirs */


int MOJOCRASH_platform_get_objects(MOJOCRASH_get_objects_callback cb)
{
    const uint32_t images = _dyld_image_count();
    uint32_t i;

    for (i = 0; i < images; i++)
    {
        const char *path = fname_without_dirs(_dyld_get_image_name(i));
        const MachHeader *header = (MachHeader *) _dyld_get_image_header(i);
        const void *addr = header;
        const char *ptr = ((char *) addr) + sizeof (MachHeader);
        unsigned long len = 0;
        uint32_t j;

        for (j = 0; j < header->ncmds; j++)
        {
            const SegmentCommand *cmd = (const SegmentCommand *) ptr;
            ptr += cmd->cmdsize;

            if (cmd->cmd != LC_SEGMENT)
                continue;  /* whoops, not really a segment command! */

            if ( (strcmp(cmd->segname, "__TEXT") != 0) &&
                 (strcmp(cmd->segname, "__DATA") != 0) )
                continue;  /* don't care about this segment. */

            len += (unsigned long) cmd->vmsize;
        } /* for */

        if (!cb(path, addr, len))
            return 0;  /* stop! */
    } /* for */
} /* MOJOCRASH_platform_get_objects */


int MOJOCRASH_platform_init(void)
{
    char logpath[PATH_MAX+1];
    const char *homedir = NULL;

    /* !!! FIXME: dlopen() limits you to 10.3+ ... */
    void *lib = dlopen("libSystem.dylib", RTLD_GLOBAL | RTLD_NOW);
    void *libc_backtrace = dlsym(lib, "backtrace");

    /* !!! FIXME: use Carbon call for this... */
    homedir = getenv("HOME");
    if (homedir == NULL)
        homedir = ".";  /* !!! FIXME: read /etc/passwd? */

    len = snprintf(logpath, sizeof (logpath),
                   "%s/Library/Application Support/MojoCrash",
                   homedir);
    if (len >= sizeof (logpath) - 16)
        return 0;

    return MOJOCRASH_unix_init(logpath);
} /* MOJOCRASH_platform_init */

#endif /* MOJOCRASH_PLATFORM_MACOSX */

