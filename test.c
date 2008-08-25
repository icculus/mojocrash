#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "mojocrash.h"

#define APPNAME "MyTestCrashApp"
#define APPVERSION "1.0"
#define REPORTURL "http://127.0.0.1/test/crashreport.php"

#ifdef _WINDOWS
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
static inline void doSleep(const int ms) { Sleep(ms); }
#else
static inline void doSleep(const int ms) { usleep(ms * 1000); }
#endif

void f4() { printf("crashing!\n"); fflush(stdout); abort(); } //*((int *)0) = 0; }
void f3() { f4(); }
void f2() { f3(); }
void f1() { f2(); }

void crash(void)
{
    if (!MOJOCRASH_install(APPNAME, APPVERSION, NULL))
        printf("failed to install!\n");
    else
        f1();
} /* crash */


static int report_gui_init(void)
{
    printf("Called %s\n", __FUNCTION__);
    return 1;
} /* report_gui_init */

static MOJOCRASH_GuiShowValue report_gui_show(const char **reports,
                                              const int total)
{
    int i;
    printf("Called %s(%p, %d)\n", __FUNCTION__, reports, total);
    printf("Here are %d reports:\n\n", total);
    for (i = 0; i < total; i++)
        printf("########################################\n%s\n\n", reports[i]);

    while (1)
    {
        char buf[64];
        char *ptr;

        printf("Send them? [yes/no/later]\n> ");
        if (!fgets(buf, sizeof (buf), stdin))
            return MOJOCRASH_GUISHOW_IGNORE;

        ptr = buf + (strlen(buf) - 1);
        while (ptr > buf)
        {
            if ((*ptr == '\n') || (*ptr == '\r'))
                *(ptr--) = '\0';
            else
                break;
        } /* while */

        if (strcmp(buf, "yes") == 0)
            return MOJOCRASH_GUISHOW_SEND;
        else if (strcmp(buf, "no") == 0)
            return MOJOCRASH_GUISHOW_REJECT;
        else if (strcmp(buf, "later") == 0)
            return MOJOCRASH_GUISHOW_IGNORE;
    } /* while */

    return MOJOCRASH_GUISHOW_IGNORE;
} /* report_gui_show */

static int report_gui_status(const char *statustext, int percent)
{
    static char buf[1024];
    static int lastpct = -1;
    if ((strcmp(buf, statustext) != 0) || (percent != lastpct))
    {
        lastpct = percent;
        strcpy(buf, statustext);
        printf("Called %s('%s', %d)\n", __FUNCTION__, statustext, percent);
    } /* if */

    doSleep(10);
    return 1;
} /* report_gui_status */

static void report_gui_quit(const int success, const char *statustext)
{
    printf("Called %s(%d, '%s')\n", __FUNCTION__, success, statustext);
} /* report_gui_quit */


void report(void)
{
    MOJOCRASH_report_hooks hooks;
    memset(&hooks, '\0', sizeof (hooks));
    hooks.gui_init = report_gui_init;
    hooks.gui_show = report_gui_show;
    hooks.gui_status = report_gui_status;
    hooks.gui_quit = report_gui_quit;
    MOJOCRASH_report(APPNAME, REPORTURL, &hooks);
} /* report */


int main(int argc, char **argv)
{
    if ((argv[1]) && (strcmp(argv[1], "report") == 0))
        report();
    else
        crash();

    return 0;
} /* main */

/* end of test.c ... */



