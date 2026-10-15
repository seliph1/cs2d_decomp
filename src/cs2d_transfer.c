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
    x->accept    = 1;
    x->http.sock = INVALID_SOCKET;
}

void cs2d_xfer_reset(cs2d_xfer_t *x)
{
    x->request_count  = 0;
    x->file[0]        = '\0';
    x->tsize          = 0;
    x->data_len       = 0;
    x->files_received = 0;
    x->files_total    = 0;
    x->perc           = 0.0f;
    x->prog[0]        = '\0';
    x->saved_map[0]   = '\0';
    cs2d_xfer_http_reset(x);
}

void cs2d_xfer_free(cs2d_xfer_t *x)
{
    cs2d_xfer_reset(x);
    free(x->requests);
    free(x->data);
    free(x->http.jobs);
    x->requests  = NULL;
    x->data      = NULL;
    x->data_cap  = 0;
    x->http.jobs = NULL;
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

/* --- Transfer cache (lua_filetransfer.cache, a TMap keyed by the exact path) --- */

typedef struct {
    char path[CS2D_XFER_PATH];
    char sha[65];
} xfer_cached_t;

static xfer_cached_t *s_cache;
static int            s_cache_count;
static int            s_cache_cap;

void cs2d_xfer_cache_clear(void)
{
    free(s_cache);
    s_cache       = NULL;
    s_cache_count = 0;
    s_cache_cap   = 0;
}

void cs2d_xfer_cache_add(const char *path, const char *sha)
{
    for (int i = 0; i < s_cache_count; i++) {
        if (strcmp(s_cache[i].path, path) == 0) {
            snprintf(s_cache[i].sha, sizeof(s_cache[i].sha), "%s", sha);
            return;
        }
    }
    if (s_cache_count >= s_cache_cap) {
        int cap = s_cache_cap ? s_cache_cap * 2 : 64;
        xfer_cached_t *grown = realloc(s_cache, (size_t)cap * sizeof(*grown));
        if (!grown) return;
        s_cache     = grown;
        s_cache_cap = cap;
    }
    xfer_cached_t *c = &s_cache[s_cache_count++];
    snprintf(c->path, sizeof(c->path), "%s", path);
    snprintf(c->sha, sizeof(c->sha), "%s", sha);
}

const char *cs2d_xfer_cache_get(const char *path)
{
    for (int i = 0; i < s_cache_count; i++) {
        if (strcmp(s_cache[i].path, path) == 0) return s_cache[i].sha[0] ? s_cache[i].sha : NULL;
    }
    return NULL;
}

/* Tres.add: CacheGet(rpath) <> "" -> rpath = "sys/cache/transfer/" + sha */
const char *cs2d_xfer_cache_resolve(const char *path, char *out, size_t out_size)
{
    const char *sha = path ? cs2d_xfer_cache_get(path) : NULL;
    if (sha) {
        snprintf(out, out_size, "sys/cache/transfer/%s", sha);
    } else {
        snprintf(out, out_size, "%s", path ? path : "");
    }
    return out;
}

/* FUN_1400a1750: each word as 8 hex digits, most significant first, "A".."F" (digit + 0x37) */
void cs2d_xfer_sha_text(const uint32_t h[8], char out[65])
{
    for (int k = 0; k < 8; k++) sprintf(out + k * 8, "%08X", h[k]);
}

static const char *cached_sha(const char *path) { return cs2d_xfer_cache_get(path); }

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

/* FUN_1402cb9e0: "url > path" entries are HTTP downloads, queued when the destination does not exist and
 * FUN_1402cb770 does not return 0 (a negative reason still queues it, as in the original) */
static void check_http(cs2d_xfer_t *x, const char *entry)
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
        return;
    }
    /* FileType(dst) = 0: neither a file nor a folder */
    if (!file_exists(dst) && !dir_exists(dst) && cs2d_xfer_check_path(dst, x->accept) != CS2D_XFER_UNKNOWN) {
        cs2d_xfer_http_addjob(x, src, dst);
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
        const char *sha = cached_sha(x->requests[i]);
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
        /* Terminator: 0xffff when HTTP downloads are queued (Thttp.list not empty) */
        cs2d_pkt_t *p = &pkts[n++];
        p->pos = 0;
        cs2d_pkt_w_byte(p, 252);
        cs2d_pkt_w_byte(p, 50);
        cs2d_pkt_w_byte(p, 2);
        cs2d_pkt_w_short(p, x->http.count > 0 ? 0xffff : 0);
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
    const char *sha = cached_sha(file);
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
            /* "Adding file to transfer cache: ..." goes to the engine log only (thunk_FUN_1408db980) */
            char sha[65];
            cs2d_xfer_sha_text(h, sha);
            cs2d_xfer_cache_add(name, sha);
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

/* --- HTTP downloads (Thttp) --- */

/* Download size limits for "download" 4..10 (DAT_140bc89e0); other values allow 16 MB */
static const int s_http_limits[7] = { 51200, 102400, 256000, 512000, 1048576, 2097152, 3145728 };

int cs2d_xfer_http_size_limit(int download)
{
    if (download >= 4 && download <= 10) return s_http_limits[download - 4];
    return 0x1000000;
}

bool cs2d_xfer_http_active(const cs2d_xfer_t *x)
{
    return x->http.count > 0 && x->files_total <= x->files_received;
}

static void http_close(cs2d_http_t *h)
{
    if (h->sock != INVALID_SOCKET) closesocket(h->sock);
    if (h->file) fclose(h->file);
    h->sock = INVALID_SOCKET;
    h->file = NULL;
    h->rpos = h->rlen = 0;
}

/* Thttp.reset */
void cs2d_xfer_http_reset(cs2d_xfer_t *x)
{
    cs2d_http_t *h = &x->http;
    http_close(h);
    h->count       = 0;
    h->bytestoread = 0;
    h->fails       = 0;
    h->state       = 0;
    h->read        = 0;
    h->i           = 0;
    h->total       = 0;
}

void cs2d_xfer_http_addjob(cs2d_xfer_t *x, const char *url, const char *dest)
{
    cs2d_http_t *h = &x->http;
    for (int i = 0; i < h->count; i++) {
        if (strcmp(h->jobs[i].url, url) == 0 && strcmp(h->jobs[i].dest, dest) == 0) return;
    }
    if (!h->jobs) {
        h->jobs = calloc(CS2D_XFER_MAX_FILES, sizeof(*h->jobs));
        if (!h->jobs) return;
    }
    if (h->count >= CS2D_XFER_MAX_FILES) return;
    snprintf(h->jobs[h->count].url, CS2D_XFER_PATH, "%s", url);
    snprintf(h->jobs[h->count].dest, CS2D_XFER_PATH, "%s", dest);
    h->count++;
    h->total++;
}

/* Blocking socket stream reads (BlitzMax TSocketStream): -1 when the connection closed */
static int http_byte(cs2d_http_t *h)
{
    if (h->rpos >= h->rlen) {
        int n = recv(h->sock, (char *)h->rbuf, (int)sizeof(h->rbuf), 0);
        if (n <= 0) return -1;
        h->rpos = 0;
        h->rlen = n;
    }
    return h->rbuf[h->rpos++];
}

/* ReadLine: up to LF, CR dropped; false at the end of the stream with nothing read (Eof) */
static bool http_line(cs2d_http_t *h, char *out, size_t out_size)
{
    size_t n = 0;
    int c = 0;
    bool any = false;
    while ((c = http_byte(h)) >= 0) {
        any = true;
        if (c == '\n') break;
        if (c != '\r' && n + 1 < out_size) out[n++] = (char)c;
    }
    out[n] = '\0';
    return any;
}

/* ReadBytes: blocks until n bytes arrived; returns how many did */
static int http_read(cs2d_http_t *h, uint8_t *out, int n)
{
    int got = 0;
    while (got < n) {
        if (h->rpos < h->rlen) {
            int k = h->rlen - h->rpos;
            if (k > n - got) k = n - got;
            memcpy(out + got, h->rbuf + h->rpos, (size_t)k);
            h->rpos += k;
            got += k;
            continue;
        }
        int r = recv(h->sock, (char *)out + got, n - got, 0);
        if (r <= 0) break;
        got += r;
    }
    return got;
}

/* The current job is over (LAB_1402ccc38): close it and drop it; true when the list is empty now */
static bool http_job_done(cs2d_http_t *h)
{
    http_close(h);
    if (h->count > 0) {
        memmove(&h->jobs[0], &h->jobs[1], (size_t)(h->count - 1) * sizeof(*h->jobs));
        h->count--;
    }
    h->bytestoread = 0;
    h->fails       = 0;
    h->state       = 0;
    h->read        = 0;
    h->i           = 0;
    return h->count == 0;
}

/* Status 200: every folder of ExtractDir(dest) is checked and created, then the file is opened */
static bool http_open_dest(cs2d_xfer_t *x)
{
    cs2d_http_t *h = &x->http;
    char dir[CS2D_XFER_PATH];
    snprintf(dir, sizeof(dir), "%s", h->dest);
    for (char *c = dir; *c; c++) {
        if (*c == '\\') *c = '/';
    }
    char *last = strrchr(dir, '/');
    if (last) {
        *last = '\0';
    } else {
        dir[0] = '\0'; /* No folder: nothing to create (the original calls CreateDir("") here) */
    }
    char path[CS2D_XFER_PATH] = "";
    char *part = dir;
    for (int index = 0; dir[0]; index++) {
        char *slash = strchr(part, '/');
        if (slash) *slash = '\0';
        if (strcmp(part, ".") == 0 || strcmp(part, "..") == 0) {
            xfer_log(x, XFER_RED, "NET: Transmission of '%s' failed (path contains dots which is not allowed)", h->dest);
            return false;
        }
        if (index == 0) {
            char low[CS2D_XFER_PATH];
            snprintf(low, sizeof(low), "%s", part);
            str_lower(low);
            if (strcmp(low, "sys") == 0) {
                xfer_log(x, XFER_RED, "NET: Transmission of '%s' failed (tried to access 'sys' folder)", h->dest);
                return false;
            }
            snprintf(path, sizeof(path), "%s", part);
        } else {
            size_t n = strlen(path);
            snprintf(path + n, sizeof(path) - n, "/%s", part);
        }
        if (!dir_exists(path)) {
            if (!cs2d_platform_mkdir(path)) {
                /* "created" as in the original string */
                xfer_log(x, XFER_RED, "ERROR: Failed to created directory '%s' for received files", path);
                return false;
            }
            xfer_log(x, XFER_GRAY, "Created directory '%s' for received files", path);
        }
        if (!slash) break;
        part = slash + 1;
    }
    h->file = fopen(h->dest, "wb");
    if (!h->file) {
        xfer_log(x, XFER_RED, "NET: Failed To write '%s'", h->dest); /* closing quote assumed */
        return false;
    }
    return true;
}

bool cs2d_xfer_http_get(cs2d_xfer_t *x, int size_limit)
{
    cs2d_http_t *h = &x->http;
    if (h->count == 0) return false;

    if (h->state == 1) {
        if (!h->file) {
            xfer_log(x, XFER_GRAY, "Failed to write '%s'", h->dest);
        } else if (h->sock == INVALID_SOCKET) {
            xfer_log(x, XFER_GRAY, "HTTP connection lost");
        } else if (h->i < h->bytestoread) {
            /* 4 KB per call */
            uint8_t block[0x1000];
            int n = h->bytestoread - h->i;
            if (n > 0x1000) n = 0x1000;
            int got = http_read(h, block, n);
            if (got < n) memset(block + got, 0, (size_t)(n - got));
            h->read += n;
            fwrite(block, 1, (size_t)n, h->file);
            h->i += 0x1000;
            return false;
        } else {
            xfer_log(x, XFER_GRAY, "File loaded (%d b)", h->bytestoread);
            fclose(h->file);
            h->file = NULL;
            log_download(h->dest);
        }
        return http_job_done(h);
    }

    /* State 0: connect to the first job's host, send the request, read the headers */
    snprintf(h->url, sizeof(h->url), "%s", h->jobs[0].url);
    snprintf(h->dest, sizeof(h->dest), "%s", h->jobs[0].dest);
    char url[CS2D_XFER_PATH * 3];
    const char *u = h->url;
    if (str_starts(u, "http://")) u += 7;
    size_t n = 0;
    for (; *u && n + 4 < sizeof(url); u++) { /* " " -> "%20" */
        if (*u == ' ') {
            memcpy(url + n, "%20", 3);
            n += 3;
        } else {
            url[n++] = *u;
        }
    }
    url[n] = '\0';
    char host[CS2D_XFER_PATH * 3];
    const char *file = "/";
    char *slash = strchr(url, '/');
    snprintf(host, sizeof(host), "%.*s", slash ? (int)(slash - url) : (int)strlen(url), url);
    if (slash) file = slash;

    h->sock = cs2d_platform_tcp_connect(host, "80");
    if (h->sock == INVALID_SOCKET) {
        if (++h->fails < 11) return false;
        xfer_log(x, XFER_GRAY, "HTTP Timeout");
        return http_job_done(h);
    }
    h->rpos = h->rlen = 0;
    xfer_log(x, XFER_GRAY, "HTTP download '%s' to '%s'", h->url, h->dest); /* closing quote assumed */
    char req[CS2D_XFER_PATH * 8];
    int len = snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: CS2D\r\nAccept: */*\r\n\r\n",
                       file, host);
    send(h->sock, req, len, 0);

    /* Status: the 3 characters after the first space of an "HTTP..." line */
    char line[1024], code[16] = "";
    if (http_line(h, line, sizeof(line)) && str_starts(line, "HTTP")) {
        const char *sp = strchr(line, ' ');
        if (sp) snprintf(code, sizeof(code), "%.3s", sp + 1);
    }
    /* Headers up to the empty line: Lower(key + ": ") selects content-length / location */
    for (;;) {
        bool more = http_line(h, line, sizeof(line));
        const char *sep = strstr(line, ": ");
        if (sep) {
            char key[64];
            snprintf(key, sizeof(key), "%.*s", (int)(sep + 2 - line), line);
            str_lower(key);
            if (strcmp(key, "content-length: ") == 0) h->bytestoread = atoi(sep + 2);
        }
        if (line[0] == '\0' || !more) break;
    }
    if (size_limit < h->bytestoread) snprintf(code, sizeof(code), "sizelimit");

    if (strcmp(code, "200") == 0) {
        if (http_open_dest(x)) {
            h->state = 1;
            h->read  = 0;
            h->i     = 0;
            return false;
        }
    } else if (strcmp(code, "404") == 0) {
        xfer_log(x, XFER_GRAY, "File not found (404)");
    } else if (strcmp(code, "301") == 0 || strcmp(code, "302") == 0 || strcmp(code, "303") == 0 ||
               strcmp(code, "307") == 0) {
        /* The original keeps "location" in its url global and returns with the job still queued; the next call
         * reads file_url again, so a redirect is retried from the same address every frame (not followed) */
        http_close(h);
        h->bytestoread = 0;
        return false;
    } else if (strcmp(code, "sizelimit") == 0) {
        xfer_log(x, XFER_GRAY, "File is too large and will not be downloaded");
    } else {
        xfer_log(x, XFER_GRAY, "Unexpected reply (%s)", code); /* closing parenthesis assumed */
    }
    return http_job_done(h);
}
