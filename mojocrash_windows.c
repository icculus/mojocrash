#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_platform.h"

#if MOJOCRASH_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <winsock.h>

#include "mojocrash_internal.h"

static int osIsWin9x = 0;
static char osversion[64];
ULONGLONG starttime = 0;


int MOJOCRASH_platform_init(void)
{
    static int already_initialized = 0;
    OSVERSIONINFO osVerInfo;
    char numcvt[32];
    char *str;
    int avail;

    if (already_initialized)
        return 1;
    already_initialized = 1;

    osVerInfo.dwOSVersionInfoSize = sizeof(osVerInfo);
    if (!pGetVersionEx(&osVerInfo))
        return 0;

    osIsWin9x = (osVerInfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS);

    str = osversion;
    avail = sizeof (osversion);
    MOJOCRASH_StringAppend(&str, avail,
                MOJOCRASH_ULongToString(osVerInfo.dwMajorVersion, numcvt));
    MOJOCRASH_StringAppend(dst, avail, ".");
    MOJOCRASH_StringAppend(dst, avail,
                MOJOCRASH_ULongToString(osVerInfo.dwMinorVersion, numcvt));
    MOJOCRASH_StringAppend(dst, avail, ".");
    MOJOCRASH_StringAppend(dst, avail,
                MOJOCRASH_ULongToString(osVerInfo.dwBuildNumber, numcvt));

    if (pGetTickCount64 != NULL)
        starttime = pGetTickCount64();
    else
        starttime = (ULONGLONG) pGetTickCount();

    return 1;
} /* MOJOCRASH_platform_init */

