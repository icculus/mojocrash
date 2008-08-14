#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "mojocrash.h"

#define APPNAME "MyTestCrashApp"
#define APPVERSION "1.0"
#define REPORTURL "http://icculus.org"

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

static int report_gui_show(const char **reports, const int total)
{
    printf("Called %s(%p, %d)\n", __FUNCTION__, reports, total);
    return 1;
} /* report_gui_show */

static int report_gui_status(const char *statustext, int percent)
{
    printf("Called %s('%s', %d)\n", __FUNCTION__, statustext, percent);
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



