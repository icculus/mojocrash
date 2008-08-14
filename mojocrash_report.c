#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_internal.h"

typedef struct
{
    int done;
    const char *status;
    int percent;
    const MOJOCRASH_report_hooks *h;
    int port;
    const char *app;
    const char **reports;
    int total;
    const char *url;
    char host[128];
    void *socket;
    void *resolved;
} SendReportData;


static void set_send_status(SendReportData *data, const char *status,
                            const int percent, const int done)
{
    if (!data->done)
    {
        data->done = done;
        data->status = status;
        data->percent = percent;
    } /* if */

    if ((!data->h->gui_status(data->status, data->percent)) && (!data->done))
        set_send_status(data, "Canceled by user.", 100, -1);
} /* set_send_status */


static int split_url(char *url, char **prot, char **user, char **pass,
                     char **host, char **port, char **path)
{
    char *ptr;
    char *ptr2;

    *prot = *user = *pass = *host = *port = *path = NULL;

    ptr = strchr(url, ':');
    if (ptr == NULL)
        return 0;

    if ((ptr[1] != '/') || (ptr[2] != '/'))
        return 0;

    *prot = url;
    *ptr = '\0';
    ptr += 3;  /* skip :// */

    ptr2 = strchr(ptr, '/');
    if (ptr2 == NULL)
    {
        *path = "/";
        *host = ptr;
        return 1;
    } /* if */

    *ptr2 = '\0';
    *path = ptr2 + 1;

    *host = ptr;
    ptr = strchr(*host, '@');
    if (ptr != NULL)
    {
        *ptr = '\0';
        *user = *host;
        *host = ptr + 1;
        ptr = strchr(*user, ':');
        if (ptr != NULL)
        {
            *ptr = '\0';
            *pass = ptr + 1;
        } /* if */
    } /* if */

    ptr = strchr(*host, ':');
    if (ptr != NULL)
    {
        *ptr = '\0';
        *port = ptr + 1;
    } /* if */

    return 0;
} /* split_url */


static int resolve_server_address(SendReportData *data)
{
    char url[256];
    char *prot = NULL;
    char *user = NULL;
    char *pass = NULL;
    char *host = NULL;
    char *port = NULL;
    char *path = NULL;

    if (data->resolved != NULL)
        return 1;  /* already done. */

    if (strlen(data->url) >= sizeof (url))
    {
        set_send_status(data, "BUG: URL buffer is too small.", 100, -1);
        return 0;
    } /* if */

    strcpy(url, data->url);
    if (!split_url(url, &prot, &user, &pass, &host, &port, &path))
    {
        set_send_status(data, "BUG: Invalid URL.", 100, -1);
        return 0;
    } /* if */

    if (strlen(host) >= sizeof (data->host))
    {
        set_send_status(data, "BUG: Hostname buffer is too small.", 100, -1);
        return 0;
    } /* if */
    strcpy(data->host, host);

    if ((prot == NULL) || (strcmp(prot, "http") != 0))
    {
        set_send_status(data, "BUG: Unsupported protocol.", 100, -1);
        return 0;
    } /* if */

    if ((user != NULL) || (pass != NULL))
    {
        set_send_status(data, "BUG: auth unsupported.", 100, -1);
        return 0;
    } /* if */

    if (port != NULL)
        data->port = (int) MOJOCRASH_StringToLong(port);
    else
        data->port = 80;

    data->resolved = MOJOCRASH_platform_begin_dns(data->host, data->port);
    while (1)
    {
        set_send_status(data, "Looking up hostname...", -1, 0);
        const int rc = MOJOCRASH_platform_check_dns(data->resolved);
        if (rc == 1)  /* done! */
            break;
        else if (rc == -1)  /* failed. */
        {
            MOJOCRASH_platform_free_dns(data->resolved);
            data->resolved = NULL;
            break;
        } /* else if */
    } /* while */

    if (data->resolved == NULL)
    {
        set_send_status(data, "Hostname lookup failed.", 100, -1);
        return 0;
    } /* if */

    return 1;
} /* resolve_server_address */


static int server_connect(SendReportData *data)
{
    /* !!! FIXME: proxies? */

    if (!resolve_server_address(data))
        return 0;

    data->socket = MOJOCRASH_platform_open_socket(data->resolved);
    while (1)
    {
        int rc;
        set_send_status(data, "Connecting...", data->percent, 0);
        rc = MOJOCRASH_platform_check_socket(data->socket);
        if (rc == 1)  /* done! */
            break;
        else if (rc == -1)  /* failed. */
        {
            MOJOCRASH_platform_close_socket(data->socket);
            data->socket = NULL;
            break;
        } /* else if */
    } /* while */

    if (data->socket == NULL)
    {
        set_send_status(data, "Connection failed.", 100, -1);
        return 0;
    } /* if */

    return 1;
} /* server_connect */