static MOJOCRASH_catcher crash_catcher = NULL;
static LONG WINAPI windows_crash_catcher_bridge(LPEXCEPTION_POINTERS excpt)
{
    crash_catcher(excpt->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_EXECUTION;
} /* windows_crash_catcher_bridge */


int MOJOCRASH_platform_install_crash_catcher(MOJOCRASH_catcher catcher)
{
    crash_catcher = catcher;
    pSetUnhandledExceptionFilter(windows_crash_catcher_bridge);
} /* MOJOCRASH_platform_install_crash_catcher */


/*
 * This will read memory if it's safe to, and reports an error instead of
 *  segfaulting otherwise. We hope.
 */
static inline int safe_read_ptr(const uintptr_t src, uintptr_t *dst)
{
    if (IsBadReadPtr((void *) src, sizeof (void *)))
        return 0;
    *dst = *((uintptr_t *) src);
} /* safe_read_ptr */


static int walk_windows_stack(int skip, MOJOCRASH_get_callstack_callback cb)
{
    /* offset of caller's address in linkage area, from base of stack frame. */
    const uintptr_t linkage_offset = sizeof (uintptr_t);  /* one pointer. */
    uintptr_t fp = 0;  /* frame pointer. */
    uintptr_t pc = 0;  /* program counter. */
    uintptr_t lower_bound = 0;

    fp = (uintptr_t) __builtin_frame_address(1);
    pc = (uintptr_t) __builtin_return_address(0);

#ifdef _MSC_VER
__try {
#endif

    while (1)  /* go until we fail. */
    {
        uintptr_t nextFrame, nextPC, dummy;

        if (!safe_read_ptr(pc, &dummy))
            break;  /* can't read? Bogus program counter. Give up. */

        if (skip > 0)
            skip--;
        else if (!cb((void *) pc))
            return 0;  /* stop here. */

        if ((fp == 0) || (fp & (sizeof (uintptr_t)-1)) || (fp <= lower_bound))
            break;  /* Bogus frame pointer. Give up. */

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

#ifdef _MSC_VER
} __except(EXCEPTION_EXECUTE_HANDLER) { /* no-op. */ }
#endif

    return 1;
} /* walk_windows_stack */


int MOJOCRASH_platform_get_callstack(MOJOCRASH_get_callstack_callback cb)
{
    return walk_windows_stack(0, cb);
} /* MOJOCRASH_platform_get_callstack */


static int win9x_get_objects(MOJOCRASH_get_objects_callback cb)
{
    MODULEENTRY32 module32;
    BOOL rc = 0;
    HANDLE h = pCreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    module32.dwSize = sizeof (module32);
    rc = pModule32First(h, &module32);
    while (rc)
    {
        cb(module32.szExePath, module32.modBaseAddr, module32.modBaseSize, 0);
        rc = pModule32Next(h, &module32);
    } /* while */

    return (pGetLastError() != ERROR_NO_MORE_FILES);
} /* win9x_get_objects */


static int winnt_get_objects(MOJOCRASH_get_objects_callback cb)
{
    static HMODULE mods[1024];
    const HANDLE h = pGetCurrentProcess();
    DWORD total = 0;
    DWORD i, j;

    if (!pEnumProcessModules(h, mods, sizeof(mods), &total))
        return 0;

    total /= sizeof(HMODULE);
    for (i = 0; i < total; i++)
    {
        static char modname[MAX_PATH];
        MODULEINFO modinfo;
        char *ptr = NULL;

        if (!pGetModuleFileNameExA(h, mods[i], modname, sizeof (modname)))
            return 0;

        if (!pGetModuleInformation(h, mods[i], &modinfo, sizeof (modinfo)))
            return 0;

        ptr = modname;
        for (j = 0; modname[j]; j++)
        {
            if (modname[j] == '\\')
                ptr = &modname[j];
        } /* for */

        cb(modname, modinfo.lpBaseOfDll, modinfo.SizeOfImage, 0);
    } /* for */

    return 1;
} /* winnt_get_objects */


int MOJOCRASH_platform_get_objects(MOJOCRASH_get_objects_callback cb)
{
    return (isWin9x) ? win9x_get_objects(cb) : winnt_get_objects(cb);
} /* MOJOCRASH_platform_get_objects */


int MOJOCRASH_platform_start_crashlog(const char *appname)
{
} /* MOJOCRASH_platform_start_crashlog */


int MOJOCRASH_platform_new_crashlog_line(const char *str)
{
} /* MOJOCRASH_platform_new_crashlog_line */


int MOJOCRASH_platform_end_crashlog(void)
{
} /* MOJOCRASH_platform_end_crashlog */


void MOJOCRASH_platform_die(int force)
{
    if (force)  /* !!! FIXME: not sure if this is right. */
        pTerminateProcess(pGetCurrentProcess(), 86);
    else
        pExitProcess(86);
} /* MOJOCRASH_platform_die */


const char *MOJOCRASH_platform_version(void)
{
    return osversion;
} /* MOJOCRASH_platform_version */


long MOJOCRASH_platform_appuptime(void)
{
    if (pGetTickCount64 != NULL)
        return (long) ((pGetTickCount64() - starttime) / 1000);
    return (long) ((pGetTickCount() - ((DWORD)starttime)) / 1000);
} /* MOJOCRASH_platform_appuptime */


/* !!! FIXME: long rolls over in 2038 ... use a 64-bit value */
long MOJOCRASH_platform_now(void)
{
    const ULONGLONG winEpochToUnixEpoch = CONST_UI64(0x019DB1DED53E8000);
    const ULONGLONG nanosecToMillisec = CONST_UI64(10000000);
    SYSTEMTIME systime;
    FILETIME filetime;
    ULARGE_INTEGER large;
    pGetSystemTime(&systime);
    pSystemTimeToFileTime(&systime, &filetime);
    large.LowPart = filetime.dwLowDateTime;
    large.HighPart = filetime.dwHighDateTime;
    return (long) ((large.QuadPart - winEpochToUnixEpoch) / nanosecToMillisec);
} /* MOJOCRASH_platform_now */


const char **MOJOCRASH_platform_load_reports(const char *appname, int *total)
{
} /* MOJOCRASH_platform_load_reports */


void MOJOCRASH_platform_delete_report(const char *appname, const int idx)
{
} /* MOJOCRASH_platform_delete_report */


void MOJOCRASH_platform_free_reports(const char **reports, const int total)
{
} /* MOJOCRASH_platform_free_reports */


int MOJOCRASH_platform_init_network(void)
{
    WSADATA data;
    if (WSAStartup(MAKEWORD(1, 1), &data) != 0)
        return 0;
} /* MOJOCRASH_platform_init_network */


void MOJOCRASH_platform_deinit_network(void)
{
    WSACleanup();
} /* MOJOCRASH_platform_deinit_network */


typedef struct DnsResolve
{
    int in_use;
    char host[MAX_PATH];
    int port;
    CRITICAL_SECTION mutex;
    struct addrinfo *addr;
    struct hostent *hent;
    volatile int status;
    volatile int app_done;
} DnsResolve;


static void free_dns(DnsResolve *dns)
{
    pDeleteCriticalSection(&dns->mutex);
    if (dns->addr != NULL)
        pfreeaddrinfo(dns->addr);
    dns->in_use = 0;
    /* this is a static struct at the moment, don't free it. */
} /* free_dns */


static void dns_resolver_thread(void *_dns)
{
    int app_done = 0;
    DnsResolve *dns = (DnsResolve *) _dns;
    int rc = -1;

    /* gethostbyname and getaddrinfo block, hence all the thread tapdancing. */
    if (pgetaddrinfo != NULL)
    {
        char portstr[64];
        MOJOCRASH_LongToString(dns->port, portstr);
        rc = pgetaddrinfo(dns->host, portstr, NULL, &dns->addr);
    } /* if */

    else  /* pre-XP systems use this. */
    {
        if (pinet_addr(dns->host) != INADDR_NONE)  /* a ip addr string? */
            dns->hent = pgethostbyaddr(dns->host, 4, AF_INET);
        else
            dns->hent = pgethostbyname(dns->host);
        rc = (dns->hent == NULL) ? -1 : 0;
    } /* else */

    pEnterCriticalSection(&dns->mutex);
    dns->status = (rc == 0) ? 1 : -1;
    app_done = dns->app_done;
    pLeaveCriticalSection(&dns->mutex);

    /* we free if the app is done, otherwise, app will do it. */
    if (app_done)
        free_dns(dns);
} /* dns_resolver_thread */


void *MOJOCRASH_platform_begin_dns(const char *host, const int port,
                                   const int blocking)
{
    /* just made this static to avoid malloc() call. */
    static DnsResolve retval;
    int mutex_initted = 0;

    if ( (strlen(host) + 1) >= sizeof (retval.host) )
        return NULL;
    else if (retval.in_use)
        return NULL;

    pInitializeCriticalSection(&retval->mutex);
    mutex_initted = 1;
    strcpy(retval->host, host);
    retval->port = port;
    retval->addr = NULL;
    retval->hent = NULL;
    retval->status = 0;
    retval->app_done = 0;
    retval->in_use = 1;

    if (blocking)
        dns_resolver_thread(retval);
    else
    {
        if (!MOJOCRASH_platform_spin_thread(dns_resolver_thread, retval))
            goto begin_dns_failed;
    } /* else */

    return &retval;

begin_dns_failed:
    if (mutex_initted)
        pDeleteCriticalSection(&retval->mutex);
    return NULL;
} /* MOJOCRASH_platform_begin_dns */


int MOJOCRASH_platform_check_dns(void *_dns)
{
    DnsResolve *dns = (DnsResolve *) _dns;
    int retval;
    if (dns == NULL)
        return -1;
    pEnterCriticalSection(&dns->mutex);
    retval = dns->status;
    pLeaveCriticalSection(&dns->mutex);
    return retval;
} /* MOJOCRASH_platform_check_dns */


void MOJOCRASH_platform_free_dns(void *_dns)
{
    int thread_done = 0;
    DnsResolve *dns = (DnsResolve *) _dns;
    if (dns == NULL)
        return;

    pEnterCriticalSection(&dns->mutex);
    dns->app_done = 1;
    thread_done = (dns->status != 0);
    pLeaveCriticalSection(&dns->mutex);

    /* we free if the thread is done, otherwise, thread will do it. */
    if (thread_done)
        free_dns(dns);
} /* MOJOCRASH_platform_free_dns */


void *MOJOCRASH_platform_open_socket(void *_dns, const int blocking)
{
    DnsResolve *dns = (DnsResolve *) _dns;
    SOCKET fd = INVALID_SOCKET_HANDLE;
    DWORD one = 1;
    int success = 0;

    if (MOJOCRASH_platform_check_dns(dns) != 1)
        return NULL;  /* dns resolve isn't done or it failed. */

    if (dns->addr != NULL)
    {
        const struct addrinfo *addr;
        for (addr = dns->addr; addr != NULL; addr = addr->ai_next)
        {
            if (addr->ai_socktype != SOCK_STREAM)
                continue;

            if (fd != INVALID_SOCKET_HANDLE)
                pclosesocket(fd);

            fd = psocket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
            if (fd == INVALID_SOCKET_HANDLE)
                continue;

            if (!blocking)
            {
                if (pioctlsocket(fd, FIONBIO, &one) != 0)
                    continue;
            } /* if */

            if (pconnect(fd, addr->ai_addr, addr->ai_addrlen) != 0)
            {
                if (pWSAGetLastError() != WSAEWOULDBLOCK)
                    continue;
            } /* if */

            success = 1;
            break;
        } /* for */
    } /* if */

    else if (dns->hent != NULL)  /* pre-XP systems. */
    {
        const struct hostent *hent = dns->hent;
        fd = psocket(hent->h_addrtype, SOCK_STREAM, IPPROTO_TCP);
        if (fd != INVALID_SOCKET_HANDLE)
        {
            if ((blocking) || (pioctlsocket(fd, FIONBIO, &one) == 0))
            {
                if (pconnect(fd, hent->h_addr_list[0], hent->h_length) == 0)
                    success = 1;
                else if (pWSAGetLastError() == WSAEWOULDBLOCK)
                    success = 1;
            } /* if */
        } /* if */
    } /* else if */

    if (!success)
    {
        if (fd != INVALID_SOCKET_HANDLE)
            pclosesocket(fd);
        return NULL;
    } /* if */

    return ((void *) retval);
} /* MOJOCRASH_platform_open_socket */


int MOJOCRASH_platform_check_socket(void *sock)
{
    const SOCKET fd = ((SOCKET) sock);
    fd_set wfds;
    struct timeval tv;
    int retval;

    /* one handle, no wait. */
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    retval = pselect(fd+1, NULL, &wfds, NULL, &tv);
    return ((retval > 0) && (FD_ISSET(fd, &wfds) != 0));
} /* MOJOCRASH_platform_check_socket */


void MOJOCRASH_platform_close_socket(void *sock)
{
    pclosesocket((SOCKET) sock);
} /* MOJOCRASH_platform_close_socket */


int MOJOCRASH_platform_read_socket(void *sock, char *buf, const int l)
{
    const SOCKET fd = ((SOCKET) sock);
    const int retval = (int) precv(fd, buf, l, 0);
    if (retval < 0)
        return (pWSAGetLastError() == WSAEWOULDBLOCK) ? -2 : -1;
    return retval;
} /* MOJOCRASH_platform_read_socket */


int MOJOCRASH_platform_write_socket(void *sock, const char *buf, const int l)
{
    const SOCKET fd = ((SOCKET) sock);
    const int retval = (int) psend(fd, buf, l, 0);
    if (retval < 0)
        return (pWSAGetLastError() == WSAEWOULDBLOCK) ? -2 : -1;
    return retval;
} /* MOJOCRASH_platform_write_socket */


int MOJOCRASH_platform_get_http_proxy(char *buf, const int buflen)
{
    int ok = 0;
    DWORD len = 0;
    char scratch[1024];
    char auth[128];
    char *str = buf;
    int avail = buflen;
    const INTERNET_PROXY_INFO *info = (INTERNET_PROXY_INFO *) scratch;

    if (pInternetQueryOption == NULL)  /* maybe NT 3.x doesn't have it.  :) */
        return 0;
    else if (!pInternetQueryOption(0, INTERNET_OPTION_PROXY, NULL, &len))
        return 0;
    else if (len > sizeof (scratch))
        return 0;
    else if (!pInternetQueryOption(0, INTERNET_OPTION_PROXY, scratch, &len))
        return 0;
    else if (info->dwAccessType != INTERNET_OPEN_TYPE_PROXY)
        return 0;  /* no proxy configured or something we don't understand. */

    MOJOCRASH_StringAppend(&str, &avail, "http://");
    if (strncmp(info->lpszProxy, "http://", 7) == 0)
        info->lpszProxy += 7;

    if (pInternetQueryOption(0, INTERNET_OPTION_PROXY_USERNAME, NULL, &len))
    {
        if (len <= sizeof (auth))
        {
            if (pInternetQueryOption(0, INTERNET_OPTION_PROXY_USERNAME,
                                     auth, &len))
            {
                MOJOCRASH_StringAppend(&str, &avail, auth);
                if (pInternetQueryOption(0, INTERNET_OPTION_PROXY_PASSWORD,
                                         NULL, &len))
                if (len <= sizeof (auth))
                {
                    if (pInternetQueryOption(0, INTERNET_OPTION_PROXY_PASSWORD,
                                             auth, &len))
                    {
                        MOJOCRASH_StringAppend(&str, &avail, ":");
                        MOJOCRASH_StringAppend(&str, &avail, auth);
                    } /* if */
                    MOJOCRASH_StringAppend(&str, &avail, "@");
                } /* if */
            } /* if */
        } /* if */
    } /* if */

    /*
     * We SHOULD look to see if our host is in the exclusion list
     *  (info->lpszProxyBypass), but I reasonably doubt anyone is going to
     *  put it there.
     */

    MOJOCRASH_StringAppend(&str, &avail, info->lpszProxy);
    return 1;
} /* MOJOCRASH_platform_get_http_proxy */


typedef struct
{
    MOJOCRASH_thread_entry fn;
    void *arg;
    volatile int done;
} thread_data;

static DWORD WINAPI thread_bridge(LPVOID _arg)
{
    thread_data *data = (thread_data *) _arg;
    MOJOCRASH_thread_entry fn = data->fn;
    void *arg = data->arg;
    data->done = 1;
    fn(arg);
    return 0;
} /* thread_bridge */

int MOJOCRASH_platform_spin_thread(MOJOCRASH_thread_entry fn, void *arg)
{
    thread_data data = { fn, arg, 0 };
    HANDLE h = CreateThread(NULL, 0, thread_bridge, &data);
    if (h == NULL)
        return 0;
    CloseHandle(h);
    while (!data.done) { /* spin. */ }
    return 1;
} /* MOJOCRASH_platform_spin_thread */

#endif  /* MOJOCRASH_PLATFORM_WINDOWS */

/* end of mojocrash_windows.c ... */

