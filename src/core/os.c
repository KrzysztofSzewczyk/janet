/*
* Copyright (c) 2019 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#ifndef JANET_AMALG
#include <janet.h>
#include "util.h"
#endif

#include <stdlib.h>

#ifndef JANET_REDUCED_OS

#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef JANET_WINDOWS
#include <windows.h>
#include <direct.h>
#include <sys/utime.h>
#include <io.h>
#else
#include <utime.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

/* For macos */
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#endif /* JANET_REDCUED_OS */

/* Core OS functions */

/* Full OS functions */

static Janet os_which(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
#ifdef JANET_WINDOWS
    return janet_ckeywordv("windows");
#elif __APPLE__
    return janet_ckeywordv("macos");
#elif defined(__EMSCRIPTEN__)
    return janet_ckeywordv("web");
#else
    return janet_ckeywordv("posix");
#endif
}

static Janet os_exit(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    if (argc == 0) {
        exit(EXIT_SUCCESS);
    } else if (janet_checkint(argv[0])) {
        exit(janet_unwrap_integer(argv[0]));
    } else {
        exit(EXIT_FAILURE);
    }
    return janet_wrap_nil();
}

#ifdef JANET_REDUCED_OS
/* Provide a dud os/getenv so init.janet works, but nothing else */

static Janet os_getenv(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 1);
    return janet_wrap_nil();
}

#else
/* Provide full os functionality */

#ifdef JANET_WINDOWS
static Janet os_execute(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, -1);
    JanetBuffer *buffer = janet_buffer(10);
    for (int32_t i = 0; i < argc; i++) {
        const uint8_t *argstring = janet_getstring(argv, i);
        janet_buffer_push_bytes(buffer, argstring, janet_string_length(argstring));
        if (i != argc - 1) {
            janet_buffer_push_u8(buffer, ' ');
        }
    }
    janet_buffer_push_u8(buffer, 0);

    /* Convert to wide chars */
    wchar_t *sys_str = malloc(buffer->count * sizeof(wchar_t));
    if (NULL == sys_str) {
        JANET_OUT_OF_MEMORY;
    }
    int nwritten = MultiByteToWideChar(
                       CP_UTF8,
                       MB_PRECOMPOSED,
                       buffer->data,
                       buffer->count,
                       sys_str,
                       buffer->count);
    if (nwritten == 0) {
        free(sys_str);
        janet_panic("could not create process");
    }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Start the child process.
    if (!CreateProcess(NULL,
                       (LPSTR) sys_str,
                       NULL,
                       NULL,
                       FALSE,
                       0,
                       NULL,
                       NULL,
                       &si,
                       &pi)) {
        free(sys_str);
        janet_panic("could not create process");
    }
    free(sys_str);

    // Wait until child process exits.
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Close process and thread handles.
    WORD status;
    GetExitCodeProcess(pi.hProcess, (LPDWORD)&status);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return janet_wrap_integer(status);
}
#else
static Janet os_execute(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, -1);
    const char **child_argv = malloc(sizeof(char *) * (argc + 1));
    int status = 0;
    if (NULL == child_argv) {
        JANET_OUT_OF_MEMORY;
    }
    for (int32_t i = 0; i < argc; i++) {
        child_argv[i] = janet_getcstring(argv, i);
    }
    child_argv[argc] = NULL;

    /* Fork child process */
    pid_t pid = fork();
    if (pid < 0) {
        janet_panic("failed to execute");
    } else if (pid == 0) {
        if (-1 == execve(child_argv[0], (char **)child_argv, NULL)) {
            exit(1);
        }
    } else {
        waitpid(pid, &status, 0);
    }
    free(child_argv);
    return janet_wrap_integer(status);
}
#endif

static Janet os_shell(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    const char *cmd = argc
                      ? janet_getcstring(argv, 0)
                      : NULL;
    int stat = system(cmd);
    return argc
           ? janet_wrap_integer(stat)
           : janet_wrap_boolean(stat);
}

