/**
 * MojoCrash; a problem-reporting tool.
 *
 * Documentation is in mojocrash.h. It's verbose, honest.  :)
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#define __MOJOCRASH_INTERNAL__ 1
#include "mojocrash_internal.h"

// !!! FIXME: avoid C runtime dependencies for these functions:
//   strlen
//   strcpy


static MOJOCRASH_hooks hooks;
static int initialized = 0;
static char appname[MOJOCRASH_MAX_APPNAME_STRING];
static char version[MOJOCRASH_MAX_VERSION_STRING];
static char url[MOJOCRASH_MAX_URL_STRING];




/* The hooks... */

static int defhook_install_crash_catcher(void (*catcher)(int sig))
{
    return MOJOCRASH_platform_install_crash_catcher(catcher);
} /* defhook_install_crash_catcher */

static int defhook_app_crash_catcher_preflight(int sig)
{
    return 0;  /* no-op by default, tell normal processing to go forward. */
} /* defhook_app_crash_catcher_preflight */

static int defhook_get_callstack(MOJOCRASH_get_callstack_callback callback)
{
    return MOJOCRASH_platform_get_callstack(callback);
} /* defhook_get_callstack */

static int defhook_get_objects(MOJOCRASH_get_objects_callback callback)
{
    return MOJOCRASH_platform_get_objects(callback);
} /* defhook_get_objects */

static const char *defhook_get_other_info(void)
{
    return NULL;  /* no-op by default. */
} /* defhook_get_other_info */

static int defhook_app_crash_catcher_postflight(int sig)
{
    return 0;  /* no-op by default, tell normal processing to go forward. */
} /* defhook_app_crash_catcher_post */

static void init_hooks(const MOJOCRASH_hooks *_hooks)
{
    #define INIT_HOOK(H) hooks.H = _hooks->H ? _hooks->H : defhook_##H;
    INIT_HOOK(install_crash_catcher);
    INIT_HOOK(app_crash_catcher_preflight);
    INIT_HOOK(get_callstack);
    INIT_HOOK(get_objects);
    INIT_HOOK(get_other_info);
    INIT_HOOK(app_crash_catcher_post);
    #undef INIT_HOOK
} /* init_hooks */


/* Entry point(s) ... */

int MOJOCRASH_install(const char *_appname, const char *_version,
                      const char *_url, const MOJOCRASH_hooks *_hooks)
{
    if (initialized)
        return 0;  /* don't double-call this! */

    /* sanity-check parameters. */
    if (strlen(_appname) >= MOJOCRASH_MAX_APPNAME_STRING) return 0;
    if (strlen(_version) >= MOJOCRASH_MAX_VERSION_STRING) return 0;
    if (strlen(_url) >= MOJOCRASH_MAX_URL_STRING) return 0;

    /* set up state. */
    strcpy(appname, _appname);
    strcpy(version, _version);
    strcpy(url, _url);
    init_hooks(_hooks);

    if (!MOJOCRASH_platform_init())
        return 0;

    initialized = 1;
    return 1;  /* success! */
} /* MOJOCRASH_install */

/* end of mojocrash.c ... */


