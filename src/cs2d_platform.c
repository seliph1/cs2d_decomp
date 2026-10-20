#include "cs2d_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <direct.h>

bool cs2d_platform_net_init(void)
{
    static bool done = false;
    if (done) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    done = true;
    return true;
}

void cs2d_platform_set_nonblocking(SOCKET s)
{
    u_long non_blocking = 1;
    ioctlsocket(s, FIONBIO, &non_blocking);
}

bool cs2d_platform_file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool cs2d_platform_dir_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool cs2d_platform_mkdir(const char *path)
{
    return _mkdir(path) == 0;
}

bool cs2d_platform_rmdir(const char *path)
{
    return _rmdir(path) == 0;
}

int cs2d_platform_list_dir(const char *dir, cs2d_platform_dir_fn fn, void *user)
{
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        fn(fd.cFileName, user);
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

bool cs2d_platform_net_init(void)
{
    return true;
}

void cs2d_platform_set_nonblocking(SOCKET s)
{
    int flags = fcntl(s, F_GETFL, 0);
    if (flags >= 0) fcntl(s, F_SETFL, flags | O_NONBLOCK);
}

bool cs2d_platform_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool cs2d_platform_dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool cs2d_platform_mkdir(const char *path)
{
    return mkdir(path, 0755) == 0;
}

bool cs2d_platform_rmdir(const char *path)
{
    return rmdir(path) == 0;
}

/* NTFS orders names by their upper-cased UTF-16 units; for ASCII names that is this comparison */
static int cmp_upper(const void *a, const void *b)
{
    const unsigned char *x = *(const unsigned char *const *)a;
    const unsigned char *y = *(const unsigned char *const *)b;
    for (;; x++, y++) {
        int cx = toupper(*x), cy = toupper(*y);
        if (cx != cy || !cx) return cx - cy;
    }
}

int cs2d_platform_list_dir(const char *dir, cs2d_platform_dir_fn fn, void *user)
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    char **names = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char path[MAX_PATH * 2];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (!cs2d_platform_file_exists(path)) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            char **grown = (char **)realloc(names, (size_t)cap * sizeof(*names));
            if (!grown) break;
            names = grown;
        }
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof(*names), cmp_upper);
    for (int i = 0; i < n; i++) {
        fn(names[i], user);
        free(names[i]);
    }
    free(names);
    return n;
}
#endif

/* --- file system helpers shared by both platforms (no Win32 / POSIX specific call) --- */

bool cs2d_platform_mkdir_parent(const char *path)
{
    char dir[MAX_PATH];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(dir)) return false;
    memcpy(dir, path, len + 1);

    char *sep = strrchr(dir, '/');
    char *backslash = strrchr(dir, '\\');
    if (backslash && (!sep || backslash > sep)) sep = backslash;
    if (!sep) return true;           /* a bare name: the working directory already exists */
    *sep = '\0';
    if (dir[0] == '\0') return true; /* "/name": the root already exists */

    /* One level at a time ("a", then "a/b", ...); a level that is already there fails and is ignored */
    for (char *p = dir + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char c = *p;
        *p = '\0';
        cs2d_platform_mkdir(dir);
        *p = c;
    }
    cs2d_platform_mkdir(dir);
    return cs2d_platform_dir_exists(dir);
}

const char *cs2d_platform_screenshot_path(char *out, size_t out_size, const char *prefix, const char *suffix)
{
    size_t len = strlen(prefix);
    if (len > 4 && _stricmp(prefix + len - 4, ".bmp") == 0) len -= 4; /* a file-name prefix keeps its extension */
    snprintf(out, out_size, "%.*s%s.bmp", (int)len, prefix, suffix);
    return out;
}

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

SOCKET cs2d_platform_tcp_connect(const char *host, const char *port)
{
    if (!cs2d_platform_net_init()) return INVALID_SOCKET;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res) return INVALID_SOCKET;
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s != INVALID_SOCKET && connect(s, res->ai_addr, (cs2d_socklen_t)res->ai_addrlen) != 0) {
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return s;
}

void cs2d_platform_console_utf8(void)
{
#ifdef _WIN32
    /* Lua/config strings are UTF-8; the console code page must match printf's bytes. */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}
