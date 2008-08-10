#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_platform.h"

#if MOJOCRASH_PLATFORM_MACOSX

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/vm_map.h>
#include <mach/mach_init.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <Gestalt.h>

#if MOJOCRASH_PLATFORM_64BIT
typedef struct mach_header_64 MachHeader;
typedef struct segment_command_64 SegmentCommand;
#elif MOJOCRASH_PLATFORM_32BIT
typedef struct mach_header MachHeader;
typedef struct segment_command SegmentCommand;
#else
#error There seems to be a problem.
#endif

/*
 * The stackwalking code was, a lifetime ago, based on MoreBacktrace.c from
 *  Apple's MoreIsBetter examples ... but obviously it's a different beast
 *  now. The MoreIsBetter license allows this sort of thievery.
 */


#include "mojocrash_internal.h"

static int (*libc_backtrace)(void**,int) = NULL;
static uintptr_t sigtramp_start = 0;
static uintptr_t sigtramp_end = 0;
static uintptr_t sigtramp_frame_offset = 0;


/*
 * This will read memory if it's safe to, and reports an error instead of
 *  segfaulting otherwise.
 */
static inline int safe_read_ptr(const uintptr_t src, uintptr_t *dst)
{
    vm_size_t sizeRead = sizeof (uintptr_t);
    const kern_return_t err = vm_read_overwrite((thread_t) mach_task_self(),
                                                (vm_address_t) src,
                                                sizeof (uintptr_t),
                                                (vm_address_t) dst, &sizeRead);
    return ((err == 0) && (sizeRead == sizeof (uintptr_t)));
} /* safe_read_ptr */


static void walk_macosx_stack(int skip, MOJOCRASH_get_callstack_callback cb)
{
    uintptr_t sp = 0;
    uintptr_t pc = 0;
    uintptr_t lower_bound = 0;

    sp = (uintptr_t) __builtin_frame_address(1);
    pc = (uintptr_t) __builtin_return_address(0);

    /* MoreBacktrace checks for system calls here, but we don't bother. */

    while (1)  /* go until we fail. */
    {
        uintptr_t nextFrame, nextPC, dummy;
        const int isSigtramp = ((pc >= sigtramp_start) && (pc < sigtramp_end));

        if ( ((pc & 0x03) == 0) && (safe_read_ptr(pc, &dummy)) )
        {
            if (!isSigtramp)
            {
                if (skip > 0)
                    skip--;
                else if (!cb((void *) pc))
                    break;  /* stop here. */
            } /* if */
        } /* if */

        if ((sp == 0) || (sp & (sizeof (uintptr_t)-1)) || (sp <= lower_bound))
            break;  /* Bogus frame pointer. Give up. */

        if (isSigtramp) /* the dreaded _sigtramp()! Have to skip past it. */
            sp += sigtramp_frame_offset;

        /* get the next stack frame (zero bytes into current stack frame). */
        if (!safe_read_ptr(sp, &nextFrame))
            break;  /* Can't read? Bogus frame pointer. Give up. */

        /* get the LR (two pointers in to current stack frame). */
        else if (!safe_read_ptr(nextFrame + (sizeof (uintptr_t) * 2), &nextPC))
            break;  /* Can't read? Bogus frame pointer. Give up. */

        lower_bound = sp;
        pc = nextPC;
        sp = nextFrame;
    } /* while */
} /* walk_macosx_stack */


int MOJOCRASH_platform_get_callstack(MOJOCRASH_get_callstack_callback cb)
{
    /* skip: backtrace(), this, !!! FIXME: other things? */
    const int skip_frames = 2;

    /* Prior to Mac OS X 10.5, we have to walk the stack ourselves.  :/  */
    if (libc_backtrace == NULL)
        walk_macosx_stack(skip_frames, cb);
    else
    {
        /* If we're on 10.5 or later, just use the system facilities. */
        /* !!! FIXME: would be nice to have a backtrace_callback(). */
        /* We're memory-hungry, in hopes of avoiding malloc() during crash. */
        static void *callstack[1024];
        int frames = libc_backtrace(callstack, STATICARRAYLEN(callstack));
        int i;

        for (i = skip_frames; i < frames; i++)
        {
            if (!cb(callstack[i]))
                return 0;
        } /* for */
    } /* else */

    return 1;
} /* MOJOCRASH_platform_get_callstack */


