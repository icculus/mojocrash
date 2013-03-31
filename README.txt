
(This is a dump of the wiki where I kept notes on this; it's not 
necessarily up to date, or correct at this point, but this is the basic 
gist.)


"Creativity is hiding your sources."

I stole this idea verbatim from a chapter of Joel on Software. He stole 
it from Microsoft, Apple, Netscape Talkback, etc. 

  http://www.fogcreek.com/FogBugz/docs/30/UsingFogBUGZtoGetCrashRep.html


Rationale

 * Bad end-users don't report bugs.
 * Really bad end-users report bugs badly.
 * Most crashes happen at the same 3-5 points in a program.
 * Steve Ballmer claims 20% of the bugs are 80% of the crashes, and 1% of 
   the bugs are 50% of the crashes.
 * Keeping track of incidents individually takes an inordinate amount of time.


Basic Overview

MojoCrash is statically linked to an app, activates when there's a 
crash, and eventually reports the bug. MojoCrash takes the user (mostly) 
out of bug reporting on the client side. MojoCrash takes the developer 
(mostly) out of bug management on the server side.


Basic Vocabulary

Strings: All strings are UTF-8 encoded unless otherwise specified, but 
for all intents and purposes, this mostly means ASCII in this project. 
All newlines are ASCII 10 (0x0A...'\n'), like Unix, regardless of client 
or server platform, unless otherwise specified.

Client: the application on the end-user's side that reports crashes.

Server: the application on the developer's side that receives crash 
reports.

Hook: A function pointer supplied by the app. A callback that may or may 
not be implemented.

Trigger: Something that activates MojoCrash; an exception, GPF, 
segfault, assertion failure, etc.

Catcher: Function that handles a trigger.

Reporter App: External program on end-user's machine that actually deals 
with user input and transmission of crash info to server.


High-level Goals

 * Catch crashes and bugs in the field.
 * Feed them to developers with minimal user interaction.
 * Sort them into meaningful data with minimal developer interaction.


High-level implementation

Server-side: Linux, Apache, PHP/Perl, Bugzilla. This must use these 
components, for ease of migration between platforms and avoidance of 
vendor lock-in. All the usual good Open Source mojo.

Client-side: C-callable library separate from the application, although 
probably statically linked, and also a separate application that does 
the heavy lifting.

Client catches a crash, logs it to disk. A new process runs (the 
"reporter app") to handle the information. This is necessary since a 
crashing app is in an unknown state and shouldn't do more in the process 
than absolutely necessary; it also helps separate the functionality and 
remove assumptions about how most of the client code needs to behave. 
While it will always be a separate process, please note that the 
crashing program may also be the reporter app; optionally, it may handle 
the reporting on the next run of the program.

Reporter app sees log, confers with user, sends data as HTTP POST to 
server, server tosses it to right place for developers to examine.


Client Library

There is only one entry point into MojoCrash for the client to use:

extern "C" int MOJOCRASH_install(const char *appname, const char *version, const MOJOCRASH_hooks *hooks);

