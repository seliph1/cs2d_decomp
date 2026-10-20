#include "cs2d_bind.h"
#include "cs2d_platform.h"
#include <stdio.h>
#include <string.h>

/* The shipped sys/controls.cfg, line for line */
static const char *const s_default_binds[][2] = {
    { "I", "radio1" },
    { "O", "radio2" },
    { "P", "radio3" },
    { "Y", "say" },
    { "U", "sayteam" },
    { "Z", "voicesay" },
    { "B", "buy" },
    { ",", "buyammo1" },
    { ".", "buyammo2" },
    { "C", "buyequip" },
    { "M", "chooseteam" },
    { "0", "slot10" },
    { "1", "slot1" },
    { "2", "slot2" },
    { "3", "slot3" },
    { "4", "slot4" },
    { "5", "slot5" },
    { "6", "slot6" },
    { "7", "slot7" },
    { "8", "slot8" },
    { "9", "slot9" },
    { "tab", "showscores" },
    { "backspace", "bot" },
    { "V", "votemenu" },
    { "F1", "serverinfo" },
    { "F2", "serveraction1" },
    { "F3", "serveraction2" },
    { "F4", "serveraction3" },
    { "F6", "console" },
    { "W", "forward" },
    { "S", "back" },
    { "A", "left" },
    { "D", "right" },
    { "leftshift", "speed" },
    { "mouse1", "attack" },
    { "mouse2", "attack2" },
    { "R", "reload" },
    { "mwheelup", "invprev" },
    { "mwheeldown", "invnext" },
    { "Q", "lastinv" },
    { "N", "nightvision" },
    { "G", "drop" },
    { "E", "use" },
    { "T", "spray" },
    { "F", "flashlight" },
    { "F5", "snapshot" },
    { "leftctrl", "specmode" },
    { "space", "specplayer" },
};

void cs2d_binds_defaults(cs2d_binds_t *b)
{
    memset(b, 0, sizeof(*b));
    for (size_t i = 0; i < sizeof(s_default_binds) / sizeof(s_default_binds[0]); i++) {
        cs2d_binds_set(b, s_default_binds[i][0], s_default_binds[i][1]);
    }
}

void cs2d_binds_set(cs2d_binds_t *b, const char *key, const char *cmd)
{
    if (!b || !key || !key[0]) return;
    for (int i = 0; i < b->count; i++) {
        if (_stricmp(b->binds[i].key, key) != 0) continue;
        if (!cmd || !cmd[0]) {
            b->binds[i] = b->binds[--b->count];
        } else {
            snprintf(b->binds[i].cmd, sizeof(b->binds[i].cmd), "%s", cmd);
        }
        return;
    }
    if (!cmd || !cmd[0] || b->count >= CS2D_BIND_MAX) return;
    snprintf(b->binds[b->count].key, sizeof(b->binds[b->count].key), "%s", key);
    snprintf(b->binds[b->count].cmd, sizeof(b->binds[b->count].cmd), "%s", cmd);
    b->count++;
}

/* Next whitespace-separated token, or a "quoted" one; returns the position after it */
static const char *next_token(const char *p, char *out, size_t size)
{
    while (*p == ' ' || *p == '\t') p++;
    size_t n = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') { if (n + 1 < size) out[n++] = *p; p++; }
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') { if (n + 1 < size) out[n++] = *p; p++; }
    }
    out[n] = '\0';
    return p;
}

bool cs2d_binds_load(cs2d_binds_t *b, const char *filepath)
{
    FILE *f = fopen(filepath, "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char word[16], key[CS2D_BIND_KEY_LEN], cmd[CS2D_BIND_CMD_LEN];
        const char *p = next_token(line, word, sizeof(word));
        if (_stricmp(word, "bind") != 0) continue;
        p = next_token(p, key, sizeof(key));
        next_token(p, cmd, sizeof(cmd));
        cs2d_binds_set(b, key, cmd);
    }
    fclose(f);
    return true;
}

const char *cs2d_binds_find(const cs2d_binds_t *b, const char *key)
{
    if (!b || !key) return NULL;
    for (int i = 0; i < b->count; i++) {
        if (_stricmp(b->binds[i].key, key) == 0) return b->binds[i].cmd;
    }
    return NULL;
}

const char *cs2d_bind_key_name(int key)
{
    static char single[2];
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) {
        single[0] = (char)key;
        single[1] = '\0';
        return single;
    }
    if (key >= 'a' && key <= 'z') {
        single[0] = (char)(key - 'a' + 'A');
        single[1] = '\0';
        return single;
    }
    if (key == ',' || key == '.') {
        single[0] = (char)key;
        single[1] = '\0';
        return single;
    }
    switch (key) {
        case 0x01:        return "mouse1";
        case 0x02:        return "mouse2";
        case 0x03:        return "mouse3";
        case 0x122:       return "mwheelup";
        case 0x123:       return "mwheeldown";
        case VK_SPACE:    return "space";
        case VK_SHIFT:    return "leftshift";
        case VK_TAB:      return "tab";
        case VK_BACK:     return "backspace";
        case VK_UP:       return "uparrow";
        case VK_DOWN:     return "downarrow";
        case VK_LEFT:     return "leftarrow";
        case VK_RIGHT:    return "rightarrow";
        case VK_MENU:     return "leftalt";
        case VK_RETURN:   return "enter";
        case 0x0C:        return "clear";
        case 0x1B:        return "escape";
        case 0x21:        return "pgup";
        case 0x22:        return "pgdn";
        case 0x23:        return "end";
        case 0x24:        return "home";
        case 0x29:        return "select";
        case 0x2A:        return "print";
        case 0x2B:        return "execute";
        case 0x2C:        return "screen";
        case 0x2D:        return "ins";
        case VK_DELETE:   return "del";
        case 0x5B:        return "leftsys";
        case 0x5C:        return "rightsys";
        case 0xA0:        return "leftshift";
        case 0xA1:        return "rightshift";
        case 0xA2:        return "leftctrl";
        case 0xA3:        return "rightctrl";
        case 0xA4:        return "leftalt";
        case 0xA5:        return "rightalt";
        default:          break;
    }
    if (key >= CS2D_KEY_F1 && key < CS2D_KEY_F1 + 12) {
        static char fname[4];
        snprintf(fname, sizeof(fname), "F%d", key - CS2D_KEY_F1 + 1);
        return fname;
    }
    return NULL;
}

