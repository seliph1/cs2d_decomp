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
        case VK_SPACE:    return "space";
        case VK_LCONTROL: return "leftctrl";
        case VK_SHIFT:    return "leftshift";
        case VK_TAB:      return "tab";
        case VK_BACK:     return "backspace";
        case VK_UP:       return "uparrow";
        case VK_DOWN:     return "downarrow";
        case VK_LEFT:     return "leftarrow";
        case VK_RIGHT:    return "rightarrow";
        case VK_MENU:     return "leftalt";
        case VK_RETURN:   return "enter";
        default:          break;
    }
    if (key >= CS2D_KEY_F1 && key < CS2D_KEY_F1 + 12) {
        static char fname[4];
        snprintf(fname, sizeof(fname), "F%d", key - CS2D_KEY_F1 + 1);
        return fname;
    }
    return NULL;
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