The client-side code is initialized via MOJOCRASH_install(), and 
interacts with the program through various hooks, all of which are 
optional (you may specify NULL for any hook you don't want to implement, 
or pass a NULL pointer for the struct if you don't need any of them). 
Once MOJOCRASH_install() is called, the library will do its work if 
there is a segmentation fault, assertion failure, etc. In a normal, 
error-free run of the application, MojoCrash does nothing at all after 
returning from this call. There is no uninstall: once MojoCrash is 
ready, it stays ready until the process terminates.

The application hooks are fed to MojoCrash through the MOJOCRASH_hooks 
structure. This struct is a collection of function pointers. Any pointer 
can be NULL, including the pointer you supply for the entire struct, and 
MojoCrash will use a reasonable default.

It's worth noting that you can manipulate MojoCrash through its hooks to 
various ends that have nothing to do with the normal concept of a 
"crash" ... for example, you can hook it up to only respond to assertion 
failures or other special conditions instead of the normal segfault/GPF 
type of crash, you can wire it to manage a scripting language or virtual 
machine instead of the process itself, and other uses we haven't yet 
conceived. Override individual pieces, or all of them, to taste.

Also note that if you're handling real crashes, you might be operating 
with a corrupted memory heap, or with random portions of your process's 
memory overwritten. This is an inexact science that demands a degree of 
hope, but you shouldn't aggravate the situation:

Try to avoid malloc(), free(), etc in any hook other than 
install_crash_catcher(), as your heap may be damaged during a legitimate 
crash. Use static memory, and preallocate what you reasonably can.

Try to avoid touching any application-specific data; you should assume 
it is unreliable, and the pointer you are dereferencing may be invalid, 
causing a crash.

Try to precalculate data you might want to report during a crash, 
instead of processing it when the system is in an uncertain state.

Try to avoid grabbing mutexes; a different thread could have crashed 
while holding it, and you will deadlock.

Try to avoid unnecessary i/o; file handles could be corrupted or 
blocking.

Try to avoid blocking. Go down quickly.

Try to avoid doing ANYTHING except the bare minimum. All you really want 
to do here is get the minimal amount of useful data from the dying 
process and stash it somewhere. All the heavy lifting, including a user 
interface to apologize for the crash, will be done by a different 
process in a known state.

Obviously, if you are "crashing" a virtual machine, scripting language 
interpreter, or something else that's separate from the native process, 
you can do whatever you like in the hooks, as your process is not in an 
uncertain state.

It can not be stressed enough that, for all the complexity and following 
discussion of the hooking system, most applications can supply no hooks 
at all; the default, internal behaviour is precisely what most 
applications need.

The hooks structure looks like this:

 typedef struct MOJOCRASH_hooks
 {
     int (*install_crash_catcher)(MOJOCRASH_catcher catcher);
     int (*preflight)(int sig, int crash_count);
     int (*start_crashlog)(void);
     int (*new_crashlog_line)(const char *str);
     int (*get_callstack)(MOJOCRASH_get_callstack_callback callback);
     int (*get_objects)(MOJOCRASH_get_objects_callback callback);
     int (*get_etc)(MOJOCRASH_get_etc_callback callback);
     int (*end_crashlog)(void);
     int (*postflight)(void);
     void (*die)(void);
 } MOJOCRASH_hooks;

The install_crash_catcher hook lets the app manually install the 
catcher, in case they need to maintain a chain they've previously set 
up, or trigger MojoCrash in unusual ways, such as only when assert() 
fails, or whatnot. Most applications will leave this hook set to NULL, 
in which case MojoCrash will trigger in the normal ways for a given 
platform (SIGSEGV and other signals on Unix, GPF handler on Windows, 
etc). Returns zero on error, non-zero if trigger was 
installed...MOJOCRASH_install() will fail if this returns zero, but 
results are undefined if you really installed a catcher and reported 
failure. To be clear, if you specify this hook, MojoCrash will not 
install any sort of catchers on its own, and you must make sure that 
"catcher" will be called when there is a crash condition. Most other 
hooks are called in response to "catcher" being called (that is, when 
MojoCrash is "triggered"). This hook is called during 
MOJOCRASH_install().

The preflight hook allows an application to perform some act when 
MojoCrash is triggered. This is called first inside the internal 
MojoCrash catcher. There are two arguments to this hook: the signal 
number related to the crash (this would be something like 11 (SIGSEGV) 
on Unix, or 13 (GPF) on Windows, or some app-specific value, etc), and 
the number of recursions into the catcher (first crash is 1, if there's 
a crash in the crash handler, it's 2, etc...this number decrements each 
time the catcher finishes, so unless you have a double-fault or whatnot, 
this should be 1, even if you run the handler multiple times). Return 
non-zero to signify that you don't want MojoCrash to continue handling 
this trigger. If you return zero, the internal catcher will run, which 
calls other hooks, etc. In a game, this might be a good place to release 
the video display if you've got a fullscreen context...please consider 
that the system is in an uncertain state when there's a crash, so it 
would be wise to do as little work in this function as possible. If this 
is left as NULL, MojoCrash does the following, depending on the value of 
crash_count:

  * returns 1 (just go forward to the usual handler).
  * returns 0 (just try to stop crash handler immediately).
  * Calls the die() hook. If that returns, returns 0.
  * Tries to gently terminate the process (like exit(); static destructors 
    may run). If that returns, returns 0.
  * Tries to forcefully terminate the process (like _exit(); let the OS sort 
    it all out). If that returns, returns 0.
  * Just returns zero from there out. Nothing else you can do here, really.

The start_crashlog hook lets you create a crashlog manually. If this is 
NULL, MojoCrash will write a standard file to a reasonable location for 
future examination. If you want to stash the data somewhere else, or 
modify it en route, you should override this hook. Please note that 
you'll also need to override the end_crashlog() and new_crashlog_line() 
hooks, as the default hooks operate on an internal file handle created 
in the default start_crashlog(). The default behaviour also chooses a 
unique, unused filename, and you will probably have to do likewise. 
Otherwise, you'll get weird results, if not a double-fault. Also, please 
note that the reporter app will expect several details of the default 
behaviour, but you can override that as necessary, too. This hook does 
not return the file handle, if there's a file handle at all; please keep 
it in a static variable that your hooks can access. This hook returns 
zero if there's an error (which will cause the crash handler to call the 
die() hook and then return if the process still exists). If the hook 
returns non-zero, the crash handler continues. Please note that if the 
crash handler fails at any point, either through a hook reporting 
failure or any other cause, we may NOT call end_crashlog(). Be prepared 
for the process to terminate at any time, even outside of your control, 
with the crashlog in any state. The reporter app will reject and delete 
incomplete crashlogs.

