/*
 * cs2d_server.exe: the dedicated server entry point (cs2d_dedicated.exe 1.0.1.8 p1).
 *
 * Command line (as the original): -offline / -off, -map <name>, -port <n>. The banner lines are the
 * original's start-up output (ded_ph1.log).
 */
#include "cs2d_server.h"
#include "cs2d_sv_lua.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

static cs2d_server_t g_sv;

int main(int argc, char **argv)
{
    const char *map = NULL;
    const char *lua_script = NULL;
    int port = 0;
    bool offline = false;
    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "-offline") == 0 || _stricmp(argv[i], "-off") == 0) offline = true;
        else if (_stricmp(argv[i], "-map") == 0 && i + 1 < argc) map = argv[++i];
        else if (_stricmp(argv[i], "-port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (_stricmp(argv[i], "-lua") == 0 && i + 1 < argc) lua_script = argv[++i];
    }

    static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    cs2d_server_print("#######################################################");
    cs2d_server_print("Unreal Software's CS2D Dedicated Server");
    cs2d_server_print("> visit www.CS2D.com for info and updates <");
    cs2d_server_print("Game Version: 1.0.1.8 p1");
    cs2d_server_print("Server Build for 1.0.1.8 p1: 1.0");
    cs2d_server_print("System Time: %02d %s %d - %02d:%02d:%02d", lt->tm_mday, months[lt->tm_mon], lt->tm_year + 1900,
                      lt->tm_hour, lt->tm_min, lt->tm_sec);
    cs2d_server_print("Operating System: Microsoft Windows");
    cs2d_server_print("#######################################################");
    if (offline) {
        cs2d_server_print("WARNING: running in offline mode (commandline -offline or -off)!");
        cs2d_server_print("WARNING: U.S.G.N. (latest serverlist, news, login, friends) is not available in offline mode!");
    } else {
        /* The U.S.G.N. serverlist registration is not ported: this server never announces itself */
        cs2d_server_print("WARNING: U.S.G.N. is not implemented, running as if -offline was given");
        offline = true;
    }
    cs2d_server_print("Starting server...");

    cs2d_sv_settings_default(&g_sv.set);
    cs2d_sv_settings_load(&g_sv.set, "sys/server.cfg");
    if (!cs2d_server_start(&g_sv, map, port, offline)) return 1;

    if (lua_script && lua_script[0]) {
        cs2d_sv_lua_dofile(&g_sv, lua_script);
    }

#ifdef _WIN32
    timeBeginPeriod(1);
    LARGE_INTEGER qpf;
    QueryPerformanceFrequency(&qpf);
    double inv_freq = 1.0 / (double)qpf.QuadPart;
    LARGE_INTEGER next_tick;
    QueryPerformanceCounter(&next_tick);
#endif

    /* The dedicated server runs at 60~63 ticks (BlitzMax standard 60 FPS) */
    int target_ticks = 60;
    double frame_interval_sec = 1.0 / (double)target_ticks;

    for (;;) {
        cs2d_server_frame(&g_sv);

#ifdef _WIN32
        /* Advance next tick target */
        next_tick.QuadPart += (LONGLONG)(frame_interval_sec * (double)qpf.QuadPart);

        LARGE_INTEGER cur;
        QueryPerformanceCounter(&cur);
        double remaining = (double)(next_tick.QuadPart - cur.QuadPart) * inv_freq;

        if (remaining > 0.002) {
            DWORD sleep_ms = (DWORD)((remaining - 0.001) * 1000.0);
            if (sleep_ms > 0) Sleep(sleep_ms);
        }
        /* Spin-wait remainder for sub-millisecond precision */
        while (1) {
            QueryPerformanceCounter(&cur);
            if (cur.QuadPart >= next_tick.QuadPart) break;
        }

        /* If lagging behind by more than 2 frames, resync to avoid spiral of death */
        if (cur.QuadPart - next_tick.QuadPart > (LONGLONG)(2.0 * frame_interval_sec * (double)qpf.QuadPart)) {
            next_tick = cur;
        }
#else
        struct timespec ts = { 0, (long)(frame_interval_sec * 1e9) };
        nanosleep(&ts, NULL);
#endif
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    cs2d_server_stop(&g_sv);
    return 0;
}
