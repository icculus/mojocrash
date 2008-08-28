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


int MOJOCRASH_platform_get_callstack(MOJOCRASH_get_callstack_callback cb)
{
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


void *MOJOCRASH_platform_begin_dns(const char *host, const int port, const int blocking)
{
} /* MOJOCRASH_platform_begin_dns */


int MOJOCRASH_platform_check_dns(void *dns)
{
} /* MOJOCRASH_platform_check_dns */


void MOJOCRASH_platform_free_dns(void *dns)
{
} /* MOJOCRASH_platform_free_dns */


void *MOJOCRASH_platform_open_socket(void *dns, const int blocking)
{
} /* MOJOCRASH_platform_open_socket */


int MOJOCRASH_platform_check_socket(void *sock)
{
} /* MOJOCRASH_platform_check_socket */


void MOJOCRASH_platform_close_socket(void *sock)
{
} /* MOJOCRASH_platform_close_socket */


int MOJOCRASH_platform_read_socket(void *sock, char *buf, const int l)
{
} /* MOJOCRASH_platform_read_socket */


int MOJOCRASH_platform_write_socket(void *sock, const char *buf, const int l)
{
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


int MOJOCRASH_platform_spin_thread(void (*fn)(void *), void *arg)
{
} /* MOJOCRASH_platform_spin_thread */

#endif  /* MOJOCRASH_PLATFORM_WINDOWS */

/* end of mojocrash_windows.c ... */