The new_crashlog_line hook is called for each line of text going into 
the crashlog. It may be called at any time after the start_crashlog() 
hook sucessfully returns. The parameter to this hook is a 
null-terminated C string in UTF-8 encoding. If this is NULL, MojoCrash 
will write the line to the file it created in the start_crashlog() hook, 
so if you implement this hook, you will need to implement the 
start_crashlog() hook, too, or strange things will happen! This hook 
returns zero if there's an error (which will cause the crash handler to 
call the die() hook and then return if the process still exists). If the 
hook returns non-zero, the crash handler continues.

The get_callstack hook allows the application to specify the crashing 
process's callstack. This is called after the start_crashlog() hook, and 
supplies a callback that will eventually call the new_crashlog_line() 
hook. If this is NULL, MojoCrash will use the platform's facilities to 
determine the current callstack, which is usually what you want. 
However, some apps maintain their own callstack information that has 
more clarity than the junk the OS supplies, and others might want to 
supply a callstack relating to a scripting language or virtual machine 
and not the application process itself. This hook has a callback 
function as its only parameter; this is to prevent the need to allocate 
a buffer of arbitrary size for the callstack. There is no limit on the 
size of callstacks, but it's not unreasonable to put an upper cap on 
them, in case the system ends up in a loop (a corrupted stack is not 
unheard of during crashes, after all). Each call to the callback 
function reports a void pointer that represents the next frame pointer 
on the callstack, starting at the top (so the crashing function, or 
perhaps the crash handler, should be the first thing passed to the 
callback, and something like main() should be the last). This hook does 
not concern itself with the actual function names, source files or line 
numbers of the callstack, just the addresses. This hook returns zero if 
there's an error (which will cause the crash handler to call the die() 
hook and then return if the process still exists). If the hook returns 
non-zero, the crash handler continues.

