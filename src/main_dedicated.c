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

static bool is_flag(const char *arg, const char *name)
{
    if (!arg || !name) return false;
    const char *p = NULL;
    if (arg[0] == '-' && arg[1] == '-') p = arg + 2;
    else if (arg[0] == '-' || arg[0] == '/') p = arg + 1;
    else return false;
    return _stricmp(p, name) == 0;
}

static const char *get_arg_val(const char *arg, const char *prefix, int *i, int argc, char **argv)
{
    if (!arg || !prefix) return NULL;
    const char *p = NULL;
    if (arg[0] == '-' && arg[1] == '-') {
        p = arg + 2;
    } else if (arg[0] == '-' || arg[0] == '/') {
        p = arg + 1;
    } else {
        return NULL;
    }

    size_t plen = strlen(prefix);
    if (_strnicmp(p, prefix, plen) == 0) {
        if (p[plen] == '\0') {
            if (i && *i + 1 < argc) {
                (*i)++;
                return argv[*i];
            }
            return "";
        } else if (p[plen] == '=' || p[plen] == ':') {
            return p + plen + 1;
        }
    }
    return NULL;
}

static void print_usage(const char *prog)
{
    printf("CS2D Dedicated Server (1.0.1.8 p1)\n");
    printf("Usage: %s [options]\n\n", prog ? prog : "cs2d_server.exe");
    printf("Options:\n");
    printf("  --map, -map, -m <name>      Map to load (e.g. --map fun_roleplay)\n");
    printf("  --port, -port, -p <port>    UDP port to bind (default: 36963 or sys/server.cfg)\n");
    printf("  --offline, -off             Run in offline mode (no U.S.G.N. master server)\n");
    printf("  --lua, -lua, -l <file>      Execute Lua script at startup\n");
    printf("  --name, -name <name>        Server name (sv_name)\n");
    printf("  --pw, --password <pass>     Server password (sv_password)\n");
    printf("  --rcon <pass>               Remote console password (sv_rcon)\n");
    printf("  --maxplayers <n>            Maximum players (sv_maxplayers)\n");
    printf("  --help, -h, -?              Show this help message\n\n");
}

int main(int argc, char **argv)
{
    char map_buf[64] = {0};
    char name_buf[64] = {0};
    char pw_buf[64] = {0};
    char rcon_buf[64] = {0};
    const char *map = NULL;
    const char *lua_script = NULL;
    int port = 0;
    int maxplayers = 0;
    bool offline = false;

    for (int i = 1; i < argc; i++) {
        const char *val = NULL;
        if (is_flag(argv[i], "help") || is_flag(argv[i], "h") || is_flag(argv[i], "?")) {
            print_usage(argv[0]);
            return 0;
        } else if (is_flag(argv[i], "offline") || is_flag(argv[i], "off")) {
            offline = true;
        } else if ((val = get_arg_val(argv[i], "map", &i, argc, argv)) != NULL ||
                   (val = get_arg_val(argv[i], "m", &i, argc, argv)) != NULL) {
            cs2d_sv_sanitize_map_name(map_buf, sizeof(map_buf), val);
            map = map_buf;
        } else if ((val = get_arg_val(argv[i], "port", &i, argc, argv)) != NULL ||
                   (val = get_arg_val(argv[i], "p", &i, argc, argv)) != NULL) {
            port = atoi(val);
        } else if ((val = get_arg_val(argv[i], "lua", &i, argc, argv)) != NULL ||
                   (val = get_arg_val(argv[i], "l", &i, argc, argv)) != NULL) {
            lua_script = val;
        } else if ((val = get_arg_val(argv[i], "name", &i, argc, argv)) != NULL) {
            snprintf(name_buf, sizeof(name_buf), "%.63s", val);
        } else if ((val = get_arg_val(argv[i], "password", &i, argc, argv)) != NULL ||
                   (val = get_arg_val(argv[i], "pw", &i, argc, argv)) != NULL) {
            snprintf(pw_buf, sizeof(pw_buf), "%.63s", val);
        } else if ((val = get_arg_val(argv[i], "rcon", &i, argc, argv)) != NULL) {
            snprintf(rcon_buf, sizeof(rcon_buf), "%.63s", val);
        } else if ((val = get_arg_val(argv[i], "maxplayers", &i, argc, argv)) != NULL ||
                   (val = get_arg_val(argv[i], "max", &i, argc, argv)) != NULL) {
            maxplayers = atoi(val);
        }
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

    /* Apply commandline overrides */
    if (name_buf[0]) snprintf(g_sv.set.sv_name, sizeof(g_sv.set.sv_name), "%.63s", name_buf);
    if (pw_buf[0]) snprintf(g_sv.set.sv_password, sizeof(g_sv.set.sv_password), "%.63s", pw_buf);
    if (rcon_buf[0]) snprintf(g_sv.set.sv_rcon, sizeof(g_sv.set.sv_rcon), "%.63s", rcon_buf);
    if (maxplayers > 0) g_sv.set.sv_maxplayers = maxplayers;

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