static inline int percent(const int x, const int total)
{
    return (int) ((((float) x) / ((float) total)) * 100.0f);
} /* percent */


static void send_all_reports(SendReportData *data)
{
    int i, j;
    int bytesout = 0;
    int bytesin = 0;

    for (i = 0; i < data->total; i++)
    {
        set_send_status(data, "Starting up...", -1, 0);
        bytesin += strlen(data->reports[i]);
    } /* for */

    if (bytesin == 0)  /* nothing to do? */
    {
        data->done = 1;
        return;
    } /* if */

    for (i = 0; (i < data->total) && (!data->done); i++)
    {
        char numcvt[32];
        char intro[1024];
        int introlen = 0;
        char *str = NULL;
        int avail = 0;
        const char *report = data->reports[i];
        const int len = strlen(report);
        int bw = 0;
        int rc = 0;

        if (len == 0)   /* nothing to do? */
        {
            data->h->delete_report(data->app, i);
            continue;
        } /* if */

        /* !!! FIXME: pipelining? */
        if (!server_connect(data))
            break;  /* error message was set elsewhere. */

        str = intro;
        avail = sizeof (intro);
        MOJOCRASH_ULongToString((unsigned long) len, numcvt);
        MOJOCRASH_StringAppend(&str, &avail, "POST / HTTP/1.1\n");
        MOJOCRASH_StringAppend(&str, &avail, "Host: ");
        MOJOCRASH_StringAppend(&str, &avail, data->host);
        MOJOCRASH_StringAppend(&str, &avail, "\n");
        MOJOCRASH_StringAppend(&str, &avail, "User-Agent: mojocrash/");
        MOJOCRASH_StringAppendMojoCrashVersion(&str, &avail);
        MOJOCRASH_StringAppend(&str, &avail, "\n");
        MOJOCRASH_StringAppend(&str, &avail, "Accept: text/plain\n");
        MOJOCRASH_StringAppend(&str, &avail, "Accept-Charset: utf-8\n");
        MOJOCRASH_StringAppend(&str, &avail, "Connection: close\n");
        MOJOCRASH_StringAppend(&str, &avail, "Content-Type: text/plain; charset=utf-8\n");
        MOJOCRASH_StringAppend(&str, &avail, "Content-Length:");
        MOJOCRASH_StringAppend(&str, &avail, numcvt);
        MOJOCRASH_StringAppend(&str, &avail, "\n\n");
        introlen = strlen(intro);

        bw = 0;
        while ((bw < introlen) && (!data->done))
        {
            set_send_status(data, "Sending...", percent(bytesout, bytesin), 0);
            rc = MOJOCRASH_platform_write_socket(data->socket, intro + bw,
                                                 introlen - bw);
            if (rc == -1)
                set_send_status(data, "Network write failure.", 100, -1);
            else if (rc == 0)
                set_send_status(data, "Connection lost.", 100, -1);
            else if (rc > 0)
                bw += rc;
        } /* while */

        bw = 0;
        while ((bw < len) && (!data->done))
        {
            set_send_status(data, "Sending...", percent(bytesout, bytesin), 0);
            rc = MOJOCRASH_platform_write_socket(data->socket, report + bw,
                                                 len - bw);
            if (rc == -1)
                set_send_status(data, "Network write failure.", 100, -1);
            else if (rc == 0)
                set_send_status(data, "Connection lost.", 100, -1);
            else if (rc > 0)
            {
                bw += rc;
                bytesout += rc;
            } /* if */
        } /* while */

        if (!data->done)  /* entire message was sent? */
        {
            int found = 0;
            int br = 0;
            char buf[256];

            while ((!found) && (!data->done))
            {
                set_send_status(data, "Getting reply...", data->percent, 0);
                rc = MOJOCRASH_platform_read_socket(data->socket, buf + br,
                                                    sizeof (buf) - br);

                if (rc == -2)
                    continue;   /* would block, pump and try again. */
                else if (rc == -1)
                    set_send_status(data, "Read failure.", 100, -1);
                else if (rc == 0)
                    set_send_status(data, "Connection lost.", 100, -1);

                if (data->done)
                    break;

                for (j = 0; j < rc; j++)
                {
                    char *ptr = &buf[br + j];
                    const char ch = *ptr;
                    found = ((ch == '\r') || (ch == '\n'));
                    if (found)
                    {
                        *ptr = '\0';
                        break;
                    } /* if */
                } /* for */

                br += rc;
                if (br >= sizeof (buf))
                    break;  /* oh well. */
            } /* while */

            if (!found)
                set_send_status(data, "Bad response from server.", 100, -1);
            else /* we have a response line. Parse it. */
            {
                char *ptr = strchr(buf, ' ');
                if ((ptr != NULL) && (strncmp(buf, "HTTP/", 5) == 0))
                {
                    if (strncmp(ptr+1, "200 ", 4) == 0)
                        data->h->delete_report(data->app, i);  /* Accepted! */
                } /* if */
            } /* else */
        } /* if */

        /* !!! FIXME: pipelining? */
        MOJOCRASH_platform_close_socket(data->socket);
        data->socket = NULL;
    } /* for */

    if (data->socket)
        MOJOCRASH_platform_close_socket(data->socket);

    if (data->resolved)
        MOJOCRASH_platform_free_dns(data->resolved);
} /* send_all_reports */


