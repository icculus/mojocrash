#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_internal.h"

static volatile int reporting = 0;

typedef struct
{
    int done;
    int background;
    const char *status;
    int percent;
    const MOJOCRASH_report_hooks *h;
    int port;
    const char *app;
    const char **reports;
    int total;
    int bytesin;
    int bytesout;
    const char *url;
    int use_proxy;
    char host[128];
    char path[128];
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

    if (!data->background)
    {
        if ((!data->h->gui_status(data->status, data->percent))&&(!data->done))
        set_send_status(data, "Canceled by user.", 100, -1);
    } /* if */
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

    return 1;
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

    if (MOJOCRASH_platform_get_http_proxy(url, sizeof (url)))
        data->use_proxy = 1;
    else
    {
        if (strlen(data->url) >= sizeof (url))
        {
            set_send_status(data, "BUG: URL buffer is too small.", 100, -1);
            return 0;
        } /* if */
        strcpy(url, data->url);
    } /* else */

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

    if (strlen(path) >= sizeof (data->path))
    {
        set_send_status(data, "BUG: Path buffer is too small.", 100, -1);
        return 0;
    } /* if */
    strcpy(data->path, path);

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

    /* If we're already in a background thread, we can block here. */
    set_send_status(data, "Looking up hostname...", -1, 0);
    data->resolved = MOJOCRASH_platform_begin_dns(data->host, data->port,
                                                  data->background);

    while (data->resolved != NULL)
    {
        const int rc = MOJOCRASH_platform_check_dns(data->resolved);
        if (rc == 1)  /* done! */
            break;
        else if (rc == -1)  /* failed. */
        {
            MOJOCRASH_platform_free_dns(data->resolved);
            data->resolved = NULL;
        } /* else if */
        set_send_status(data, "Looking up hostname...", -1, 0);
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
    if (!resolve_server_address(data))
        return 0;

    /* If we're already in a background thread, we can block here. */
    set_send_status(data, "Connecting...", data->percent, 0);
    data->socket = MOJOCRASH_platform_open_socket(data->resolved,
                                                  data->background);

    while (data->socket != NULL)
    {
        const int rc = MOJOCRASH_platform_check_socket(data->socket);
        if (rc == 1)  /* done! */
            break;
        else if (rc == -1)  /* failed. */
        {
            MOJOCRASH_platform_close_socket(data->socket);
            data->socket = NULL;
        } /* else if */
        set_send_status(data, "Connecting...", data->percent, 0);
    } /* if */

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


static void write_string(SendReportData *data, const char *str, const int len,
                         const int updatepct)
{
    int bw = 0;
    int rc = 0;
    int pct = percent(data->bytesout, data->bytesin);

    while ((bw < len) && (!data->done))
    {
        set_send_status(data, "Sending...", pct, 0);
        rc = MOJOCRASH_platform_write_socket(data->socket, str+bw, len-bw);
        if (rc == -1)
            set_send_status(data, "Network write failure.", 100, -1);
        else if (rc == 0)
            set_send_status(data, "Connection lost.", 100, -1);
        else if (rc > 0)
        {
            bw += rc;
            if (updatepct)
            {
                data->bytesout += rc;
                pct = percent(data->bytesout, data->bytesin);
            } /* if */
        } /* if */
    } /* while */
} /* write_string */


static unsigned long MOJOCRASH_random_seed = 1;
static int MOJOCRASH_random(const int top)
{
    /* !!! FIXME: this is NOT a good algorithm. */
    MOJOCRASH_random_seed = (MOJOCRASH_random_seed * 1103515245) + 12345;
    return (int) ((unsigned)(MOJOCRASH_random_seed / 65536) % top);
} /* MOJOCRASH_random */


static void send_all_reports_worker_internal(SendReportData *data)
{
    int i, j;

    for (i = 0; i < data->total; i++)
    {
        set_send_status(data, "Starting up...", -1, 0);
        data->bytesin += strlen(data->reports[i]);
    } /* for */

    if (data->bytesin == 0)  /* nothing to do? */
        return;

    if (!MOJOCRASH_platform_init_network())
    {
        set_send_status(data, "Network init failed", 100, -1);
        return;
    } /* if */

    for (i = 0; (i < data->total) && (!data->done); i++)
    {
        char numcvt[32];
        char intro[1024];
        char boundary[32];
        char prologue[256];
        char epilogue[64];
        char *str = NULL;
        int avail = 0;
        const char *report = data->reports[i];
        const int len = strlen(report);
        int rc = 0;
        int x = 0;

        if (len == 0)   /* nothing to do? */
        {
            data->h->delete_report(data->app, i);
            continue;
        } /* if */

        /* !!! FIXME: pipelining? */
        if (!server_connect(data))
            break;  /* error message was set elsewhere. */

        for (x = 0; x < sizeof (boundary) - 1; x++)
        {
            if (MOJOCRASH_random(10) % 2)
                boundary[x] = ('0' + MOJOCRASH_random(10));
            else
                boundary[x] = ('a' + MOJOCRASH_random(26));
        } /* for */
        boundary[x] = '\0';

        str = prologue;
        avail = sizeof (prologue);
        MOJOCRASH_StringAppend(&str, &avail, "--");
        MOJOCRASH_StringAppend(&str, &avail, boundary);
        MOJOCRASH_StringAppend(&str, &avail,
            "\r\n"
            "Content-Disposition: form-data; name=\"crash\";"
            " filename=\"crash-report.txt\"\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n\r\n"
        );

        str = epilogue;
        avail = sizeof (epilogue);
        MOJOCRASH_StringAppend(&str, &avail, "\r\n--");
        MOJOCRASH_StringAppend(&str, &avail, boundary);
        MOJOCRASH_StringAppend(&str, &avail, "--\r\n");

        MOJOCRASH_ULongToString(
            ((unsigned long) len) + strlen(prologue) + strlen(epilogue),
            numcvt);

        str = intro;
        avail = sizeof (intro);
        if (data->use_proxy)
        {
            MOJOCRASH_StringAppend(&str, &avail, "POST ");
            MOJOCRASH_StringAppend(&str, &avail, data->url);
            MOJOCRASH_StringAppend(&str, &avail, " HTTP/1.1\r\n");
        } /* if */
        else
        {
            MOJOCRASH_StringAppend(&str, &avail, "POST /");
            MOJOCRASH_StringAppend(&str, &avail, data->path);
            MOJOCRASH_StringAppend(&str, &avail, " HTTP/1.1\r\n");
            MOJOCRASH_StringAppend(&str, &avail, "Host: ");
            MOJOCRASH_StringAppend(&str, &avail, data->host);
            MOJOCRASH_StringAppend(&str, &avail, "\r\n");
        } /* else */
        MOJOCRASH_StringAppend(&str, &avail, "User-Agent: mojocrash/");
        MOJOCRASH_StringAppendMojoCrashVersion(&str, &avail);
        MOJOCRASH_StringAppend(&str, &avail, "\r\n");
        MOJOCRASH_StringAppend(&str, &avail, "Accept: text/plain\r\n");
        MOJOCRASH_StringAppend(&str, &avail, "Accept-Charset: utf-8\r\n");
        MOJOCRASH_StringAppend(&str, &avail, "Connection: close\r\n");
        MOJOCRASH_StringAppend(&str, &avail,
            "Content-Type: multipart/form-data; boundary=\""
        );
        MOJOCRASH_StringAppend(&str, &avail, boundary);
        MOJOCRASH_StringAppend(&str, &avail, "\"\r\n");
        MOJOCRASH_StringAppend(&str, &avail, "Content-Length: ");
        MOJOCRASH_StringAppend(&str, &avail, numcvt);
        MOJOCRASH_StringAppend(&str, &avail, "\r\n\r\n");

        write_string(data, intro, strlen(intro), 0);
        write_string(data, prologue, strlen(prologue), 0);
        write_string(data, report, len, 1);
        write_string(data, epilogue, strlen(epilogue), 0);

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
                    else
                        set_send_status(data, "Rejected by server.", 100, -1);
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

    MOJOCRASH_platform_deinit_network();
} /* send_all_reports_worker_internal */


typedef struct
{
    volatile int copied;
    const MOJOCRASH_report_hooks *h;
    const char *app;
    const char **reports;
    int total;
    const char *url;
    int background;
} SendReportWorkerData;

static const char *send_all_reports_worker(SendReportWorkerData *workerdata,
                                           int *success)
{
    SendReportData data;
    data.done = 0;
    data.background = workerdata->background;
    data.status = NULL;
    data.percent = -1;
    data.h = workerdata->h;
    data.port = 0;
    data.app = workerdata->app;
    data.reports = workerdata->reports;
    data.total = workerdata->total;
    data.bytesin = 0;
    data.bytesout = 0;
    data.url = workerdata->url;
    data.use_proxy = 0;
    data.host[0] = '\0';
    data.path[0] = '\0';
    data.socket = NULL;
    data.resolved = NULL;

    workerdata->copied = 1;
    send_all_reports_worker_internal(&data);
    if (!data.done)
        set_send_status(&data, "All done!", 100, 1);  /* we won. */
    data.h->free_reports(data.reports, data.total);
    reporting = 0;
    *success = (data.done > 0) ? 1 : 0;
    return data.status;
} /* send_all_reports_worker */


static void send_all_reports_thread(void *_data)
{
    SendReportWorkerData *workerdata = (SendReportWorkerData *) _data;
    int ignored = 0;
    send_all_reports_worker(workerdata, &ignored);
} /* send_all_reports_thread */


static const char *send_all_reports(const MOJOCRASH_report_hooks *h,
                                    const char *app, const char **reports,
                                    const int total, const char *url,
                                    const int background, int *success)
{
    SendReportWorkerData workerdata;
    workerdata.copied = 0;
    workerdata.h = h;
    workerdata.app = app;
    workerdata.reports = reports;
    workerdata.total = total;
    workerdata.url = url;
    workerdata.background = background;

    if (!background)
        return send_all_reports_worker(&workerdata, success);

    if (!MOJOCRASH_platform_spin_thread(send_all_reports_thread, &workerdata))
    {
        *success = 0;
        return "Failed to spin thread";
    } /* if */

    while (!workerdata.copied) { /* spin */ }
    *success = 1;
    return "Running in background";
} /* send_all_reports */


static inline const char *delete_all_reports(const MOJOCRASH_report_hooks *h,
                                             const char *app,
                                             const char **reports,
                                             const int total, int *success)
{
    int i;
    for (i = 0; i < total; i++)
        h->delete_report(app, i);
    h->free_reports(reports, total);
    reporting = 0;
    *success = 1;
    return "Reports deleted";
} /* delete_all_reports */


static void handle_reports(const MOJOCRASH_report_hooks *h, const char *app,
                           const char **reports, const int total,
                           const char *url)
{
    int success = 0;
    const char *status = NULL;

    if (!h->gui_init())
        return;

    switch (h->gui_show(reports, total))
    {
        case MOJOCRASH_GUISHOW_REJECT:
            status = delete_all_reports(h, app, reports, total, &success);
            break;
        case MOJOCRASH_GUISHOW_SEND:
            status = send_all_reports(h, app, reports, total, url, 0, &success);
            break;
        case MOJOCRASH_GUISHOW_SEND_BACKGROUND:
            status = send_all_reports(h, app, reports, total, url, 1, &success);
            break;
        case MOJOCRASH_GUISHOW_IGNORE:
            success = -1;   /* GUI error, etc. Just try again later. */
            h->free_reports(reports, total);
            reporting = 0;
            break;
    } /* switch */

    h->gui_quit(success, status);
} /* handle_report */


static void report_internal(const MOJOCRASH_report_hooks *h, const char *app,
                            const char *url)
{
    int total = 0;
    const char **reports = NULL;

    reports = h->load_reports(app, &total);
    if (reports != NULL)
    {
        reporting = 1;
        if (total > 0)
            handle_reports(h, app, reports, total, url);
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


static MOJOCRASH_GuiShowValue defhook_gui_show(const char **reports,
                                               const int total)
{
    return MOJOCRASH_GUISHOW_SEND;  /* oh well. */
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
    static MOJOCRASH_report_hooks hooks;
    if (reporting) return;
    if (appname == NULL) return;
    if (url == NULL) return;
    if (!MOJOCRASH_platform_init()) return;
    MOJOCRASH_random_seed = MOJOCRASH_platform_now();
    init_report_hooks(h, &hooks);
    report_internal(&hooks, appname, url);
} /* MOJOCRASH_report */


int MOJOCRASH_reporting(void)
{
    return reporting;
} /* MOJOCRASH_reporting */

/* end of mojocrash_report.c ... */