static inline const char *fname_without_dirs(const char *path)
{
    const char *retval = strrchr(path, '/');
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
    long ver = 0;
    int len = 0;
    void *lib = NULL;
    uintptr_t sigtramp_len = 0;
    int must_have_backtrace = 0;

	if (Gestalt(gestaltSystemVersion, &ver) != noErr)
        return 0;

    /* Apple never shipped an OS X before 10.4 for anyting but ppc32. */
    #if !MOJOCRASH_PLATFORM_POWERPC
    if ((ver & 0xFFFF) < 0x1040)
        return 0;  /* shouldn't happen... */
    #endif

    /*
     * If we don't have backtrace (and sometimes when we do!), we need to
     *  tapdance down the callstack. If we're passing through a signal
     *  handler (and we WILL be), then we need a magic offset that changes
     *  between Mac OS X releases. Figure out which magic we need, here.
     */

    /*
     * sigtramp_len is the offset between _sigtramp and the symbol after it,
     *  according to "nm /usr/lib/libSystem.dylib |sort" on the various
     *  releases. It's an inexact science.  :)
     */

    if (ver & 0xFFF0) <= 0x1000)  /* 10.0.x? ("Cheetah") */
        return 0;  /* 10.0.x is unsupported. Sorry. */

    else if (ver & 0xFFF0) == 0x1010)  /* 10.1.x? ("Puma") */
    {
        sigtramp_frame_offset = 0x7C;
        sigtramp_len = 0;  /* !!! FIXME */
        return 0;  /* !!! FIXME */
    } /* else if */

    else if (ver & 0xFFF0) == 0x1020)  /* 10.2.x? ("Jaguar") */
    {
        sigtramp_frame_offset = 0xA4;
        sigtramp_len = 0;  /* !!! FIXME */
        return 0;  /* !!! FIXME */
    } /* else if */

    else if (ver & 0xFFF0) == 0x1030)  /* 10.3.x? ("Panther") */
    {
        sigtramp_frame_offset = 0; /* !!! FIXME */
        sigtramp_len = 0;  /* !!! FIXME */
        return 0;  /* !!! FIXME */
    } /* else if */

    else if (ver & 0xFFF0) == 0x1040)  /* 10.4.x? ("Tiger") */
    {
        #if MOJOCRASH_PLATFORM_POWERPC
            sigtramp_frame_offset = 0xFC;
            sigtramp_len = 0x248;
        #elif MOJOCRASH_PLATFORM_POWERPC_64
            sigtramp_frame_offset = 0;  /* !!! FIXME */
            sigtramp_len = 0x27c;
            return 0;  /* !!! FIXME */
        #elif MOJOCRASH_PLATFORM_X86_64
            sigtramp_frame_offset = 0; /* !!! FIXME */
            sigtramp_len = 0;  /* !!! FIXME */
            return 0;  /* !!! FIXME */
        #elif MOJOCRASH_PLATFORM_X86
            sigtramp_frame_offset = 0; /* !!! FIXME */
            sigtramp_len = 0;  /* !!! FIXME */
            return 0;  /* !!! FIXME */
        #else
            must_have_backtrace = 1;  /* hopefully we have it! */
        #endif
    } /* else if */

    else if (ver & 0xFFF0) == 0x1050)  /* 10.5.x? ("Leopard") */
    {
        /* PowerPC32/64 have backtrace(), but it fails in signal handlers. */
        #if MOJOCRASH_PLATFORM_POWERPC
            sigtramp_frame_offset = 0x9C;
            sigtramp_len = 0x50;
        #elif MOJOCRASH_PLATFORM_POWERPC_64
            sigtramp_frame_offset = 0xB8;
            sigtramp_len = 0x50;
        #elif MOJOCRASH_PLATFORM_X86_64
            sigtramp_frame_offset = 0; /* !!! FIXME */
            sigtramp_len = 0;  /* !!! FIXME */
            return 0;  /* !!! FIXME */
        #else  /* x86 backtrace() works in 10.5.0. */
            must_have_backtrace = 1;  /* hopefully we have it! */
        #endif
    } /* else if */

    else
    {
        must_have_backtrace = 1; /* Unknown OS version? We need backtrace(). */
    } /* else */

    /* !!! FIXME: dlopen() limits you to 10.3+ ... */
    lib = dlopen("libSystem.dylib", RTLD_GLOBAL | RTLD_NOW);
    if (lib == NULL)
        return 0;

    if (!must_have_backtrace)
        libc_backtrace = NULL;
    else
    {
        libc_backtrace = dlsym(lib, "backtrace");
        if (libc_backtrace == NULL)
            return 0;
    } /* else */

    sigtramp_start = (uintptr_t) dlsym(lib, "_sigtramp");
    sigtramp_end = sigtramp_start + sigtramp_len;

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