The get_etc hook allows the application to specify app-specific 
information. This is called sometime after the start_crashlog() hook, 
and supplies a callback that will eventually call the 
new_crashlog_line() hook. If this is NULL, MojoCrash will do nothing 
here (any "etc" information reported is optional and specific to the 
app), which is usually what you want. The crash handler will report 
other things about the system (OS version, processor type, etc) separate 
from any of the hooks, so you might not need to dig up other information 
here anyhow. This hook has a callback function as its only parameter; 
this is to prevent the need to allocate a buffer of arbitrary size for 
the list of objects. There is no limit on the number of "etc" items. 
Each call to the callback function reports key and value pairs, each a 
string. For example, you might pass the key "GL_VERSION" and the value 
"2.1.1 NVIDIA 100.14.19" if you wanted to know what OpenGL's 
glGetString(GL_VERSION) call reports on the crashing machine (please 
preallocate this string, though, since you ARE crashing at this point, 
and the GL isn't necessarily reliable!). There are no conventions on 
what format the key and value must conform to, except that both need to 
be null-terminated UTF-8 strings. This hook returns zero if there's an 
error (which will cause the crash handler to call the die() hook and 
then return if the process still exists). If the hook returns non-zero, 
the crash handler continues.

The end_crashlog hook is called if (and only if!) the crash handler has 
successfully run completely through all other hooks involved in 
producing data for the crashlog, and should be used to finalize what was 
started in start_crashlog(). If this is NULL, MojoCrash will close the 
internal file handle it created and ensure the crashlog data is flushed 
to disk. You'll need to implement all of start_crashlog(), 
new_crashlog_line() and end_crashlog(), or none of them. In most cases, 
the default hooks are fine (and the reporter app expects the default 
behaviour unless you override that, too). Please note that if there's a 
failure in the crash handler (one of the hooks returns zero, etc), this 
hook is NOT called. If this is going to be a serious problem, be 
prepared to clean up in the die() hook, but you should assume that the 
process could terminate without calling either (which will mean the OS 
will clean up open files, etc, instead of you). This hook returns zero 
if there's an error (which will cause the crash handler to call the 
die() hook and then return if the process still exists). If the hook 
returns non-zero, the crash handler continues.