static Janet os_getenv(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *cstr = janet_getcstring(argv, 0);
    const char *res = getenv(cstr);
    return res
           ? janet_cstringv(res)
           : janet_wrap_nil();
}

static Janet os_setenv(int32_t argc, Janet *argv) {
#ifdef JANET_WINDOWS
#define SETENV(K,V) _putenv_s(K, V)
#define UNSETENV(K) _putenv_s(K, "")
#else
#define SETENV(K,V) setenv(K, V, 1)
#define UNSETENV(K) unsetenv(K)
#endif
    janet_arity(argc, 1, 2);
    const char *ks = janet_getcstring(argv, 0);
    if (argc == 1 || janet_checktype(argv[1], JANET_NIL)) {
        UNSETENV(ks);
    } else {
        SETENV(ks, janet_getcstring(argv, 1));
    }
    return janet_wrap_nil();
}

static Janet os_time(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    double dtime = (double)(time(NULL));
    return janet_wrap_number(dtime);
}

/* Clock shims */
#ifdef JANET_WINDOWS
static int gettime(struct timespec *spec) {
    int64_t wintime = 0LL;
    GetSystemTimeAsFileTime((FILETIME *)&wintime);
    /* Windows epoch is January 1, 1601 apparently*/
    wintime -= 116444736000000000LL;
    spec->tv_sec  = wintime / 10000000LL;
    /* Resolution is 100 nanoseconds. */
    spec->tv_nsec = wintime % 10000000LL * 100;
    return 0;
}
#elif defined(__MACH__)
static int gettime(struct timespec *spec) {
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    spec->tv_sec = mts.tv_sec;
    spec->tv_nsec = mts.tv_nsec;
    return 0;
}
#else
#define gettime(TV) clock_gettime(CLOCK_MONOTONIC, (TV))
#endif

static Janet os_clock(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    struct timespec tv;
    if (gettime(&tv)) janet_panic("could not get time");
    double dtime = tv.tv_sec + (tv.tv_nsec / 1E9);
    return janet_wrap_number(dtime);
}

static Janet os_sleep(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    double delay = janet_getnumber(argv, 0);
    if (delay < 0) janet_panic("invalid argument to sleep");
#ifdef JANET_WINDOWS
    Sleep((DWORD)(delay * 1000));
#else
    struct timespec ts;
    ts.tv_sec = (time_t) delay;
    ts.tv_nsec = (delay <= UINT32_MAX)
                 ? (long)((delay - ((uint32_t)delay)) * 1000000000)
                 : 0;
    nanosleep(&ts, NULL);
#endif
    return janet_wrap_nil();
}

static Janet os_cwd(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    char buf[FILENAME_MAX];
    char *ptr;
#ifdef JANET_WINDOWS
    ptr = _getcwd(buf, FILENAME_MAX);
#else
    ptr = getcwd(buf, FILENAME_MAX);
#endif
    if (NULL == ptr) janet_panic("could not get current directory");
    return janet_cstringv(ptr);
}

static Janet os_date(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    (void) argv;
    time_t t;
    struct tm *t_info;
    if (argc) {
        t = (time_t) janet_getinteger64(argv, 0);
    } else {
        time(&t);
    }
    t_info = localtime(&t);
    JanetKV *st = janet_struct_begin(9);
    janet_struct_put(st, janet_ckeywordv("seconds"), janet_wrap_number(t_info->tm_sec));
    janet_struct_put(st, janet_ckeywordv("minutes"), janet_wrap_number(t_info->tm_min));
    janet_struct_put(st, janet_ckeywordv("hours"), janet_wrap_number(t_info->tm_hour));
    janet_struct_put(st, janet_ckeywordv("month-day"), janet_wrap_number(t_info->tm_mday - 1));
    janet_struct_put(st, janet_ckeywordv("month"), janet_wrap_number(t_info->tm_mon));
    janet_struct_put(st, janet_ckeywordv("year"), janet_wrap_number(t_info->tm_year + 1900));
    janet_struct_put(st, janet_ckeywordv("week-day"), janet_wrap_number(t_info->tm_wday));
    janet_struct_put(st, janet_ckeywordv("year-day"), janet_wrap_number(t_info->tm_yday));
    janet_struct_put(st, janet_ckeywordv("dst"), janet_wrap_boolean(t_info->tm_isdst));
    return janet_wrap_struct(janet_struct_end(st));
}