static inline void delete_all_reports(const MOJOCRASH_report_hooks *h,
                                      const char *app, const int total)
{
    int i;
    for (i = 0; i < total; i++)
        h->delete_report(app, i);
} /* delete_all_reports */


static void handle_reports(const MOJOCRASH_report_hooks *h, const char *app,
                           const char **reports, const int total,
                           const char *url)
{
    int rc = 0;
    int success = 0;
    const char *status = NULL;

    if (!h->gui_init())
        return;

    rc = h->gui_show(reports, total);

    if (rc == 0)  /* 0 == refuse send. */
        delete_all_reports(h, app, total);

    else if (rc == 1)  /* 1 == confirm send. */
    {
        SendReportData data;
        data.done = 0;
        data.status = NULL;
        data.percent = -1;
        data.h = h;
        data.port = 0;
        data.app = app;
        data.reports = reports;
        data.total = total;
        data.url = url;
        data.host[0] = '\0';
        data.socket = NULL;
        data.resolved = NULL;
        send_all_reports(&data);
        if (!data.done)
            set_send_status(&data, "All done!", 100, 1);  /* we won. */
        success = (data.done > 0) ? 1 : -1;
        status = data.status;
    } /* else if */

    /* < 0 is GUI error, etc. Just try again later. */
    h->gui_quit(success, status);
} /* handle_report */


static void report_internal(const MOJOCRASH_report_hooks *h, const char *app,
                            const char *url)
{
    int total = 0;
    const char **reports = h->load_reports(app, &total);
    if (reports != NULL)
    {
        if (total > 0)
            handle_reports(h, app, reports, total, url);
        h->free_reports(reports, total);
    } /* if */
} /* report_internal */


static const char **defhook_load_reports(const char *appname, int *total)
{
    return MOJOCRASH_platform_load_reports(appname, total);
} /* defhook_load_reports */


static void defhook_delete_report(const char *appname, const int idx)
{
    return MOJOCRASH_platform_delete_report(appname, idx);
} /* defhook_delete_report */


static void defhook_free_reports(const char **reports, const int total)
{
    return MOJOCRASH_platform_free_reports(reports, total);
} /* defhook_free_reports */


static int defhook_gui_init(void)
{
    return 1;  /* oh well. */
} /* defhook_gui_init */


static int defhook_gui_show(const char **reports, const int total)
{
    return 1;  /* oh well. */
} /* defhook_gui_show */


static int defhook_gui_status(const char *statustext, int percent)
{
    return 1;  /* oh well. */
} /* defhook_gui_status */


static void defhook_gui_quit(const int success, const char *statustext)
{
    return;  /* oh well. */
} /* defhook_gui_quit */

static void init_report_hooks(const MOJOCRASH_report_hooks *in,
                              MOJOCRASH_report_hooks *out)
{
    #define INIT_HOOK(H) out->H = ((in && in->H) ? in->H : defhook_##H)
    INIT_HOOK(load_reports);
    INIT_HOOK(delete_report);
    INIT_HOOK(free_reports);
    INIT_HOOK(gui_init);
    INIT_HOOK(gui_show);
    INIT_HOOK(gui_status);
    INIT_HOOK(gui_quit);
    #undef INIT_HOOK
} /* init_report_hooks */


void MOJOCRASH_report(const char *appname, const char *url,
                      const MOJOCRASH_report_hooks *h)
{
    MOJOCRASH_report_hooks hooks;
    if (appname == NULL) return;
    if (url == NULL) return;
    init_report_hooks(h, &hooks);
    report_internal(&hooks, appname, url);
} /* MOJOCRASH_report */

/* end of mojocrash_report.c ... */

