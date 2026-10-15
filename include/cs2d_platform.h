#ifndef CS2D_PLATFORM_H
#define CS2D_PLATFORM_H

/*
 * Platform layer: the only header that includes <windows.h> / <winsock2.h>.
 *
 * On Windows it is a thin pass-through, so the desktop build keeps calling the same Win32 functions
 * it always did. Elsewhere (Emscripten for the web port, and any POSIX desktop) it maps the handful
 * of Win32 names the tree uses onto POSIX: BSD sockets, strcasecmp, a monotonic millisecond clock and
 * the VK_* key codes. VK_* are the game's own key identifiers (binds, UI), so they keep the Win32
 * values everywhere.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>

typedef int cs2d_socklen_t; /* recvfrom / getsockname length */
#else
#include <strings.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef int            SOCKET;
typedef unsigned long  u_long;
typedef unsigned short u_short;
typedef socklen_t      cs2d_socklen_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket    close
#define ioctlsocket    ioctl

#define _stricmp  strcasecmp
#define _strnicmp strncasecmp
#define _strdup   strdup
#define strtok_s  strtok_r

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

/* Win32 virtual-key codes (winuser.h values) */
#define VK_BACK     0x08
#define VK_TAB      0x09
#define VK_RETURN   0x0D
#define VK_SHIFT    0x10
#define VK_MENU     0x12
#define VK_ESCAPE   0x1B
#define VK_SPACE    0x20
#define VK_LEFT     0x25
#define VK_UP       0x26
#define VK_RIGHT    0x27
#define VK_DOWN     0x28
#define VK_DELETE   0x2E
#define VK_LCONTROL 0xA2

/* GetTickCount: milliseconds from a monotonic clock, wrapping at 2^32 like the Win32 call */
static inline uint32_t GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}
#endif

/* WSAStartup on Windows (once), nothing elsewhere; false when sockets are unavailable */
bool cs2d_platform_net_init(void);

/* Non-blocking mode for a UDP socket (FIONBIO) */
void cs2d_platform_set_nonblocking(SOCKET s);

bool cs2d_platform_file_exists(const char *path);
bool cs2d_platform_dir_exists(const char *path);
/* Creates / removes (empty) one directory level; true on success */
bool cs2d_platform_mkdir(const char *path);
bool cs2d_platform_rmdir(const char *path);

/*
 * Calls fn for each regular file of dir (names only, no directories), in the order the platform
 * returns them: FindFirstFileA's on Windows; elsewhere sorted case-insensitively, which is what NTFS
 * yields. Returns the number of files reported, or -1 when the directory cannot be opened.
 */
typedef void (*cs2d_platform_dir_fn)(const char *name, void *user);
int cs2d_platform_list_dir(const char *dir, cs2d_platform_dir_fn fn, void *user);

#endif
