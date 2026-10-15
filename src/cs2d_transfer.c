#include "cs2d_transfer.h"

#include <ctype.h>
#include "cs2d_platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <zlib.h>

#define XFER_RED 255, 0, 0     /* "©255000000" console prefix */
#define XFER_GRAY 170, 170, 170

static void xfer_log(const cs2d_xfer_t *x, uint8_t r, uint8_t g, uint8_t b, const char *fmt, ...)
{
    if (!x->log) return;
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    x->log(x->log_user, r, g, b, text);
}

void cs2d_xfer_init(cs2d_xfer_t *x)
{
    memset(x, 0, sizeof(*x));
    x->accept = 1;
}

void cs2d_xfer_reset(cs2d_xfer_t *x)
{
    x->request_count  = 0;
    x->cached_count   = 0;
    x->file[0]        = '\0';
    x->tsize          = 0;
    x->data_len       = 0;
    x->files_received = 0;
    x->files_total    = 0;
    x->perc           = 0.0f;
    x->prog[0]        = '\0';
    x->saved_map[0]   = '\0';
}

void cs2d_xfer_free(cs2d_xfer_t *x)
{
    free(x->requests);
    free(x->cached);
    free(x->data);
    x->requests = NULL;
    x->cached   = NULL;
    x->data     = NULL;
    x->data_cap = 0;
    cs2d_xfer_reset(x);
}

/* --- String helpers (BlitzMax Instr / ExtractExt / Lower) --- */

static bool str_has(const char *s, const char *sub) { return strstr(s, sub) != NULL; }

