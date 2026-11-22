/*
 * dirs.c - walking a folder, on all three platforms.
 *
 * <dirent.h> is POSIX. MSVC does not have it at all, and MinGW has it but with a struct dirent
 * that carries no d_type. The folder browser and the diagnostics screen both need to list a
 * directory and ask whether an entry is one, so the difference is settled here, once, rather
 * than in #ifdefs scattered through code that is about something else.
 *
 * It is also why this file exists at all: browse.c included <dirent.h> directly and so stopped
 * the Windows builds compiling from the day it was added, which nothing noticed because those
 * two targets are only built when a release is cut.
 */
#include "apeiron.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#endif

bool dir_is_dir(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool dir_open(DirScan *s, const char *path)
{
    memset(s, 0, sizeof *s);
#ifdef _WIN32
    char pattern[1300];
    snprintf(pattern, sizeof pattern, "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { s->err = (int)GetLastError(); return false; }
    s->h = h;
    /* FindFirstFile has already handed back the first entry; dir_next gives it out. */
    s->pending = true;
    snprintf(s->name, sizeof s->name, "%s", fd.cFileName);
    return true;
#else
    DIR *d = opendir(path);
    if (!d) { s->err = errno; return false; }
    s->h = d;
    return true;
#endif
}

bool dir_next(DirScan *s)
{
    if (!s->h) return false;
#ifdef _WIN32
    if (s->pending) { s->pending = false; return true; }
    WIN32_FIND_DATAA fd;
    if (!FindNextFileA((HANDLE)s->h, &fd)) return false;
    snprintf(s->name, sizeof s->name, "%s", fd.cFileName);
    return true;
#else
    struct dirent *e = readdir((DIR *)s->h);
    if (!e) return false;
    snprintf(s->name, sizeof s->name, "%s", e->d_name);
    return true;
#endif
}

void dir_close(DirScan *s)
{
    if (!s->h) return;
#ifdef _WIN32
    FindClose((HANDLE)s->h);
#else
    closedir((DIR *)s->h);
#endif
    s->h = NULL;
}

/* Why dir_open said no, in words a player can act on. The distinction that matters is between a
 * folder that is not there and one that is there but closed to us: on Android the second means
 * the storage permission has not been granted yet, and waiting is the right answer. */
const char *dir_why(int err)
{
#ifdef _WIN32
    switch (err) {
    case ERROR_ACCESS_DENIED:  return "permission denied";
    case ERROR_PATH_NOT_FOUND:
    case ERROR_FILE_NOT_FOUND: return "no such folder on this machine";
    default:                   return "cannot be opened";
    }
#else
    switch (err) {
    case EACCES: return "permission denied - storage not granted yet";
    case ENOENT: return "no such folder on this machine";
    default:     return strerror(err);
    }
#endif
}