static Janet os_link(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
#ifdef JANET_WINDOWS
    (void) argc;
    (void) argv;
    janet_panic("os/link not supported on Windows");
    return janet_wrap_nil();
#else
    const char *oldpath = janet_getcstring(argv, 0);
    const char *newpath = janet_getcstring(argv, 1);
    int res = ((argc == 3 && janet_getboolean(argv, 2)) ? symlink : link)(oldpath, newpath);
    if (res == -1) janet_panicv(janet_cstringv(strerror(errno)));
    return janet_wrap_integer(res);
#endif
}

static Janet os_mkdir(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *path = janet_getcstring(argv, 0);
#ifdef JANET_WINDOWS
    int res = _mkdir(path);
#else
    int res = mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH);
#endif
    return janet_wrap_boolean(res != -1);
}

static Janet os_cd(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *path = janet_getcstring(argv, 0);
#ifdef JANET_WINDOWS
    int res = _chdir(path);
#else
    int res = chdir(path);
#endif
    return janet_wrap_boolean(res != -1);
}

static Janet os_touch(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    const char *path = janet_getcstring(argv, 0);
    struct utimbuf timebuf, *bufp;
    if (argc >= 2) {
        bufp = &timebuf;
        timebuf.actime = (time_t) janet_getnumber(argv, 1);
        if (argc >= 3) {
            timebuf.modtime = (time_t) janet_getnumber(argv, 2);
        } else {
            timebuf.modtime = timebuf.actime;
        }
    } else {
        bufp = NULL;
    }
    int res = utime(path, bufp);
    return janet_wrap_boolean(res != -1);
}

#ifdef JANET_WINDOWS
static const uint8_t *janet_decode_permissions(unsigned short m) {
    uint8_t flags[9] = {0};
    flags[0] = flags[3] = flags[6] = (m & S_IREAD) ? 'r' : '-';
    flags[1] = flags[4] = flags[7] = (m & S_IWRITE) ? 'w' : '-';
    flags[2] = flags[5] = flags[8] = (m & S_IEXEC) ? 'x' : '-';
    return janet_string(flags, sizeof(flags));
}

static const uint8_t *janet_decode_mode(unsigned short m) {
    const char *str = "other";
    if (m & _S_IFREG(m)) str = "file";
    else if (m & _S_IFDIR(m)) str = "directory";
    return janet_ckeyword(str);
}
#else
static const uint8_t *janet_decode_permissions(mode_t m) {
    uint8_t flags[9] = {0};
    flags[0] = (m & S_IRUSR) ? 'r' : '-';
    flags[1] = (m & S_IWUSR) ? 'w' : '-';
    flags[2] = (m & S_IXUSR) ? 'x' : '-';
    flags[3] = (m & S_IRGRP) ? 'r' : '-';
    flags[4] = (m & S_IWGRP) ? 'w' : '-';
    flags[5] = (m & S_IXGRP) ? 'x' : '-';
    flags[6] = (m & S_IROTH) ? 'r' : '-';
    flags[7] = (m & S_IWOTH) ? 'w' : '-';
    flags[8] = (m & S_IXOTH) ? 'x' : '-';
    return janet_string(flags, sizeof(flags));
}