/* Canonical name of a Win32 VK code as used by the Lua bind list ([252,6], [227]/[228]).
 * The VK F1..F12 and numpad ranges overlap ASCII 'p'..'{' / 'a'..'o', so the client-facing
 * cs2d_bind_key_name() cannot be used for them. */
const char *cs2d_bind_vk_name(int key)
{
    if (key >= 0x70 && key <= 0x7B) {
        static char lua_fname[4];
        snprintf(lua_fname, sizeof(lua_fname), "f%d", key - 0x70 + 1);
        return lua_fname;
    }
    switch (key) {
        case 0x60: return "kp_ins";
        case 0x61: return "kp_end";
        case 0x62: return "kp_downarrow";
        case 0x63: return "kp_pgdn";
        case 0x64: return "kp_leftarrow";
        case 0x65: return "kp_5";
        case 0x66: return "kp_rightarrow";
        case 0x67: return "kp_home";
        case 0x68: return "kp_uparrow";
        case 0x69: return "kp_pgup";
        case 0x6A: return "kp_multiply";
        case 0x6B: return "kp_add";
        case 0x6C: return "kp_separator";
        case 0x6D: return "kp_subtract";
        case 0x6E: return "kp_decimal";
        case 0x6F: return "kp_divide";
        default:   return cs2d_bind_key_name(key);
    }
}

/* Lua addbind()/removebind() key name -> Win32 VK code. The codes and accepted names were
 * captured from cs2d_dedicated.exe in the [252,6] snapshot with a bind probe. */
int cs2d_bind_key_code(const char *name)
{
    if (!name || !name[0]) return 0;
    if (name[1] == '\0') {
        unsigned char c = (unsigned char)name[0];
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) return (int)c;
        return 0;
    }
    static const struct { const char *name; int code; } keys[] = {
        { "f1", 0x70 }, { "f2", 0x71 }, { "f3", 0x72 }, { "f4", 0x73 },
        { "f5", 0x74 }, { "f6", 0x75 }, { "f7", 0x76 }, { "f8", 0x77 },
        { "f9", 0x78 }, { "f10", 0x79 }, { "f11", 0x7A }, { "f12", 0x7B },
        { "mouse1", 0x01 }, { "mouse2", 0x02 }, { "mouse3", 0x03 },
        { "mwheelup", 0x122 }, { "mwheeldown", 0x123 },
        { "leftshift", 0xA0 }, { "rightshift", 0xA1 },
        { "leftctrl", 0xA2 }, { "rightctrl", 0xA3 },
        { "leftalt", 0xA4 }, { "rightalt", 0xA5 },
        { "leftsys", 0x5B }, { "rightsys", 0x5C },
        { "leftarrow", 0x25 }, { "uparrow", 0x26 },
        { "rightarrow", 0x27 }, { "downarrow", 0x28 },
        { "backspace", 0x08 }, { "tab", 0x09 }, { "clear", 0x0C },
        { "enter", 0x0D }, { "escape", 0x1B }, { "space", 0x20 },
        { "pgup", 0x21 }, { "pgdn", 0x22 }, { "end", 0x23 },
        { "home", 0x24 }, { "select", 0x29 }, { "print", 0x2A },
        { "execute", 0x2B }, { "screen", 0x2C }, { "ins", 0x2D },
        { "del", 0x2E },
        { "kp_0", 0x60 }, { "kp_ins", 0x60 },
        { "kp_1", 0x61 }, { "kp_end", 0x61 },
        { "kp_2", 0x62 }, { "kp_downarrow", 0x62 },
        { "kp_3", 0x63 }, { "kp_pgdn", 0x63 },
        { "kp_4", 0x64 }, { "kp_leftarrow", 0x64 },
        { "kp_5", 0x65 },
        { "kp_6", 0x66 }, { "kp_rightarrow", 0x66 },
        { "kp_7", 0x67 }, { "kp_home", 0x67 },
        { "kp_8", 0x68 }, { "kp_uparrow", 0x68 },
        { "kp_9", 0x69 }, { "kp_pgup", 0x69 },
        { "kp_multiply", 0x6A }, { "kp_add", 0x6B },
        { "kp_separator", 0x6C }, { "kp_subtract", 0x6D },
        { "kp_decimal", 0x6E }, { "kp_del", 0x2E },
        { "kp_divide", 0x6F }, { "kp_enter", 0x0D }, { "kp_clear", 0x0C },
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (_stricmp(keys[i].name, name) == 0) return keys[i].code;
    }
    return 0;
}

const char *cs2d_bind_mouse_name(int button)
{
    switch (button) {
        case 1:  return "mouse1";
        case 2:  return "mouse2";
        case 3:  return "mouse3";
        default: return NULL;
    }
}
