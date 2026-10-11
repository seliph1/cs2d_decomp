#include "cs2d_client.h"
#include <stdio.h>
#include <string.h>

/*
 * Lobby scene: the CS2D main menu.
 * Layout measured from CS2D.exe 1.0.1.8 at 850x480: gfx/splash.bmp stretched over the
 * whole window, the cs2d.ico logo in the top-left corner, the "Unreal Software's CS2D"
 * header with U.S.G.N. / Steam status, and the menu entries (sys/language strings
 * 0003..0021) in Liberation Sans 15px at x = 16, 20px apart.
 */

typedef enum {
    LOBBY_ACT_CONSOLE,
    LOBBY_ACT_QUICK_PLAY,
    LOBBY_ACT_NEW_GAME,
    LOBBY_ACT_FIND_SERVERS,
    LOBBY_ACT_OPTIONS,
    LOBBY_ACT_FRIENDS,
    LOBBY_ACT_MODS,
    LOBBY_ACT_EDITOR,
    LOBBY_ACT_HELP,
    LOBBY_ACT_DISCORD,
    LOBBY_ACT_QUIT
} lobby_action_t;

typedef struct {
    const char     *label;
    float           y;          /* Text top; negative values are measured from the bottom edge */
    cs2d_font_id_t  font;
    lobby_action_t  action;
} lobby_entry_t;

static const lobby_entry_t s_entries[] = {
    { "Console",      185.0f, CS2D_FONT_SMALL, LOBBY_ACT_CONSOLE      },
    { "Quick Play",   215.0f, CS2D_FONT_MENU,  LOBBY_ACT_QUICK_PLAY   },
    { "New Game",     235.0f, CS2D_FONT_MENU,  LOBBY_ACT_NEW_GAME     },
    { "Find Servers", 255.0f, CS2D_FONT_MENU,  LOBBY_ACT_FIND_SERVERS },
    { "Options",      291.0f, CS2D_FONT_MENU,  LOBBY_ACT_OPTIONS      },
    { "Friends",      311.0f, CS2D_FONT_MENU,  LOBBY_ACT_FRIENDS      },
    { "Mods",         331.0f, CS2D_FONT_MENU,  LOBBY_ACT_MODS         },
    { "Editor",       351.0f, CS2D_FONT_MENU,  LOBBY_ACT_EDITOR       },
    { "Help",         371.0f, CS2D_FONT_MENU,  LOBBY_ACT_HELP         },
    { "Discord",      391.0f, CS2D_FONT_MENU,  LOBBY_ACT_DISCORD      },
    { "Quit",         -30.0f, CS2D_FONT_MENU,  LOBBY_ACT_QUIT         }
};
#define LOBBY_ENTRY_COUNT ((int)(sizeof(s_entries) / sizeof(s_entries[0])))

#define LOBBY_TEXT_X   15.0f
#define LOBBY_FLAG_US  196 /* Index of "us" in gfx/incbindata/flags_iso.txt (16x11 cells, 21 per row) */

static cs2d_texture_t s_tex_bg;
static cs2d_texture_t s_tex_logo;
static cs2d_texture_t s_tex_title;
static cs2d_texture_t s_tex_usgn;
static cs2d_texture_t s_tex_steam;
static cs2d_texture_t s_tex_flags;

static float entry_top(const lobby_entry_t *e, int screen_h)
{
    return (e->y < 0.0f) ? (float)screen_h + e->y : e->y;
}

static int entry_at(const cs2d_client_t *client, float mx, float my)
{
    for (int i = 0; i < LOBBY_ENTRY_COUNT; i++) {
        const lobby_entry_t *e = &s_entries[i];
        float w, h;
        cs2d_render_text_measure(e->font, e->label, &w, &h);
        float y = entry_top(e, client->config.window_height);
        if (mx >= LOBBY_TEXT_X && mx < LOBBY_TEXT_X + w && my >= y && my < y + h) return i;
    }
    return -1;
}

static void lobby_activate(cs2d_client_t *client, const lobby_entry_t *entry)
{
    switch (entry->action) {
        case LOBBY_ACT_CONSOLE:
            client->console_open  = true;
            client->ui.focused_id = CS2D_UI_ID("ConsoleInput");
            break;
        case LOBBY_ACT_FIND_SERVERS:
            cs2d_client_open_servers(client);
            break;
        case LOBBY_ACT_NEW_GAME:
            client->create_server_open = true;
            client->options_open       = false;
            client->ui.focused_id      = CS2D_UI_ID("SrvNameInput");
            break;
        case LOBBY_ACT_OPTIONS:
            client->options_open       = true;
            client->create_server_open = false;
            client->ui.focused_id      = CS2D_UI_ID("OptPlayerName");
            break;
        case LOBBY_ACT_EDITOR:
            client->editor_map_name[0] = '\0';
            cs2d_client_set_scene(client, CS2D_SCENE_EDITOR);
            break;
        case LOBBY_ACT_QUIT:
            client->quit_requested = true;
            break;
        default:
            cs2d_client_console_print(client, 255, 45, 45, "'%s' is not reconstructed yet.", entry->label);
            client->console_open = true;
            break;
    }
}