static bool str_starts(const char *s, const char *prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

static bool str_ends(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static void str_lower(char *s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* ExtractExt + Lower: the text after the last '.' of the file name ("" when there is none) */
static void file_ext(const char *path, char *out, size_t out_size)
{
    const char *dot   = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    const char *bs    = strrchr(path, '\\');
    if (bs > slash) slash = bs;
    out[0] = '\0';
    if (!dot || (slash && dot < slash)) return;
    snprintf(out, out_size, "%s", dot + 1);
    str_lower(out);
}

/* Instr("bmp,jpg,jpeg,png", ext): a substring test, as in the original. An empty ext does not match. */
static bool ext_in(const char *list, const char *ext) { return ext[0] && str_has(list, ext); }

/* Replace "\" with "/", Lower, drop one leading "/" */
static void norm_path(const char *path, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", path);
    for (char *c = out; *c; c++) {
        if (*c == '\\') *c = '/';
    }
    str_lower(out);
    if (out[0] == '/') memmove(out, out + 1, strlen(out));
}

static bool file_exists(const char *path)
{
    return cs2d_platform_file_exists(path);
}

static bool dir_exists(const char *path)
{
    return cs2d_platform_dir_exists(path);
}

/* --- Path conditions --- */

/* FUN_1402cb540: maps/ accepts only .map and .txt */
static int check_maps(const char *path)
{
    char ext[32];
    file_ext(path, ext, sizeof(ext));
    return (strcmp(ext, "map") == 0 || strcmp(ext, "txt") == 0) ? CS2D_XFER_OK : CS2D_XFER_BAD_MAPS_EXT;
}

int cs2d_xfer_check_path(const char *path, int accept)
{
    char p[CS2D_XFER_PATH], ext[32];
    norm_path(path, p, sizeof(p));
    file_ext(path, ext, sizeof(ext));
    bool img = ext_in("bmp,jpg,jpeg,png", ext);
    bool snd = ext_in("wav,ogg", ext);

    int r;
    if (str_starts(p, "maps/")) {
        r = check_maps(path);
    } else if (str_starts(p, "gfx/tiles/") || str_starts(p, "gfx/backgrounds/")) {
        r = img ? CS2D_XFER_OK : CS2D_XFER_BAD_TILES_EXT;
    } else if (str_starts(p, "sfx/weapons/")) {
        if (str_has(p, "_clipin.") || str_has(p, "_clipout.") || str_has(p, "_roundin.")) {
            r = CS2D_XFER_RELOAD_SOUND;
        } else if (!snd) {
            r = CS2D_XFER_WEAPONS_NOT_SND;
        } else {
            r = (accept == 2) ? CS2D_XFER_NO_SOUNDS : CS2D_XFER_OK;
        }
    } else if (!img && !snd) {
        r = CS2D_XFER_INVALID_EXT;
    } else if (accept == 2 && !img) {
        r = CS2D_XFER_NO_SOUNDS;
    } else if (accept == 3 && !snd) {
        r = CS2D_XFER_NO_IMAGES;
    } else {
        r = CS2D_XFER_OK;
    }
    if (str_has(p, ":/") || str_has(p, "./") || str_has(p, "..")) r = CS2D_XFER_INVALID_PATH;
    return r;
}

const char *cs2d_xfer_reason(int code)
{
    switch (code) {
    case CS2D_XFER_UNKNOWN:         return "unknown reason";
    case CS2D_XFER_INVALID_EXT:     return "invalid file type/ext.";
    case CS2D_XFER_INVALID_PATH:    return "invalid path";
    case CS2D_XFER_NO_IMAGES:       return "images not accepted (client setting)";
    case CS2D_XFER_NO_SOUNDS:       return "sounds not accepted (client setting)";
    case CS2D_XFER_WEAPONS_NOT_SND: return "only sounds allowed in 'sfx/weapons'";
    case CS2D_XFER_RELOAD_SOUND:    return "reload sound not allowed";
    case CS2D_XFER_BAD_TILES_EXT:   return "ext. not allowed in 'gfx/tiles' or 'gfx/backgrounds'";
    case CS2D_XFER_BAD_MAPS_EXT:    return "ext. not allowed in 'maps'";
    default:                        return "unknown error code";
    }
}

/* --- Request list --- */

static void add_request(cs2d_xfer_t *x, const char *path)
{
    if (!x->requests) {
        x->requests = calloc(CS2D_XFER_MAX_FILES, sizeof(*x->requests));
        if (!x->requests) return;
    }
    if (x->request_count >= CS2D_XFER_MAX_FILES) return;
    snprintf(x->requests[x->request_count++], CS2D_XFER_PATH, "%s", path);
}

static const char *cached_sha(const cs2d_xfer_t *x, const char *path)
{
    for (int i = 0; i < x->cached_count; i++) {
        if (strcmp(x->cached[i].path, path) == 0) return x->cached[i].sha;
    }
    return NULL;
}

/* "mods/<mod>/<rest>" -> "sys/cache/mods/<mod>/<rest>" */
static bool mod_cache_path(const char *path, char *out, size_t out_size)
{
    if (!str_starts(path, "mods/")) return false;
    const char *rest = path + 5;
    const char *slash = strchr(rest, '/');
    if (!slash) {
        snprintf(out, out_size, "sys/cache/mods/%.*s/", (int)strlen(rest), rest);
    } else {
        snprintf(out, out_size, "sys/cache/mods/%.*s/%s", (int)(slash - rest), rest, slash + 1);
    }
    return true;
}

/* FUN_1402cbcd0: a mod file that is not cached yet */
static void check_mod_file(cs2d_xfer_t *x, const char *path)
{
    char p[CS2D_XFER_PATH], ext[32], low[CS2D_XFER_PATH];
    norm_path(path, p, sizeof(p));
    file_ext(path, ext, sizeof(ext));
    snprintf(low, sizeof(low), "%s", path);
    str_lower(low);

    const char *reason = NULL;
    if (ext_in("bmp,jpg,jpeg,png", ext) || ext_in("wav,ogg", ext) || str_ends(low, "mod.txt")) {
        if (!str_has(p, ":/")) {
            xfer_log(x, XFER_GRAY, "Checking mod file '%s': Not cached. Request transfer!", path);
            add_request(x, path);
            return;
        }
        reason = "invalid path";
    } else {
        reason = str_has(p, ":/") ? "invalid path" : "invalid file type/ext.";
    }
    xfer_log(x, XFER_GRAY, "Checking mod file '%s': Does not exist and does not fit conditions (%s)! Do Not request!",
             path, reason);
}

/* FUN_1402cb9e0: "url > path" entries are HTTP downloads. Not implemented: well-formed ones are skipped silently. */
static void check_http(const cs2d_xfer_t *x, const char *entry)
{
    const char *gt = strchr(entry, '>');
    char src[CS2D_XFER_PATH], dst[CS2D_XFER_PATH];
    snprintf(src, sizeof(src), "%.*s", (int)(gt - entry), entry);
    snprintf(dst, sizeof(dst), "%s", gt + 1);
    /* Trim */
    for (char *s = src; ; ) {
        size_t n = strlen(s);
        while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
        size_t lead = strspn(s, " \t\r\n");
        if (lead) memmove(s, s + lead, strlen(s + lead) + 1);
        if (s == dst) break;
        s = dst;
    }
    if (strlen(src) <= 2 || strlen(dst) <= 2) {
        xfer_log(x, XFER_RED, "Malformed HTTP download! Source: '%s', destination: '%s' - skipping", src, dst);
    }
}

/* One name of a [252,50,1] list (FUN_1402cd970, sub 1) */
static void check_listed(cs2d_xfer_t *x, const char *path)
{
    if (strchr(path, '>')) {
        if (x->accept > 0) check_http(x, path);
        return;
    }
    char mod_path[CS2D_XFER_PATH];
    if (mod_cache_path(path, mod_path, sizeof(mod_path))) {
        if (file_exists(mod_path)) {
            xfer_log(x, XFER_GRAY, "Checking mod file '%s': Already cached, do not request.", mod_path);
        } else {
            check_mod_file(x, path);
        }
        return;
    }
    if (!file_exists(path)) {
        if (x->accept <= 0) return;
        int r = cs2d_xfer_check_path(path, x->accept);
        if (r == CS2D_XFER_OK) {
            xfer_log(x, XFER_GRAY, "Checking '%s': Does not exist. Request transfer!", path);
            add_request(x, path);
        } else {
            xfer_log(x, XFER_GRAY, "Checking '%s': Does not exist and does not fit conditions (%s)! Do not request!",
                     path, cs2d_xfer_reason(r));
        }
        return;
    }
    /* The server lists an existing map only when its checksum differs: map_mode says what to do */
    char ext[32];
    file_ext(path, ext, sizeof(ext));
    if ((x->map_mode == 1 || x->map_mode == 2) && strcmp(ext, "map") == 0) {
        xfer_log(x, XFER_GRAY, x->map_mode == 1 ? "Checking '%s': Already exists. Request transfer (save as temp.map)!"
                                                : "Checking '%s': Already exists. Request transfer (overwrite existing map)!",
                 path);
        add_request(x, path);
        return;
    }
    xfer_log(x, XFER_GRAY, "Checking '%s': Already exists.", path);
}

/* The list ended: drop files the transfer cache already holds, then "Requesting N files" */
static void list_done(cs2d_xfer_t *x)
{
    int kept = 0;
    for (int i = 0; i < x->request_count; i++) {
        const char *sha = cached_sha(x, x->requests[i]);
        char cache_file[CS2D_XFER_PATH];
        if (sha) {
            snprintf(cache_file, sizeof(cache_file), "sys/cache/transfer/%s", sha);
            if (file_exists(cache_file)) continue; /* "[ENGI] Skipping file ..." (engine log only) */
        }
        if (kept != i) memcpy(x->requests[kept], x->requests[i], CS2D_XFER_PATH);
        kept++;
    }
    x->request_count = kept;
    if (kept == 1) {
        xfer_log(x, XFER_GRAY, "Requesting 1 file from server...");
    } else {
        xfer_log(x, XFER_GRAY, "Requesting %d files from server...", kept);
    }
    x->files_received = 0;
    x->files_total    = kept;
}

int cs2d_xfer_build_requests(const cs2d_xfer_t *x, cs2d_pkt_t *pkts, int max_pkts)
{
    int n = 0;
    for (int i = 0; i < x->request_count && n < max_pkts; i += CS2D_XFER_CHUNK_FILES) {
        cs2d_pkt_t *p = &pkts[n++];
        int count = x->request_count - i;
        if (count > CS2D_XFER_CHUNK_FILES) count = CS2D_XFER_CHUNK_FILES;
        p->pos = 0;
        cs2d_pkt_w_byte(p, 252);
        cs2d_pkt_w_byte(p, 50);
        cs2d_pkt_w_byte(p, 2);
        cs2d_pkt_w_short(p, (uint16_t)count);
        for (int k = 0; k < count; k++) cs2d_pkt_w_str(p, x->requests[i + k]);
    }
    if (n < max_pkts) {
        /* Terminator: 0xffff when HTTP downloads are queued (Thttp.list not empty); we queue none */
        cs2d_pkt_t *p = &pkts[n++];
        p->pos = 0;
        cs2d_pkt_w_byte(p, 252);
        cs2d_pkt_w_byte(p, 50);
        cs2d_pkt_w_byte(p, 2);
        cs2d_pkt_w_short(p, 0);
    }
    return n;
}

/* --- Receiving --- */

static bool rd_u16(const uint8_t *buf, int len, int *pos, int *out)
{
    if (*pos + 2 > len) return false;
    *out = buf[*pos] | (buf[*pos + 1] << 8);
    *pos += 2;
    return true;
}

static bool rd_i32(const uint8_t *buf, int len, int *pos, int *out)
{
    if (*pos + 4 > len) return false;
    uint32_t v = (uint32_t)buf[*pos] | ((uint32_t)buf[*pos + 1] << 8) | ((uint32_t)buf[*pos + 2] << 16) |
                 ((uint32_t)buf[*pos + 3] << 24);
    *out = (int32_t)v;
    *pos += 4;
    return true;
}

/* Ttrans_recv.add: progress line "read/total kb - perc %" */
static void update_progress(cs2d_xfer_t *x)
{
    float perc = 0.0f;
    if (x->data_len > 0 && x->tsize > 0) {
        perc = (float)x->data_len / (float)x->tsize;
        if (perc < 0.0f) perc = 0.0f;
        if (perc > 1.0f) perc = 1.0f;
    }
    x->perc = perc;
    snprintf(x->prog, sizeof(x->prog), "%d/%d kb - %d %%", x->data_len >> 10, x->tsize / 1024, (int)(perc * 100.0f));
}

static void append_data(cs2d_xfer_t *x, const uint8_t *bytes, int n)
{
    if (x->data_len + n > x->data_cap) {
        int cap = x->data_cap ? x->data_cap : 65536;
        while (cap < x->data_len + n) cap *= 2;
        uint8_t *grown = realloc(x->data, (size_t)cap);
        if (!grown) return;
        x->data     = grown;
        x->data_cap = cap;
    }
    memcpy(x->data + x->data_len, bytes, (size_t)n);
    x->data_len += n;
}

bool cs2d_xfer_uncompress(const uint8_t *in, int in_len, uint8_t **out, int *out_len)
{
    *out     = NULL;
    *out_len = 0;
    if (in_len < 4) return false;
    int32_t raw = (int32_t)((uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24));
    int size = raw < 0 ? -raw : raw;
    uint8_t *buf = malloc(size > 0 ? (size_t)size : 1);
    if (!buf) return false;
    if (raw > 0) {
        uLongf dest_len = (uLongf)size;
        if (uncompress(buf, &dest_len, in + 4, (uLong)(in_len - 4)) != Z_OK) {
            free(buf);
            return false;
        }
        size = (int)dest_len;
    } else {
        if (size > in_len - 4) size = in_len - 4;
        memcpy(buf, in + 4, (size_t)size);
    }
    *out     = buf;
    *out_len = size;
    return true;
}

/* Creates every folder of "a/b/c/file" */
static bool make_dirs(const cs2d_xfer_t *x, const char *path)
{
    char dir[CS2D_XFER_PATH];
    snprintf(dir, sizeof(dir), "%s", path);
    for (char *c = dir; *c; c++) {
        if (*c == '\\') *c = '/';
    }
    char *last = strrchr(dir, '/');
    if (!last) return true;
    *last = '\0';
    for (char *c = dir; ; c++) {
        if (*c == '/' || *c == '\0') {
            char saved = *c;
            *c = '\0';
            if (dir[0] && !dir_exists(dir)) {
                if (!cs2d_platform_mkdir(dir)) {
                    xfer_log(x, XFER_RED, "ERROR: Failed to create directory '%s' for received files", dir);
                    return false;
                }
                xfer_log(x, XFER_GRAY, "Created directory '%s' for received files", dir);
            }
            *c = saved;
            if (saved == '\0') break;
        }
    }
    return true;
}

/* FUN_1402cbff0: "%d %b %Y" + path, one line per downloaded file */
static void log_download(const char *path)
{
    FILE *f = fopen("sys/core/dls.cache", "ab");
    if (!f) return;
    char date[32];
    time_t now = time(NULL);
    strftime(date, sizeof(date), "%d %b %Y", localtime(&now));
    fprintf(f, "%s%s\r\n", date, path);
    fclose(f);
}

/* Ttrans_recv.finish */
static cs2d_xfer_event_t finish(cs2d_xfer_t *x, const char *file)
{
    if (strcmp(x->file, file) != 0) return CS2D_XFER_EV_NONE;
    if (x->data_len < 4) {
        xfer_log(x, XFER_RED, "NET: Transmission of '%s' failed (no data received)", file);
        return CS2D_XFER_EV_FILE_FAILED;
    }
    char ext[32];
    file_ext(file, ext, sizeof(ext));
    if (strcmp(ext, "lua") == 0) {
        xfer_log(x, XFER_RED, "NET: Transmission of '%s' failed (not allowed to receive Lua files for security reasons)", file);
        return CS2D_XFER_EV_FILE_FAILED;
    }
    if (!ext_in("bmp,jpg,jpeg,png,wav,ogg,map,txt,cfg,inf,lst", ext)) {
        xfer_log(x, XFER_RED, "NET: Transmission of '%s' failed (file extension not allowed)", file);
        return CS2D_XFER_EV_FILE_FAILED;
    }
    if (str_has(file, ":/") || str_has(file, ":\\") || str_starts(file, "/")) {
        xfer_log(x, XFER_RED, "NET: Transmission of '%s' failed (absolute paths are not allowed)", file);
        return CS2D_XFER_EV_FILE_FAILED;
    }
    if (str_has(file, "./") || str_ends(file, ".")) {
        xfer_log(x, XFER_RED, "NET: Transmission of '%s' failed (path contains dots which is not allowed)", file);
        return CS2D_XFER_EV_FILE_FAILED;
    }
    if (str_starts(file, "sys")) {
        char low[CS2D_XFER_PATH];
        snprintf(low, sizeof(low), "%s", file);
        str_lower(low);
        if (!str_starts(low, "sys/cache/transfer")) {
            xfer_log(x, XFER_RED, "NET: Transmission of '%s' failed (tried to access 'sys' folder)", file);
            return CS2D_XFER_EV_FILE_FAILED;
        }
    }

    uint8_t *raw = NULL;
    int raw_len = 0;
    if (!cs2d_xfer_uncompress(x->data, x->data_len, &raw, &raw_len)) {
        xfer_log(x, XFER_RED, "NET: Transmission of '%s' failed (failed to uncompress data)", file);
        return CS2D_XFER_EV_FILE_FAILED;
    }

    char dest[CS2D_XFER_PATH];
    snprintf(dest, sizeof(dest), "%s", file);
    bool is_map = strcmp(ext, "map") == 0;
    const char *sha = cached_sha(x, file);
    if (x->map_mode == 1 && is_map) {
        xfer_log(x, XFER_GRAY, "Saving map '%s' as 'maps/temp.map' (temporary mode)", file);
        snprintf(dest, sizeof(dest), "maps/temp.map");
    } else if (str_starts(file, "mods")) {
        xfer_log(x, XFER_GRAY, "Caching mod file %s...", file);
        mod_cache_path(file, dest, sizeof(dest));
    } else if (sha) {
        xfer_log(x, XFER_GRAY, "Caching temporary file %s...", file);
        snprintf(dest, sizeof(dest), "sys/cache/transfer/%s", sha);
    }

    bool ok = make_dirs(x, dest);
    if (ok) {
        FILE *f = fopen(dest, "wb");
        ok = f && fwrite(raw, 1, (size_t)raw_len, f) == (size_t)raw_len;
        if (f) fclose(f);
    }
    free(raw);
    if (!ok) {
        xfer_log(x, XFER_RED, "NET: Transmission of '%s' failed (failed to save file)", dest);
        if (is_map) {
            xfer_log(x, XFER_RED, "ERROR: Map file is crucial for playing. Failed to join server!");
            return CS2D_XFER_EV_MAP_FAILED;
        }
        return CS2D_XFER_EV_FILE_FAILED;
    }
    if (strcmp(dest, "maps/temp.map") != 0) log_download(dest);
    if (is_map) snprintf(x->saved_map, sizeof(x->saved_map), "%s", dest);
    return CS2D_XFER_EV_FILE_SAVED;
}

cs2d_xfer_event_t cs2d_xfer_on_message(cs2d_xfer_t *x, const uint8_t *buf, int len, int *pos)
{
    if (*pos >= len) return CS2D_XFER_EV_NONE;
    int sub = buf[(*pos)++];
    char name[CS2D_XFER_PATH];

    if (sub == 1) {
        int count;
        if (!rd_u16(buf, len, pos, &count)) return CS2D_XFER_EV_NONE;
        if (count == 0) {
            list_done(x);
            return CS2D_XFER_EV_LIST_DONE;
        }
        for (int i = 0; i < count && *pos < len; i++) {
            cs2d_pkt_r_str(buf, len, pos, name, sizeof(name));
            check_listed(x, name);
        }
        return CS2D_XFER_EV_NONE;
    }

    if (sub == 3) {
        int count;
        if (!rd_u16(buf, len, pos, &count)) return CS2D_XFER_EV_NONE;
        for (int i = 0; i < count && *pos < len; i++) {
            cs2d_pkt_r_str(buf, len, pos, name, sizeof(name));
            uint32_t h[8];
            int v = 0;
            for (int k = 0; k < 8; k++) {
                if (!rd_i32(buf, len, pos, &v)) return CS2D_XFER_EV_NONE;
                h[k] = (uint32_t)v;
            }
            if (!x->cached) x->cached = calloc(CS2D_XFER_MAX_FILES, sizeof(*x->cached));
            if (!x->cached || x->cached_count >= CS2D_XFER_MAX_FILES) continue;
            cs2d_xfer_cached_t *c = &x->cached[x->cached_count++];
            snprintf(c->path, sizeof(c->path), "%s", name);
            /* FUN_1400a1750: 8 words as hex, the same text as cs2d_net_sha256_file (assumed) */
            for (int k = 0; k < 8; k++) sprintf(c->sha + k * 8, "%08x", h[k]);
        }
        return CS2D_XFER_EV_NONE;
    }

    if (sub < 5 || sub > 7) {
        xfer_log(x, 255, 150, 0, "NET: Received invalid transfer data (252/50/%d)", sub);
        *pos = len;
        return CS2D_XFER_EV_NONE;
    }

    cs2d_pkt_r_str(buf, len, pos, name, sizeof(name));
    if (sub == 5) {
        int total;
        if (!rd_i32(buf, len, pos, &total)) return CS2D_XFER_EV_NONE;
        /* Ttrans_recv.ini */
        snprintf(x->file, sizeof(x->file), "%s", name);
        x->tsize    = total;
        x->data_len = 0;
        xfer_log(x, XFER_GRAY, "Downloading '%s' from server...", name);
        x->files_received++;
        if (x->files_total < x->files_received) x->files_total = x->files_received;
    }
    int n;
    if (!rd_i32(buf, len, pos, &n) || n < 0 || *pos + n > len) {
        *pos = len;
        return CS2D_XFER_EV_NONE;
    }
    if (strcmp(x->file, name) == 0) {
        append_data(x, buf + *pos, n);
        update_progress(x);
    }
    *pos += n;
    if (sub == 7) return finish(x, name);
    return CS2D_XFER_EV_NONE;
}