The postflight hook runs after the end_crashlog() hook returns 
successfully. This allows you to do something after MojoCrash has logged 
the error to disk for transmission, such as your own cleanup (but 
remember, don't do more than necessary: you are crashing, after all!). 
If this hook is NULL, it's a no-op internally. You should return 
non-zero on success from this hook, and zero if there's an error, but 
this is merely for future expansion: in either case, MojoCrash will call 
the die() hook and then return if the process still exists.

The die hook can be called at any time. This is called by the crash 
handler before returning, either when all other hooks have returned 
successfully or when one of the hooks reports failure. If this returns, 
the crash handler will return, too, so you want to make sure it does not 
return for legitimate program crashes; in these cases, you may want to 
terminate the process: throw a C++ exception, use longjmp(), or 
otherwise alter control flow. If your "crashes" are cleanly separated 
from the OS process, such as an assertion in a scripting language or a 
problem with a virtual machine, etc, you may want to override this hook 
to do nothing, and then regain control normally when the crash handler 
returns. If this hook is NULL, the crash handler will try to gently kill 
the process (this is usually equivalent to the C runtime's exit() 
function, not the _exit() one, so static destructors might now run).

To summarize the chain of events for an unexceptional case:

  * Application calls MOJOCRASH_install().
  * MojoCrash stores hooks, replaces NULL hooks with internal functions.
  * install_crash_catcher hook runs.
  * MOJOCRASH_install() returns.
  * Application later crashes for whatever reason, triggering the crash catcher.
  * Control jumps to MojoCrash.
  * preflight hook runs.
  * start_crashlog hook runs.
  * Some internal data gets added to the log with new_crashlog_line hook.
  * get_objects hook runs, calling callback which calls new_crashlog_line hook.
  * get_callstack hook runs, calling callback which calls new_crashlog_line hook.
  * get_etc hook runs, calling callback which calls new_crashlog_line hook.
  * end_crashlog hook runs.
  * postflight hook runs.
  * die hook runs.
  * Application terminates.


Crash log format

(This information is still subject to change.)

The crashlog is a plain text file in UTF-8 encoding, using Unix newlines 
(ASCII 0x0D or '\n') for endlines. Here's an example log:

 # This is a log of a program crash. It is meant to help the
 #  developers debug the problem that caused the crash.
 #
 # This log was generated by software called "MojoCrash" but
 #  please note that MojoCrash did NOT cause the crash in the first 
 #  place. It only helped report the problem. Please do not contact the 
 #  MojoCrash developers about this problem, we didn't cause it and 
 #  probably haven't heard of the crashing program.
 #
 # If you need to contact someone about a bug, please contact the
 #  original vendor of your program instead. Thanks!

 MOJOCRASH 0.0.1
 CRASHLOG_VERSION 1
 APPLICATION_NAME TestApp
 APPLICATION_VERSION 1.0
 PLATFORM_NAME linux
 CPUARCH_NAME x86-64
 PLATFORM_VERSION 2.6.22.10
 CRASH_SIGNAL 11
 APPLICATION_UPTIME 0
 CRASH_TIME 1199350192

 OBJECT test-bin/4194304/12124
 OBJECT libc.so.6/47641679454208/1381596
 OBJECT ld-linux-x86-64.so.2/47641677234176/117128

 CALLSTACK 4201103
 CALLSTACK 4200063
 CALLSTACK 4199576
 CALLSTACK 4199829
 CALLSTACK 47641679656912
 CALLSTACK 4202709
 CALLSTACK 4202726
 CALLSTACK 4202737
 CALLSTACK 4202904
 CALLSTACK 47641679575876
 CALLSTACK 4196729

 ETC_KEY ThisIsMyKey
 ETC_VALUE ThisIsMyValue
 ETC_KEY ThisIsAnotherKey
 ETC_VALUE ThisIsAnotherValue

 END

Please note that blank lines are ignored. Lines starting with '#' chars 
are treated as comments and are thus ignored.

The first not-ignored line is always "MOJOCRASH <version>" and 
represents the version of MojoCrash that produced this crashlog. The 
second line is the crashlog format version. Currently this is always 
"CRASHLOG_VERSION 1", but the value increments whenever new fields are 
added. Servers should understand all fields up to the version they 
handle (and in most cases, should pass other fields through unmolested, 
as most are meant to be human-readable anyhow). The rest of the lines 
can appear in any order and frequency (although they tend to have a 
static pattern, as that's how the source code dumps them out). Here they 
are:

"APPLICATION_NAME <string>" is the name of the application. This should 
be an identifier, not an English title, so "UT2004" is better than 
"Unreal Tournament 2004" here. Aim for not having spaces or "strange" 
characters.

"APPLICATION_VERSION <string>" is the version of the crashing app. The 
string can be any format: 1.0, svn-revision-452, 5.2.1.0, "december 25, 
2:03 am", whatever makes this build uniquely identifiable.

"PLATFORM_NAME <string>" is the platform that the crashing app was run 
upon: "linux", "macosx", "windows", etc.

"PLATFORM_VERSION <string>" attempts to identify the platform's 
revision. This might be the kernel version on Linux, "5.0" on Windows 
XP, "10.4.1" on Mac OS X Tiger, etc.

"CRASH_SIGNAL <int>" is the signal number for the crash. This might be 
11 (SIGSEGV) on Unix, 13 (GPF) on Windows, etc. This is the int passed 
to the preflight() hook.

"APPLICATION_UPTIME <int>" is the amount of time the application ran, in 
seconds, between MOJOCRASH_install() being called and the crash handler 
starting, so in reasonable cases, this is roughly the amount of time 
that the program ran before crashing.

"CRASH_TIME <int>" is the time when the app crashed, in seconds since 
the Unix epoch (midnight, Jan 1st, 1970). This can be translated to a 
specific date and time when the crash occurred.

"OBJECT <string>/<int>/<int>" specify code objects: that's the filename, 
base address, and size in bytes, respectively. These repeat for each 
object.

"CALLSTACK <int>" specify the callstack. Each line is a frame pointer, 
starting with the top of the stack (the crash or crash handler, usually) 
and ending with main() or whatnot. This repeats for each stack frame and 
collectively represents the entire callstack.

"ETC_KEY <string>" specifies a key reported in the get_etc() hook. It 
must be followed by an "ETC_VALUE" line. There may be any number of 
these repeated in the log. Please limit keys to printable characters 
(alphanumeric and underscores).

"ETC_VALUE <string>" specifies a value reported in the get_etc() hook. 
It follows an "ETC_KEY" line. There may be any number of these repeated 
in the log. If you need a newline, please encode it as a \n sequence, as 
a real endline char would end the ETC_VALUE data.

"END" must be the last line in the log, demonstrating that the log is 
complete and not truncated.


Reporter App

(This section is out of date, please revise!)

The reporter app is a seperate process because it allows us to not care 
about the main application's requirements for threading, UI, etc. It 
also allows us to do the complex work from outside a process that might 
have any sort of strange memory corruption and otherwise mangled state.

Basic chain of events for the reporter app:
 * Reporter app reads pending bug reports and deletes them from disk.
 * If pending reports look like garbage, reporter app terminates immediately.
 * Reporter app shows user the various info it plans to send, lets them 
   optionally enter an email address.
 * User declines sending? Delete crash log and terminate immediately.
 * User approves sending? Connect to server and HTTP POST the data.
 * Read response from server, don't care about the result, hang up.


Posting to the web server

Connect to the server ("crashcatcher.mojogames.com" or whatever), TCP 
port 80, and send something like this:

 POST / HTTP/1.1
 Host: crashcatcher.mojogames.com
 User-Agent: mojocrash/0.0.1
 Accept: text/plain
 Accept-Charset: utf-8
 Connection: close
 Content-Type: multipart/form-data; boundary="9234j239fj2gjklsdu82jfl0234"
 Content-Length: 1687

 --9234j239fj2gjklsdu82jfl0234
 Content-Disposition: form-data; name="report1"; filename="file1.txt"
 Content-Type: text/plain; charset=utf8

 (crash report log file goes here.)

 --9234j239fj2gjklsdu82jfl0234--

Going through this line-by-line: !!! FIXME: this is out of date.

There's a POST command to http://crashcatcher.mojogames.com/ ... you 
determine the POST and the "Host:" lines from the url variable passed to 
MOJOCRASH_report().

The post is done by the MojoCrash client (User-Agent). The server will 
reject all posts from clients with a different User-Agent.

We are sending utf-8 encoded, plain text, and expect the reply to be as 
such, too.

Content-Type line is required to be application/x-www-form-urlencoded so 
the server doesn't mangle it.

Content-Length is just the strlen() of the data.

There must be a blank line after that so the server knows the real data 
is now coming.

The actual POST data then follows.

Client reads response and hangs up.

Please note that there's a hook that allows transformation of the data, which could, say, gzip-compress it and add an HTTP header that says as such.


To be clear, we're POSTing a few variables here.

The first is "app" ("UT2004") and that's the appname variable passed to 
MOJOCRASH_install().

The second is "version" ("3355") and that's the version variable passed 
to MOJOCRASH_install().

Next is "stack" which is the stack dump. Above it is url-encoded, but in 
human-readable terms, it looks like this:

 printf (stdio.c:801) [0xabcdef01]
 main (myprog.c:23) [0x10000000]

...that is, the program crashed in the printf() function, on line 801 of 
the source code stdio.c. The memory address of the crash point in memory 
was 0xabcdef01. printf(), in this case, was called on line 23 of 
myprog.c, in the function main(). Programs without debug symbols will 
just leave out the (stdio.c:801) part.

Next is "email" ("a@b.com"). This is what the user supplied, and it may 
be an empty string if they chose to ignore it.

Finally is "other" ("kernel 2.4.2") and that's whatever was supplied by 
the get_other_info callback.


Server

The server side is an Apache process that reads the request, parses the 
data, and passes it to a PHP or mod_perl script. This means the tedious 
parts, like logging, basic string parsing, or deciding if this was a 
valid virtual host, are already done by the webserver.

The script, when it receives control, will work as glue to Bugzilla. 
This can be done as a perl CGI script to take advantage of the existing 
bugzilla code, or can exist outside of bugzilla and use their example 
bug entry scripts. The bugzilla itself may or may not be available to 
the public, so this script may be the only way they can interact with 
it.

Once the script is running, it will need to decide if the given stack 
already exists in a bug entry, and if not, create a new entry. If it 
already exists, it should just append a new comment to the existing bug 
report with the pertinent details (email address, the "other" field, 
etc) and add a vote to the entry...the vote is just a convenient way to 
sort the bug list by most popular crashes...the one with the most votes 
is the one people are reporting the most.

If the bug was successfully added/updated in bugzilla, it will return a 
HTTP 200 result code ("success") and disconnect from the client. If 
there was a temporary problem, it should report 503 ("Service 
unavailable") and the client may optionally include logic to try 
resending sometime later. If there was a permanent problem or the 
client's POST was malformed, it should report 400 ("Bad Request") and 
the client should never try to resend this bug report.

To be considered...

Callstacks for multiple threads?
Checksums/validation for posted logs?