static void lobby_enter(cs2d_client_t *client)
{
    client->lobby_hover = -1;
    cs2d_texture_load_ex(&s_tex_bg,      "gfx/splash.bmp",                0);
    cs2d_texture_load_ex(&s_tex_logo,    "cs2d.ico",                      2);
    cs2d_texture_load_ex(&s_tex_title,   "gfx/gametitle.png",             2);
    cs2d_texture_load_ex(&s_tex_usgn,    "gfx/incbindata/usgn_icon.png",  3);
    cs2d_texture_load_ex(&s_tex_steam,   "gfx/incbindata/steam_icon.png", 3);
    cs2d_texture_load_grid(&s_tex_flags, "gfx/incbindata/flags.png",      0, 16, 11);
}

static void lobby_leave(cs2d_client_t *client)
{
    (void)client;
    cs2d_texture_free(&s_tex_bg);
    cs2d_texture_free(&s_tex_logo);
    cs2d_texture_free(&s_tex_usgn);
    cs2d_texture_free(&s_tex_steam);
    cs2d_texture_free(&s_tex_flags);
    cs2d_texture_free(&s_tex_title);
}

static void lobby_update(cs2d_client_t *client, float delta_time)
{
    (void)delta_time;
    /* A join started from the menu: the handshake and the file transfer run here, [252,4,0] enters the game */
    if (client->net_sock != INVALID_SOCKET) {
        cs2d_client_poll_network(client);
        if (client->scene != CS2D_SCENE_LOBBY) return;
    }
    client->lobby_hover = cs2d_client_any_ui_open(client)
                        ? -1
                        : entry_at(client, client->mouse_screen_x, client->mouse_screen_y);
}

static void lobby_render(cs2d_client_t *client, int screen_w, int screen_h)
{
    float sw = (float)screen_w;
    float sh = (float)screen_h;

    /* gfx/splash.bmp stretched over the whole window (banner, yellow stripe and navy backdrop) */
    cs2d_render_sprite(&s_tex_bg, sw * 0.5f, sh * 0.5f, sw, sh, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);

    /* Logo (256x256 frame of cs2d.ico) at 82x82 in the top-left corner */
    cs2d_render_sprite(&s_tex_logo, 6.0f + 41.0f, 6.0f + 41.0f, 82.0f, 82.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);

    /* Header: "Unreal Software's" / "CS2D" / U.S.G.N. and Steam login state */
    cs2d_render_text_ex(CS2D_FONT_REGULAR, LOBBY_TEXT_X, 100.0f, "Unreal Software's", 214.0f / 255.0f, 214.0f / 255.0f, 0.0f, 1.0f);
    cs2d_render_sprite(&s_tex_title, 38.5f, 128.3f, 47.0f, 19.6f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f); /* gfx/gametitle.png at ~0.49x */
    cs2d_render_sprite(&s_tex_usgn,  22.5f, 150.0f, 16.0f, 16.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
    cs2d_render_sprite(&s_tex_steam, 23.5f, 165.5f, 15.0f, 15.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, 31.0f, 141.0f, "-", 1.0f, 1.0f, 0.0f, 1.0f);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, 31.0f, 157.0f, "-", 1.0f, 1.0f, 0.0f, 1.0f);

    /* Menu entries: gray, brightened while hovered */
    for (int i = 0; i < LOBBY_ENTRY_COUNT; i++) {
        const lobby_entry_t *e = &s_entries[i];
        float c = (i == client->lobby_hover) ? 1.0f
                : (e->font == CS2D_FONT_SMALL ? 146.0f / 255.0f : 150.0f / 255.0f);
        cs2d_render_text_ex(e->font, LOBBY_TEXT_X, entry_top(e, screen_h), e->label, c, c, c, 1.0f);
    }

    /* Version and language flag in the bottom-right corner */
    const char *version = "1.0.1.8";
    float flag_x = sw - 20.0f;
    float tag_w  = cs2d_render_text_width_ex(CS2D_FONT_SMALL, "p1");
    float ver_w  = cs2d_render_text_width_ex(CS2D_FONT_SMALL, version);
    float text_y = sh - 16.0f;
    float tag_x  = flag_x - 5.0f - tag_w;
    cs2d_render_text_ex(CS2D_FONT_SMALL, tag_x - 3.0f - ver_w, text_y, version, 0.45f, 0.45f, 0.45f, 1.0f);
    cs2d_render_text_ex(CS2D_FONT_SMALL, tag_x, text_y, "p1", 0.85f, 0.85f, 0.60f, 1.0f);
    cs2d_render_sprite_frame(&s_tex_flags, 16, 11, LOBBY_FLAG_US,
                             flag_x + 8.0f, sh - 9.5f, 16.0f, 11.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0);
}

static void lobby_handle_key(cs2d_client_t *client, int key, bool pressed)
{
    (void)client;
    (void)key;
    (void)pressed;
}

static void lobby_handle_mouse(cs2d_client_t *client, float mx, float my, int button, bool pressed)
{
    if (button != 1 || !pressed) return;
    int idx = entry_at(client, mx, my);
    if (idx >= 0) lobby_activate(client, &s_entries[idx]);
}

static void lobby_handle_escape(cs2d_client_t *client)
{
    (void)client;
}

const cs2d_scene_t cs2d_scene_lobby = {
    .name          = "Lobby",
    .enter         = lobby_enter,
    .leave         = lobby_leave,
    .update        = lobby_update,
    .render        = lobby_render,
    .handle_key    = lobby_handle_key,
    .handle_mouse  = lobby_handle_mouse,
    .handle_escape = lobby_handle_escape
};