static const uint8_t *janet_decode_mode(mode_t m) {
    const char *str = "other";
    if (S_ISREG(m)) str = "file";
    else if (S_ISDIR(m)) str = "directory";
    else if (S_ISFIFO(m)) str = "fifo";
    else if (S_ISBLK(m)) str = "block";
    else if (S_ISSOCK(m)) str = "socket";
    else if (S_ISLNK(m)) str = "link";
    else if (S_ISCHR(m)) str = "character";
    return janet_ckeyword(str);
}
#endif

static Janet os_stat(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    const char *path = janet_getcstring(argv, 0);
    JanetTable *tab;
    if (argc == 2) {
        tab = janet_gettable(argv, 1);
    } else {
        tab = janet_table(0);
    }
    /* Build result */
#ifdef JANET_WINDOWS
    struct _stat st;
    int res = _stat(path, &st);
#else
    struct stat st;
    int res = stat(path, &st);
#endif
    if (-1 == res) {
        janet_panicv(janet_cstringv(strerror(errno)));
    }
    janet_table_put(tab, janet_ckeywordv("dev"), janet_wrap_number(st.st_dev));
    janet_table_put(tab, janet_ckeywordv("inode"), janet_wrap_number(st.st_ino));
    janet_table_put(tab, janet_ckeywordv("mode"), janet_wrap_keyword(janet_decode_mode(st.st_mode)));
    janet_table_put(tab, janet_ckeywordv("permissions"), janet_wrap_string(janet_decode_permissions(st.st_mode)));
    janet_table_put(tab, janet_ckeywordv("uid"), janet_wrap_number(st.st_uid));
    janet_table_put(tab, janet_ckeywordv("gid"), janet_wrap_number(st.st_gid));
    janet_table_put(tab, janet_ckeywordv("size"), janet_wrap_number(st.st_size));
    janet_table_put(tab, janet_ckeywordv("nlink"), janet_wrap_number(st.st_nlink));
    janet_table_put(tab, janet_ckeywordv("rdev"), janet_wrap_number(st.st_rdev));
#ifndef JANET_WINDOWS
    janet_table_put(tab, janet_ckeywordv("blocksize"), janet_wrap_number(st.st_blksize));
    janet_table_put(tab, janet_ckeywordv("blocks"), janet_wrap_number(st.st_blocks));
#endif
    janet_table_put(tab, janet_ckeywordv("accessed"), janet_wrap_number((double) st.st_atime));
    janet_table_put(tab, janet_ckeywordv("modified"), janet_wrap_number((double) st.st_mtime));
    janet_table_put(tab, janet_ckeywordv("changed"), janet_wrap_number((double) st.st_ctime));
    return janet_wrap_table(tab);
}

static Janet os_dir(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    const char *dir = janet_getcstring(argv, 0);
    JanetArray *paths = (argc == 2) ? janet_getarray(argv, 1) : janet_array(0);
#ifdef JANET_WINDOWS
    /* Read directory items with FindFirstFile / FindNextFile / FindClose */
    struct _finddata_t afile;
    char pattern[MAX_PATH + 1];
    if (strlen(dir) > (sizeof(pattern) - 3))
        janet_panicf("path too long: %s", dir);
    sprintf(pattern, "%s/*", dir); 
    intptr_t res = _findfirst(pattern, &afile);
    while (res != -1) {
        janet_array_push(paths, janet_cstringv(afile.name));
        res = _findnext(res, &afile);
    }
    _findclose(res);
#else
    /* Read directory items with opendir / readdir / closedir */
    struct dirent *dp;
    DIR *dfd = opendir(dir);
    if (dfd == NULL) janet_panicf("cannot open directory %s", dir);
    while ((dp = readdir(dfd)) != NULL) {
        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
            continue;
        }
        janet_array_push(paths, janet_cstringv(dp->d_name));
    }
    closedir(dfd);
#endif
    return janet_wrap_array(paths);
}

#endif /* JANET_REDUCED_OS */

