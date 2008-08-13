#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_internal.h"

typedef struct
{
    int done;
    char status[128];
    int percent;
    const MOJOCRASH_report_hooks *h;
    const char *app;
    const char **reports;
    int total;
    const char *url;
    void *mutex;
} SendReportData;

static volatile SendReportData send_data;



static int split_url(char *url, char **prot, char **user, char **pass,
                     char **host, char **port, char **path)
{
    char *ptr;
    char *ptr2;

    prot = user = pass = host = port = path = NULL;

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
        *path = "";
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


static void *server_connect(const MOJOCRASH_report_hooks *h, const char *_url)
{
    /* !!! FIXME: proxies? */
    char url[256];
    char *prot = NULL;
    char *user = NULL;
    char *pass = NULL;
    char *host = NULL;
    char *port = NULL;
    char *path = NULL;
    int iport = 80;
    void *sock = NULL;

    if (strlen(_url) > sizeof (url))
        return NULL;

    strcpy(url, _url);
    if (!split_url(url, &prot, &user, &pass, &host, &port, &path))
        return NULL;

    if ((prot == NULL) || (strcmp(prot, "http") != 0))
        return NULL;

    if ((user != NULL) || (pass != NULL)) return NULL;  /* !!! FIXME */

    if (host == NULL)
        return NULL;

    if (path == NULL)
        return NULL;

    if (port != NULL)
        iport = (int) MOJOCRASH_StringToLong(port);

    sock = MOJOCRASH_platform_open_socket(host, iport);
    if (sock == NULL)
        return NULL;

    return sock;
} /* server_connect */


static inline int percent(const int x, const int total)
{
    return (int) ((((float) x) / ((float) total)) * 100.0f);
} /* percent */


static inline void set_send_status(const char *status, const int percent,
                                   const int done)
{
    MOJOCRASH_platform_lock_mutex(send_data.mutex);
    if (!send_data.done)
    {
        send_data.done = done;
        strcpy((char *) send_data.status, status);
        send_data.percent = percent;
    } /* if */
    MOJOCRASH_platform_unlock_mutex(send_data.mutex);
} /* set_send_status */


static inline void spin(void)
{
    MOJOCRASH_platform_lock_mutex(send_data.mutex);
    MOJOCRASH_platform_unlock_mutex(send_data.mutex);
} /* spin */


static void *send_all_reports(void *unused)
{
    const MOJOCRASH_report_hooks *h = send_data.h;
    const char *app = send_data.app;
    const char **reports = send_data.reports;
    const int total = send_data.total;
    const char *url = send_data.url;
    int i, j;
    int bytesout = 0;
    int bytesin = 0;

    (void) unused;  /* laziness wins: using a global. */

    for (i = 0; i < total; i++)
        bytesin += strlen(reports[i]);

    for (i = 0; (i < total) && (!send_data.done); i++)
    {
        const char *report = reports[i];
        const int len = strlen(report);
        void *sock = NULL;
        int bw = 0;
        int rc = 0;

        if (len == 0)   /* nothing to do? */
        {
            h->delete_report(app, i);
            continue;
        } /* if */

        /* !!! FIXME: pipelining? */
        set_send_status("Connecting...", -1, 0);
        sock = server_connect(h, url);
        if (sock == NULL)
        {
            set_send_status("Connection failed.", 100, -1);
            break;
        } /* if */

        while ((rc >= 0) && (bw < len) && (!send_data.done))
        {
            set_send_status("Sending...", percent(bytesout, bytesin), 0);
            rc = MOJOCRASH_platform_write_socket(sock, report + bw, len - bw);
            if (rc > 0)
            {
                bw += rc;
                bytesout += rc;
            } /* if */
        } /* while */

        if (rc >= 0)  /* entire message was sent? */
        {
            int found = 0;
            int br = 0;
            char buf[256];

            set_send_status("Getting reply...", percent(bytesout, bytesin), 0);
            while ((!found) && (!send_data.done))
            {
                rc = MOJOCRASH_platform_read_socket(sock, buf + br,
                                                    sizeof (buf) - br);

                if (rc < 0)
                {
                    set_send_status("Read failure.", 100, -1);
                    break;
                } /* if */

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

            if (found)  /* we have a response line. Parse it. */
            {
                char *ptr = strchr(buf, ' ');
                if ((ptr != NULL) && (strncmp(buf, "HTTP/", 5) == 0))
                {
                    if (strncmp(ptr+1, "200 ", 4) == 0)
                        h->delete_report(app, i);  /* Accepted! Delete it. */
                } /* if */
            } /* if */
        } /* if */

        /* !!! FIXME: pipelining? */
        set_send_status("Closing connection...", -1, 0);
        MOJOCRASH_platform_close_socket(sock);
    } /* for */

    if (!send_data.done)
        set_send_status("All done!", 100, 1);  /* tell main thread we won. */

    while (send_data.done != -2)
        spin();  /* spin until main thread acknowledges we're done. */

    send_data.done = -3;  /* tell main thread we're dying. */
    return NULL;
} /* send_all_reports */


static inline void delete_all_reports(const MOJOCRASH_report_hooks *h,
                                      const char *app, const int total)
{
    int i;
    for (i = 0; i < total; i++)
        h->delete_report(app, i);
} /* delete_all_reports */


static inline void *spin_report_thread(void)
{
    return MOJOCRASH_platform_spin_thread(send_all_reports, NULL);
} /* spin_report_thread */


static void handle_reports(const MOJOCRASH_report_hooks *h, const char *app,
                           const char **reports, const int total,
                           const char *url)
{
    int rc = 0;

    if (!h->gui_init())
        return;

    rc = h->gui_show(reports, total);
    if (rc == 0)  /* 0 == refuse send. */
        delete_all_reports(h, app, total);

    else if (rc == 1)  /* 1 == confirm send. */
    {
        void *thread = NULL;

        strcpy((char *) send_data.status, "Starting up...");
        send_data.done = 0;
        send_data.percent = -1;
        send_data.h = h;
        send_data.app = app;
        send_data.reports = reports;
        send_data.total = total;
        send_data.url = url;
        send_data.mutex = MOJOCRASH_platform_create_mutex();

        h->gui_status((char *) send_data.status, -1);

        if (send_data.mutex == NULL)
            h->gui_quit(0, "System error: couldn't create mutex.");
        else if ((thread = spin_report_thread()) == NULL)
            h->gui_quit(0, "System error: couldn't spin thread.");
        else
        {
            char status[sizeof (send_data.status)];
            int percent = -1;
            int done = 0;

            while (!done)
            {
                MOJOCRASH_platform_lock_mutex(send_data.mutex);
                done = send_data.done;
                percent = send_data.percent;
                strcpy(status, (const char *) send_data.status);
                MOJOCRASH_platform_unlock_mutex(send_data.mutex);

                if (!h->gui_status(status, percent))
                    set_send_status("Canceled by user.", 100, -1);
            } /* while */

            send_data.done = -2;  /* tell thread it's okay to die. */
            MOJOCRASH_platform_join_thread(thread);
            h->gui_quit((done > 0), status);
        } /* else */

        MOJOCRASH_platform_destroy_mutex(send_data.mutex);
    } /* else if */

    /* < 0 is GUI error, etc. Just try again later. */
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

