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
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "mojocrash_internal.h"

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


static int build_dirs(char *path)
{
    int rc = 0;
    char *ptr = path;
    if (*ptr == '/')
        ptr++;

    while (1)
    {
        const char ch = *ptr;
        if ((ch == '/') || (ch == '\0'))
        {
            *ptr = '\0';
            rc = mkdir(path, S_IRWXU);
            *ptr = ch;
            if ((rc == -1) && (errno != EEXIST))
                return 0;
        } /* if */

        if (ch == '\0')
            break;

        ptr++;
    } /* while */

    return 1;
} /* build_dirs */


int MOJOCRASH_platform_start_crashlog(const char *appname)
{
    char logpath[PATH_MAX+1];
    char *sep = NULL;
    int len = 0;
    int num = 0;

    MOJOCRASH_unix_get_logpath(logpath, sizeof (logpath), appname);
    len = strlen(logpath);
    if (len > (sizeof (logpath) - 16))
        return 0;
    sep = &logpath[len];
    *sep = '/';
    sep++;

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
        build_dirs(logpath);
        snprintf(sep, sizeof (logpath) - len, "%d", num);
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


static char **loaded_reports = NULL;

const char **MOJOCRASH_platform_load_reports(const char *appname, int *total)
{
    char logpath[PATH_MAX+1];
    char **retval = NULL;
    int count = 0;
    DIR *dirp = NULL;
    struct dirent *dent = NULL;

    MOJOCRASH_unix_get_logpath(logpath, sizeof (logpath), appname);
    dirp = opendir(logpath);
    if (dirp == NULL)
        return NULL;

    while ((dent = readdir(dirp)) != NULL)
    {
        char path[PATH_MAX+1];
        const char *name = dent->d_name;
        char *data = NULL;
        char **ptr = NULL;
        struct stat statbuf;
        int rc = 0;
        int io = -1;

        if ((strcmp(name, ".") == 0) || (strcmp(name, "..") == 0))
            continue;

        snprintf(path, sizeof (path), "%s/%s", logpath, name);
        if (stat(path, &statbuf) == -1)
            continue;

        if (S_ISDIR(statbuf.st_mode))
            continue;

        ptr = (char **) realloc(retval, sizeof (char *) * (count+1));
        if (ptr == NULL)
            continue;
        retval = ptr;

        ptr = (char **) realloc(loaded_reports, sizeof (char *) * (count+1));
        if (ptr == NULL)
            continue;
        loaded_reports = ptr;

        io = open(path, O_RDONLY);
        if (io == -1)
            continue;

        data = (char *) malloc(statbuf.st_size + 1);
        if (data == NULL)
        {
            close(io);
            continue;
        } /* if */

        rc = read(io, data, statbuf.st_size);
        close(io);
        if (rc != statbuf.st_size)
        {
            free(data);
            continue;
        } /* if */
        data[statbuf.st_size] = '\0';

        loaded_reports[count] = (char *) malloc(strlen(path) + 1);
        if (loaded_reports[count] == NULL)
        {
            free(data);
            continue;
        } /* if */

        strcpy(loaded_reports[count], path);
        retval[count] = data;
        count++;
    } /* while */

    closedir(dirp);

    if (count == 0)
    {
        free(retval);
        free(loaded_reports);
        retval = NULL;
        loaded_reports = NULL;
    } /* if */

    *total = count;
    return (const char **) retval;
} /* MOJOCRASH_platform_load_reports */


void MOJOCRASH_platform_delete_report(const char *appname, const int idx)
{
    unlink(loaded_reports[idx]);
} /* MOJOCRASH_platform_delete_report */


void MOJOCRASH_platform_free_reports(const char **reports, const int total)
{
    int i;
    for (i = 0; i < total; i++)
    {
        free((void *) reports[i]);
        free(loaded_reports[i]);
    } /* for */

    free(reports);
    free(loaded_reports);
    loaded_reports = NULL;
} /* MOJOCRASH_platform_free_reports */


typedef struct DnsResolve
{
    char *host;
    int port;
    pthread_mutex_t mutex;
    struct addrinfo *addr;
    volatile int status;
    volatile int app_done;
} DnsResolve;


static void free_dns(DnsResolve *dns)
{
    pthread_mutex_destroy(&dns->mutex);
    if (dns->addr != NULL)
        freeaddrinfo(dns->addr);
    free(dns->host);
    free(dns);
} /* free_dns */


static void *dns_resolver_thread(void *_dns)
{
    char portstr[64];
    int app_done = 0;
    DnsResolve *dns = (DnsResolve *) _dns;
    int rc;

    MOJOCRASH_LongToString(dns->port, portstr);

    /* this blocks, hence all the thread tapdancing. */
    rc = getaddrinfo(dns->host, portstr, NULL, &dns->addr);

    pthread_mutex_lock(&dns->mutex);
    dns->status = (rc == 0) ? 1 : -1;
    app_done = dns->app_done;
    pthread_mutex_unlock(&dns->mutex);

    /* we free if the app is done, otherwise, app will do it. */
    if (app_done)
        free_dns(dns);

    return NULL;  /* we're detached anyhow. */
} /* dns_resolve */


void *MOJOCRASH_platform_begin_dns(const char *host, const int port)
{
    DnsResolve *retval = NULL;
    int mutex_initted = 0;
    pthread_t thread;

    retval = (DnsResolve *) malloc(sizeof (DnsResolve));
    if (retval == NULL)
        goto begin_dns_failed;

    retval->host = (char *) malloc(strlen(host) + 1);
    if (retval->host == NULL)
        goto begin_dns_failed;

    if (pthread_mutex_init(&retval->mutex, NULL) != 0)
        goto begin_dns_failed;

    mutex_initted = 1;
    strcpy(retval->host, host);
    retval->port = port;
    retval->addr = NULL;
    retval->status = 0;
    retval->app_done = 0;

    if (pthread_create(&thread, NULL, dns_resolver_thread, retval) != 0)
        goto begin_dns_failed;
    pthread_detach(thread);

    return retval;

begin_dns_failed:
    if (retval != NULL)
    {
        if (mutex_initted)
            pthread_mutex_destroy(&retval->mutex);
        free(retval->host);
        free(retval);
    } /* if */
    return NULL;
} /* MOJOCRASH_platform_begin_dns */


int MOJOCRASH_platform_check_dns(void *_dns)
{
    DnsResolve *dns = (DnsResolve *) _dns;
    int retval;
    pthread_mutex_lock(&dns->mutex);
    retval = dns->status;
    pthread_mutex_unlock(&dns->mutex);
    return retval;
} /* MOJOCRASH_platform_check_dns */


void MOJOCRASH_platform_free_dns(void *_dns)
{
    int thread_done = 0;
    DnsResolve *dns = (DnsResolve *) _dns;
    if (dns == NULL)
        return;

    pthread_mutex_lock(&dns->mutex);
    dns->app_done = 1;
    thread_done = (dns->status != 0);
    pthread_mutex_unlock(&dns->mutex);

    /* we free if the thread is done, otherwise, thread will do it. */
    if (thread_done)
        free_dns(dns);
} /* MOJOCRASH_platform_free_dns */


void *MOJOCRASH_platform_open_socket(void *_dns)
{
    DnsResolve *dns = (DnsResolve *) _dns;
    const struct addrinfo *addr = NULL;
    int *retval = NULL;
    int flags = 0;
    int fd = -1;
    int success = 0;

    if (MOJOCRASH_platform_check_dns(dns) != 1)
        return NULL;  /* dns resolve isn't done or it failed. */

    retval = (int *) malloc(sizeof (int));
    if (retval == NULL)
        return NULL;

    for (addr = dns->addr; addr != NULL; addr = addr->ai_next)
    {
        if (fd != -1)
            close(fd);

        fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

        if (fd == -1)
            continue;
        else if ((flags = fcntl(fd, F_GETFL, 0)) < 0)
            continue;
        else if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
            continue;
        else if (connect(fd, addr->ai_addr, addr->ai_addrlen) == -1)
        {
            if (errno != EINPROGRESS)
                continue;
        } /* if */

        success = 1;
        break;
    } /* for */

    if (success)
        *retval = fd;
    else
    {
        if (fd != -1)
            close(fd);
        free(retval);
        retval = NULL;
    } /* else */

    return retval;
} /* MOJOCRASH_platform_open_socket */


int MOJOCRASH_platform_check_socket(void *sock)
{
    const int fd = *((int *) sock);
    fd_set wfds;
    struct timeval tv;
    int retval;

    /* one handle, no wait. */
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    retval = select(fd+1, NULL, &wfds, NULL, &tv);
    return ((retval > 0) && (FD_ISSET(fd, &wfds) != 0));
} /* MOJOCRASH_platform_check_socket */


void MOJOCRASH_platform_close_socket(void *sock)
{
    close(*((int *) sock));
    free(sock);
} /* MOJOCRASH_platform_close_socket */


int MOJOCRASH_platform_read_socket(void *sock, char *buf, const int l)
{
    const int fd = *((int *) sock);
    const int retval = (int) recv(fd, buf, l, 0);
    if (retval < 0)
        return (errno == EAGAIN) ? -2 : -1;
    return retval;
} /* MOJOCRASH_platform_read_socket */


int MOJOCRASH_platform_write_socket(void *sock, const char *buf, const int l)
{
    const int fd = *((int *) sock);
    const int retval = (int) send(fd, buf, l, 0);
    if (retval < 0)
        return (errno == EAGAIN) ? -2 : -1;
    return retval;
} /* MOJOCRASH_platform_write_socket */


int MOJOCRASH_platform_init(void)
{
    gettimeofday(&starttime, NULL);
    return MOJOCRASH_unix_init();
} /* MOJOCRASH_platform_init */


#endif /* MOJOCRASH_PLATFORM_UNIX */

/* end of mojocrash_unix.c ... */