static const JanetReg os_cfuns[] = {
    {
        "os/exit", os_exit,
        JDOC("(os/exit x)\n\n"
             "Exit from janet with an exit code equal to x. If x is not an integer, "
             "the exit with status equal the hash of x.")
    },
    {
        "os/which", os_which,
        JDOC("(os/which)\n\n"
             "Check the current operating system. Returns one of:\n\n"
             "\t:windows - Microsoft Windows\n"
             "\t:macos - Apple macos\n"
             "\t:posix - A POSIX compatible system (default)")
    },
    {
        "os/getenv", os_getenv,
        JDOC("(os/getenv variable)\n\n"
             "Get the string value of an environment variable.")
    },
#ifndef JANET_REDUCED_OS
    {
        "os/dir", os_dir,
        JDOC("(os/stat dir [, array])\n\n"
             "Iterate over files and subdirectories in a directory. Returns an array of paths parts, "
             "with only the filename or directory name and no prefix.")
    },
    {
        "os/stat", os_stat,
        JDOC("(os/stat path [, tab])\n\n"
             "Gets information about a file or directory. Returns a table.")
    },
    {
        "os/touch", os_touch,
        JDOC("(os/touch path [, actime [, modtime]])\n\n"
             "Update the access time and modification times for a file. By default, sets "
             "times to the current time.")
    },
    {
        "os/cd", os_cd,
        JDOC("(os/cd path)\n\n"
             "Change current directory to path. Returns true on success, false on failure.")
    },
    {
        "os/mkdir", os_mkdir,
        JDOC("(os/mkdir path)\n\n"
             "Create a new directory. The path will be relative to the current directory if relative, otherwise "
             "it will be an absolute path.")
    },
    {
        "os/link", os_link,
        JDOC("(os/link oldpath newpath [, symlink])\n\n"
             "Create a symlink from oldpath to newpath. The 3 optional paramater "
             "enables a hard link over a soft link. Does not work on Windows.")
    },
    {
        "os/execute", os_execute,
        JDOC("(os/execute program & args)\n\n"
             "Execute a program on the system and pass it string arguments. Returns "
             "the exit status of the program.")
    },
    {
        "os/shell", os_shell,
        JDOC("(os/shell str)\n\n"
             "Pass a command string str directly to the system shell.")
    },
    {
        "os/setenv", os_setenv,
        JDOC("(os/setenv variable value)\n\n"
             "Set an environment variable.")
    },
    {
        "os/time", os_time,
        JDOC("(os/time)\n\n"
             "Get the current time expressed as the number of seconds since "
             "January 1, 1970, the Unix epoch. Returns a real number.")
    },
    {
        "os/clock", os_clock,
        JDOC("(os/clock)\n\n"
             "Return the number of seconds since some fixed point in time. The clock "
             "is guaranteed to be non decreasing in real time.")
    },
    {
        "os/sleep", os_sleep,
        JDOC("(os/sleep nsec)\n\n"
             "Suspend the program for nsec seconds. 'nsec' can be a real number. Returns "
             "nil.")
    },
    {
        "os/cwd", os_cwd,
        JDOC("(os/cwd)\n\n"
             "Returns the current working directory.")
    },
    {
        "os/date", os_date,
        JDOC("(os/date [,time])\n\n"
             "Returns the given time as a date struct, or the current time if no time is given. "
             "Returns a struct with following key values. Note that all numbers are 0-indexed.\n\n"
             "\t:seconds - number of seconds [0-61]\n"
             "\t:minutes - number of minutes [0-59]\n"
             "\t:seconds - number of hours [0-23]\n"
             "\t:month-day - day of month [0-30]\n"
             "\t:month - month of year [0, 11]\n"
             "\t:year - years since year 0 (e.g. 2019)\n"
             "\t:week-day - day of the week [0-6]\n"
             "\t:year-day - day of the year [0-365]\n"
             "\t:dst - If Day Light Savings is in effect")
    },
#endif
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_os(JanetTable *env) {
    janet_core_cfuns(env, NULL, os_cfuns);
}
