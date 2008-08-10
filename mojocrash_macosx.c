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
 *
 * The ABI Function Call Guide was helpful, for moving this from 32-bit PPC
 *  two four total CPU architectures:
 *
 *  http://developer.apple.com/documentation/DeveloperTools/Conceptual/LowLevelABI/Introduction.html
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
    /* offset of caller's address in linkage area, from base of stack frame. */
    #if MOJOCRASH_PLATFORM_POWERPC || MOJOCRASH_PLATFORM_POWERPC_64
    const uintptr_t linkage_offset = sizeof (uintptr_t) * 2;  /* 2 pointers. */
    #elif MOJOCRASH_PLATFORM_X86 || MOJOCRASH_PLATFORM_X86_64
    const uintptr_t linkage_offset = sizeof (uintptr_t);  /* one pointer. */
    #else
    #error Unhandled CPU arch.
    #endif

    uintptr_t fp = 0;  /* frame pointer. */
    uintptr_t pc = 0;  /* program counter. */
    uintptr_t lower_bound = 0;

    fp = (uintptr_t) __builtin_frame_address(1);
    pc = (uintptr_t) __builtin_return_address(0);

    /* MoreBacktrace checks for system calls here, but we don't bother. */

    while (1)  /* go until we fail. */
    {
        uintptr_t nextFrame, nextPC, dummy;
        const int isSigtramp = ((pc >= sigtramp_start) && (pc < sigtramp_end));

        #if MOJOCRASH_PLATFORM_POWERPC || MOJOCRASH_PLATFORM_POWERPC_64
        if ((pc & (sizeof (uintptr_t)-1)) != 0)
            break;  /* opcodes must be word-aligned on PPC...corrupt stack. */
        #endif

        if (!safe_read_ptr(pc, &dummy))
            break;  /* can't read? Bogus program counter. Give up. */

        if (skip > 0)
            skip--;
        else if (!cb((void *) pc))
            break;  /* stop here. */

        /*
         * !!! FIXME: ABI guides for all 4 CPUs say the frame pointer should
         * !!! FIXME:  be aligned to 16 bytes, not pointer size. Check this!
         */
        if ((fp == 0) || (fp & (sizeof (uintptr_t)-1)) || (fp <= lower_bound))
            break;  /* Bogus frame pointer. Give up. */

        if (isSigtramp) /* the dreaded _sigtramp()! Have to adjust frame. */
            fp += sigtramp_frame_offset;

        /* get the next stack frame (zero bytes into current stack frame). */
        if (!safe_read_ptr(fp, &nextFrame))
            break;  /* Can't read? Bogus frame pointer. Give up. */

        /* get the stored Linkage Area value. */
        if (!safe_read_ptr(nextFrame + linkage_offset, &nextPC))
            break;  /* Can't read? Bogus frame pointer. Give up. */

        lower_bound = fp;
        pc = nextPC;
        fp = nextFrame;
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
    long major, minor;

	if (Gestalt(gestaltSystemVersion, &ver) != noErr)
        return 0;

    major = ((ver & 0xFF) >> 8);
    minor = ((ver & 0xF0) >> 4);
    /*patch = (ver & 0xF);*/

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

    #define XX 0  /* just to show it's unsupported. */
    static const struct { uintptr_t offset; uintptr_t len; } sigtramps[] = {
        /* 10.0, 10.1, 10.2, 10.3, 10.4, 10.5 */
        #if MOJOCRASH_PLATFORM_POWERPC
        {XX,XX}, {0x7C,0}, {0xA4,0}, {0,0},   {0xFC,0x248}, {0x9C,0x50},
        #elif MOJOCRASH_PLATFORM_POWERPC_64
        {XX,XX}, {XX,XX},  {XX,XX},  {XX,XX}, {0,0x27C},    {0xB8,0x50},
        #elif MOJOCRASH_PLATFORM_X86
        {XX,XX}, {XX,XX},  {XX,XX},  {XX,XX}, {0,0},        {0x80,0x44},
        #elif MOJOCRASH_PLATFORM_X86_64
        {XX,XX}, {XX,XX},  {XX,XX},  {XX,XX}, {0,0},        {0,0x30},
        #else
        {XX,XX}, {XX,XX},  {XX,XX},  {XX,XX}, {0,0},        {0,0},
        #endif
    };
    #undef XX

    if (major != 0x10)
        must_have_backtrace = 1;  /* not Mac OS X 10.x? */
    else if (minor >= STATICARRAYLEN(sigtramps))
        must_have_backtrace = 1;  /* newer Mac OS X than we know about. */
    else
    {
        sigtramp_frame_offset = sigtramps[minor].offset;
        sigtramp_len = sigtramps[minor].len;
        if ((sigtramp_frame_offset == 0) || (sigtramp_len == 0))
            must_have_backtrace = 1;  /* we lack data. */
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

