#include "cs2d_font.h"
#include "cs2d_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define FONT_MAX_CHARS 256

#ifdef _WIN32
static cs2d_font_backend_t s_backend = CS2D_FONT_BACKEND_GDIPLUS;
#else
static cs2d_font_backend_t s_backend = CS2D_FONT_BACKEND_STB;
#endif

/* UI font ids: face (0 = Liberation Sans, 1 = Source Sans Pro Black), pixel size, bold */
static const struct { int face; float px; bool bold; } k_ui_fonts[CS2D_FONT_COUNT] = {
    { 0, 13.0f, false }, /* CS2D_FONT_REGULAR */
    { 0, 10.0f, false }, /* CS2D_FONT_SMALL */
    { 0, 13.0f, true  }, /* CS2D_FONT_BOLD */
    { 1, 18.0f, false }, /* CS2D_FONT_TITLE */
    { 0, 15.0f, false }, /* CS2D_FONT_MENU */
};

/* Font of a UI id or a CS2D_FONT_SIZED id (same style as CS2D_FONT_REGULAR: both are LoadImageFont flags 5) */
static void font_style(cs2d_font_id_t font_id, int *face, float *px, bool *bold)
{
    if ((int)font_id >= CS2D_FONT_SIZED_BASE + 10 && (int)font_id <= CS2D_FONT_SIZED_BASE + 100) {
        *face = 0;
        *px   = (float)((int)font_id - CS2D_FONT_SIZED_BASE);
        *bold = false;
        return;
    }
    if (font_id < 0 || font_id >= CS2D_FONT_COUNT) font_id = CS2D_FONT_REGULAR;
    *face = k_ui_fonts[font_id].face;
    *px   = k_ui_fonts[font_id].px;
    *bold = k_ui_fonts[font_id].bold;
}

/* --- Text decoding --- */

/* Windows-1252 0x80..0x9F (0 = undefined, kept as the byte value like MultiByteToWideChar does) */
static const uint16_t k_cp1252_high[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

static int decode_utf8_strict(const unsigned char *s, int len, uint32_t *out, int max_out)
{
    int n = 0;
    for (int i = 0; i < len;) {
        uint32_t c = s[i];
        int extra;
        uint32_t min;
        if (c < 0x80)              { extra = 0; min = 0; }
        else if ((c & 0xE0) == 0xC0) { extra = 1; min = 0x80;    c &= 0x1F; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; min = 0x800;   c &= 0x0F; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; min = 0x10000; c &= 0x07; }
        else return -1;
        for (int k = 1; k <= extra; k++) {
            if (i + k >= len || (s[i + k] & 0xC0) != 0x80) return -1;
            c = (c << 6) | (s[i + k] & 0x3F);
        }
        if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return -1;
        if (n < max_out) out[n++] = c;
        i += extra + 1;
    }
    return n;
}

int cs2d_font_decode(const char *text, int len, uint32_t *out, int max_out)
{
    if (!text || max_out <= 0) return 0;
    if (len < 0) len = (int)strlen(text);
    const unsigned char *s = (const unsigned char *)text;
    int n = decode_utf8_strict(s, len, out, max_out);
    if (n >= 0) return n;
    n = 0;
    for (int i = 0; i < len && n < max_out; i++) {
        out[n++] = (s[i] >= 0x80 && s[i] < 0xA0) ? k_cp1252_high[s[i] - 0x80] : s[i];
    }
    return n;
}

/* --- stb_truetype backend --- */

typedef struct {
    bool           ok;
    unsigned char *data;
    stbtt_fontinfo info;
    int            upem;
    int            ascent;   /* OS/2 usWinAscent: the baseline below the top of the layout box */
    int            height;   /* OS/2 usWinAscent + usWinDescent: GdipMeasureString's line height */
    int            bold;     /* synthetic bold widening, design units */
} stb_face_t;

static stb_face_t s_stb[2];

static bool stb_load_face(stb_face_t *f, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    f->data = (unsigned char *)malloc((size_t)size);
    if (!f->data || fread(f->data, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        free(f->data);
        f->data = NULL;
        return false;
    }
    fclose(fp);
    if (!stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0))) return false;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    f->upem   = ttUSHORT(f->info.data + f->info.head + 18);
    f->ascent = asc;
    f->height = asc - desc;
    stbtt_uint32 os2 = stbtt__find_table(f->data, (stbtt_uint32)f->info.fontstart, "OS/2");
    if (os2) {
        f->ascent = ttUSHORT(f->data + os2 + 74);
        f->height = f->ascent + ttUSHORT(f->data + os2 + 76);
    }
    /* Measured on GdipMeasureString (Liberation Sans 13 px, FontStyleBold, no bold face in the collection):
     * every glyph with an outline is 0.2603 px = 41 / 2048 em wider, spaces unchanged; 41 = round(2% em) */
    f->bold = (int)floorf((float)f->upem * 0.02f + 0.5f);
    f->ok   = true;
    return true;
}

static bool stb_start(void)
{
    stb_load_face(&s_stb[0], "gfx/fonts/liberationsans.ttf");
    stb_load_face(&s_stb[1], "gfx/fonts/sourcesans.ttf");
    if (!s_stb[1].ok) s_stb[1] = s_stb[0];
    return s_stb[0].ok;
}

static float stb_advance(const stb_face_t *f, float scale, bool bold, int glyph)
{
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&f->info, glyph, &adv, &lsb);
    if (bold && !stbtt_IsGlyphEmpty((stbtt_fontinfo *)&f->info, glyph)) adv += f->bold;
    return (float)adv * scale;
}

static bool stb_measure(cs2d_font_id_t font_id, const char *text, int len, float *out_w, float *out_h)
{
    int face; float px; bool bold;
    font_style(font_id, &face, &px, &bold);
    const stb_face_t *f = &s_stb[face];
    float scale = px / (float)f->upem;
    uint32_t cps[FONT_MAX_CHARS];
    int n = cs2d_font_decode(text, len, cps, FONT_MAX_CHARS);
    float w = 0.0f;
    for (int i = 0; i < n; i++) w += stb_advance(f, scale, bold, stbtt_FindGlyphIndex(&f->info, (int)cps[i]));
    if (out_w) *out_w = w;
    if (out_h) *out_h = (float)f->height * scale;
    return n > 0;
}

static void stb_blit(uint8_t *cov, int w, int h, const uint8_t *g, int gw, int gh, int ox, int oy, bool max)
{
    for (int y = 0; y < gh; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= h) continue;
        for (int x = 0; x < gw; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= w) continue;
            unsigned a = g[y * gw + x], b = cov[dy * w + dx];
            if (max) cov[dy * w + dx] = (uint8_t)(a > b ? a : b);
            else     cov[dy * w + dx] = (uint8_t)(a + b - (a * b + 127) / 255); /* "over" of two coverages */
        }
    }
}

static void stb_draw_glyph(const stb_face_t *f, float scale, int gi, float x, float y,
                           uint8_t *dst, int w, int h, uint8_t *glyph, bool max)
{
    float ix = floorf(x), iy = floorf(y);
    float sx = x - ix, sy = y - iy;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&f->info, gi, scale, scale, sx, sy, &x0, &y0, &x1, &y1);
    int gw = x1 - x0, gh = y1 - y0;
    if (gw <= 0 || gh <= 0 || gw > 160 || gh > 160) return;
    stbtt_MakeGlyphBitmapSubpixel(&f->info, glyph, gw, gh, gw, scale, scale, sx, sy, gi);
    stb_blit(dst, w, h, glyph, gw, gh, (int)ix + x0, (int)iy + y0, max);
}

/*
 * Synthetic bold stroke: GDI+ widens each advance by only 41 / 2048 em but draws the stems heavier. Of the
 * widenings tried on the 13 px bold font (0.25 .. 1.5 px in 0.25 steps) 1 px gave the smallest mean coverage
 * difference to GDI+ (12.5 / 255; 25 with the advance's 0.26 px). Measured at 13 px only, the one bold size.
 */
#define STB_BOLD_STROKE_PX 1.0f
#define STB_BOLD_STEP_PX   0.25f

static bool stb_raster(cs2d_font_id_t font_id, const char *text, uint8_t *cov, int w, int h)
{
    int face; float px; bool bold;
    font_style(font_id, &face, &px, &bold);
    const stb_face_t *f = &s_stb[face];
    float scale = px / (float)f->upem;
    uint32_t cps[FONT_MAX_CHARS];
    int n = cs2d_font_decode(text, -1, cps, FONT_MAX_CHARS);
    /* Baseline on a whole pixel: of the placements tried (fractional, +-1/8 px steps, floor, round) rounding
     * gave the smallest mean coverage difference to GDI+ (10.7 / 255, English strings, 13 px). GDI+ itself
     * point-samples (16 coverage levels); no 4x4 .. 16x4 sample grid tried came closer than exact area. */
    float baseline = floorf((float)f->ascent * scale + 0.5f);
    float pen = 0.0f;
    static uint8_t glyph[160 * 160];
    for (int i = 0; i < n; i++) {
        int gi = stbtt_FindGlyphIndex(&f->info, (int)cps[i]);
        stb_draw_glyph(f, scale, gi, pen, baseline, cov, w, h, glyph, false);
        if (bold) {
            /* Widened stroke: the union of the glyph and its copies shifted right up to the stroke width */
            for (float dx = STB_BOLD_STEP_PX; dx <= STB_BOLD_STROKE_PX + 0.001f; dx += STB_BOLD_STEP_PX) {
                stb_draw_glyph(f, scale, gi, pen + dx, baseline, cov, w, h, glyph, true);
            }
        }
        pen += stb_advance(f, scale, bold, gi);
    }
    return n > 0;
}

/* --- GDI+ backend (Windows) --- */

#ifdef _WIN32
/* Minimal GDI+ Flat API bindings */
typedef int GpStatus;
typedef void GpBitmap;
typedef void GpImage;
typedef void GpGraphics;
typedef void GpFontCollection;
typedef void GpFontFamily;
typedef void GpFont;
typedef void GpStringFormat;
typedef void GpBrush;
typedef void GpSolidFill;
typedef uint32_t ARGB;

typedef struct {
    float X;
    float Y;
    float Width;
    float Height;
} GpRectF;

typedef struct {
    uint32_t GdiplusVersion;
    void    *DebugEventCallback;
    BOOL     SuppressBackgroundThread;
    BOOL     SuppressExternalCodecs;
} GdiplusStartupInput;

typedef GpStatus (WINAPI *PFN_GdiplusStartup)(ULONG_PTR *token, const GdiplusStartupInput *input, void *output);
typedef GpStatus (WINAPI *PFN_GdipCreateBitmapFromScan0)(INT width, INT height, INT stride, INT format, BYTE *scan0, GpBitmap **bitmap);
typedef GpStatus (WINAPI *PFN_GdipGetImageGraphicsContext)(GpImage *image, GpGraphics **graphics);
typedef GpStatus (WINAPI *PFN_GdipSetTextRenderingHint)(GpGraphics *graphics, INT mode);
typedef GpStatus (WINAPI *PFN_GdipNewPrivateFontCollection)(GpFontCollection **fontCollection);
typedef GpStatus (WINAPI *PFN_GdipPrivateAddFontFile)(GpFontCollection *fontCollection, const WCHAR *filename);
typedef GpStatus (WINAPI *PFN_GdipCreateFontFamilyFromName)(const WCHAR *name, GpFontCollection *fontCollection, GpFontFamily **fontFamily);
typedef GpStatus (WINAPI *PFN_GdipCreateFont)(const GpFontFamily *fontFamily, float emSize, INT style, INT unit, GpFont **font);
typedef GpStatus (WINAPI *PFN_GdipCreateSolidFill)(ARGB color, GpSolidFill **brush);
typedef GpStatus (WINAPI *PFN_GdipStringFormatGetGenericTypographic)(GpStringFormat **format);
typedef GpStatus (WINAPI *PFN_GdipCloneStringFormat)(const GpStringFormat *format, GpStringFormat **newFormat);
typedef GpStatus (WINAPI *PFN_GdipSetStringFormatFlags)(GpStringFormat *format, INT flags);
typedef GpStatus (WINAPI *PFN_GdipDrawString)(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font, const GpRectF *layoutRect, const GpStringFormat *stringFormat, const GpBrush *brush);
typedef GpStatus (WINAPI *PFN_GdipMeasureString)(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font, const GpRectF *layoutRect, const GpStringFormat *stringFormat, GpRectF *boundingBox, INT *codepointsFitted, INT *linesFilled);

static PFN_GdipCreateFont    pGdipCreateFont = NULL;
static PFN_GdipDrawString    pGdipDrawString = NULL;
static PFN_GdipMeasureString pGdipMeasureString = NULL;

#define GDIP_SCRATCH_W 1024
#define GDIP_SCRATCH_H 128 /* hudtxt sizes up to 100 px */

static GpFont         *s_gp_fonts[CS2D_FONT_COUNT] = {0};
static GpFont         *s_gp_sized[101] = {0}; /* CS2D_FONT_SIZED(px), created on first use */
static GpFontFamily   *s_fam_lib = NULL;
static GpStringFormat *s_str_format = NULL;
static GpSolidFill    *s_white_brush = NULL;
static uint32_t       *s_scratch_pixels = NULL;
static GpGraphics     *s_scratch_gfx = NULL;

#define GDIP_LOAD(mod, name) (PFN_##name)(void *)GetProcAddress(mod, #name)

static bool gdip_start(void)
{
    HMODULE mod = LoadLibraryA("gdiplus.dll");
    if (!mod) return false;

    PFN_GdiplusStartup pGdiplusStartup = GDIP_LOAD(mod, GdiplusStartup);
    PFN_GdipCreateBitmapFromScan0 pGdipCreateBitmapFromScan0 = GDIP_LOAD(mod, GdipCreateBitmapFromScan0);
    PFN_GdipGetImageGraphicsContext pGdipGetImageGraphicsContext = GDIP_LOAD(mod, GdipGetImageGraphicsContext);
    PFN_GdipSetTextRenderingHint pGdipSetTextRenderingHint = GDIP_LOAD(mod, GdipSetTextRenderingHint);
    PFN_GdipNewPrivateFontCollection pGdipNewPrivateFontCollection = GDIP_LOAD(mod, GdipNewPrivateFontCollection);
    PFN_GdipPrivateAddFontFile pGdipPrivateAddFontFile = GDIP_LOAD(mod, GdipPrivateAddFontFile);
    PFN_GdipCreateFontFamilyFromName pGdipCreateFontFamilyFromName = GDIP_LOAD(mod, GdipCreateFontFamilyFromName);
    PFN_GdipCreateSolidFill pGdipCreateSolidFill = GDIP_LOAD(mod, GdipCreateSolidFill);
    PFN_GdipStringFormatGetGenericTypographic pGdipStringFormatGetGenericTypographic = GDIP_LOAD(mod, GdipStringFormatGetGenericTypographic);
    PFN_GdipCloneStringFormat pGdipCloneStringFormat = GDIP_LOAD(mod, GdipCloneStringFormat);
    PFN_GdipSetStringFormatFlags pGdipSetStringFormatFlags = GDIP_LOAD(mod, GdipSetStringFormatFlags);
    pGdipCreateFont    = GDIP_LOAD(mod, GdipCreateFont);
    pGdipDrawString    = GDIP_LOAD(mod, GdipDrawString);
    pGdipMeasureString = GDIP_LOAD(mod, GdipMeasureString);

    if (!pGdiplusStartup || !pGdipCreateBitmapFromScan0 || !pGdipGetImageGraphicsContext || !pGdipSetTextRenderingHint ||
        !pGdipNewPrivateFontCollection || !pGdipPrivateAddFontFile || !pGdipCreateFontFamilyFromName ||
        !pGdipCreateFont || !pGdipCreateSolidFill || !pGdipDrawString || !pGdipMeasureString) {
        return false;
    }

    ULONG_PTR token = 0;
    GdiplusStartupInput input = { 1, NULL, FALSE, FALSE };
    if (pGdiplusStartup(&token, &input, NULL) != 0) return false;

    s_scratch_pixels = (uint32_t *)calloc((size_t)GDIP_SCRATCH_W * (size_t)GDIP_SCRATCH_H, 4);
    /* PixelFormat32bppARGB = 0x26200A */
    GpBitmap *bitmap = NULL;
    pGdipCreateBitmapFromScan0(GDIP_SCRATCH_W, GDIP_SCRATCH_H, GDIP_SCRATCH_W * 4, 0x26200A,
                               (BYTE *)s_scratch_pixels, &bitmap);
    if (bitmap) {
        pGdipGetImageGraphicsContext((GpImage *)bitmap, &s_scratch_gfx);
        if (s_scratch_gfx) {
            /* TextRenderingHintAntiAlias = 4 (smooth 256-level grayscale without broken GridFit bytecode) */
            pGdipSetTextRenderingHint(s_scratch_gfx, 4);
        }
    }

    pGdipCreateSolidFill(0xFFFFFFFFu, &s_white_brush);

    const GpStringFormat *generic_typo = NULL;
    if (pGdipStringFormatGetGenericTypographic &&
        pGdipStringFormatGetGenericTypographic((GpStringFormat **)&generic_typo) == 0 &&
        generic_typo && pGdipCloneStringFormat) {
        pGdipCloneStringFormat(generic_typo, &s_str_format);
        if (s_str_format && pGdipSetStringFormatFlags) {
            /* StringFormatFlagsMeasureTrailingSpaces (0x0800) | StringFormatFlagsNoWrap (0x1000) | StringFormatFlagsNoClip (0x4000) */
            pGdipSetStringFormatFlags(s_str_format, 0x0800 | 0x1000 | 0x4000);
        }
    }

    GpFontCollection *collection = NULL;
    pGdipNewPrivateFontCollection(&collection);
    if (collection) {
        WCHAR wpath1[MAX_PATH] = {0};
        WCHAR wpath2[MAX_PATH] = {0};
        GetFullPathNameW(L"gfx\\fonts\\liberationsans.ttf", MAX_PATH, wpath1, NULL);
        GetFullPathNameW(L"gfx\\fonts\\sourcesans.ttf",     MAX_PATH, wpath2, NULL);
        pGdipPrivateAddFontFile(collection, wpath1);
        pGdipPrivateAddFontFile(collection, wpath2);
    }

    GpFontFamily *fam_lib = NULL;
    GpFontFamily *fam_src = NULL;
    if (collection) {
        pGdipCreateFontFamilyFromName(L"Liberation Sans",       collection, &fam_lib);
        pGdipCreateFontFamilyFromName(L"Source Sans Pro Black", collection, &fam_src);
    }
    if (!fam_lib) pGdipCreateFontFamilyFromName(L"Segoe UI", NULL, &fam_lib);
    if (!fam_lib) pGdipCreateFontFamilyFromName(L"Arial", NULL, &fam_lib);
    if (!fam_src) fam_src = fam_lib;

    /* UnitPixel = 2; FontStyleRegular = 0, FontStyleBold = 1 */
    s_fam_lib = fam_lib;
    for (int i = 0; i < CS2D_FONT_COUNT; i++) {
        GpFontFamily *fam = k_ui_fonts[i].face ? fam_src : fam_lib;
        if (fam) pGdipCreateFont(fam, k_ui_fonts[i].px, k_ui_fonts[i].bold ? 1 : 0, 2, &s_gp_fonts[i]);
    }
    return s_scratch_gfx && s_gp_fonts[CS2D_FONT_REGULAR];
}

static GpFont *gdip_font(cs2d_font_id_t font_id)
{
    if ((int)font_id >= CS2D_FONT_SIZED_BASE + 10 && (int)font_id <= CS2D_FONT_SIZED_BASE + 100) {
        int px = (int)font_id - CS2D_FONT_SIZED_BASE;
        if (!s_gp_sized[px] && s_fam_lib) pGdipCreateFont(s_fam_lib, (float)px, 0, 2, &s_gp_sized[px]);
        if (s_gp_sized[px]) return s_gp_sized[px];
    }
    if (font_id < 0 || font_id >= CS2D_FONT_COUNT) font_id = CS2D_FONT_REGULAR;
    return s_gp_fonts[font_id] ? s_gp_fonts[font_id] : s_gp_fonts[CS2D_FONT_REGULAR];
}

/* UTF-8 (or ANSI fallback) to WCHAR */
static int gdip_wide(const char *text, int len, WCHAR *out, int max_out)
{
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, len, out, max_out);
    if (wlen <= 0) wlen = MultiByteToWideChar(CP_ACP, 0, text, len, out, max_out);
    if (wlen > 0 && len < 0) wlen--; /* exclude NUL terminator */
    return wlen;
}

static bool gdip_measure(cs2d_font_id_t font_id, const char *text, int len, float *out_w, float *out_h)
{
    GpFont *font = gdip_font(font_id);
    WCHAR wtext[FONT_MAX_CHARS];
    int wlen = gdip_wide(text, len, wtext, FONT_MAX_CHARS - 1);
    if (!font || wlen <= 0) return false;
    GpRectF layout_rect = { 0.0f, 0.0f, (float)(GDIP_SCRATCH_W - 4), (float)GDIP_SCRATCH_H };
    GpRectF bbox        = { 0.0f, 0.0f, 0.0f, 0.0f };
    pGdipMeasureString(s_scratch_gfx, wtext, wlen, font, &layout_rect, s_str_format, &bbox, NULL, NULL);
    if (out_w) *out_w = bbox.Width;
    if (out_h) *out_h = bbox.Height;
    return true;
}

static bool gdip_raster(cs2d_font_id_t font_id, const char *text, uint8_t *cov, int w, int h)
{
    GpFont *font = gdip_font(font_id);
    WCHAR wtext[FONT_MAX_CHARS];
    int wlen = gdip_wide(text, -1, wtext, FONT_MAX_CHARS - 1);
    if (!font || wlen <= 0) return false;
    if (w > GDIP_SCRATCH_W) w = GDIP_SCRATCH_W;
    if (h > GDIP_SCRATCH_H) h = GDIP_SCRATCH_H;

    /* Clear active region to opaque black (0xFF000000) so GDI+ blends white coverage directly into R/G/B */
    for (int y = 0; y < h; y++) {
        uint32_t *row = &s_scratch_pixels[y * GDIP_SCRATCH_W];
        for (int x = 0; x < w; x++) row[x] = 0xFF000000u;
    }
    GpRectF draw_rect = { 0.0f, 0.0f, (float)w, (float)h };
    pGdipDrawString(s_scratch_gfx, wtext, wlen, font, &draw_rect, s_str_format, (GpBrush *)s_white_brush);
    for (int y = 0; y < h; y++) {
        const uint32_t *src_row = &s_scratch_pixels[y * GDIP_SCRATCH_W];
        for (int x = 0; x < w; x++) cov[y * w + x] = (uint8_t)((src_row[x] >> 16) & 0xFF);
    }
    return true;
}
#endif

/* --- Public API --- */

static int s_state[2]; /* per backend: 0 = not started, 1 = ready, -1 = failed */

bool cs2d_font_backend_ready(cs2d_font_backend_t backend)
{
    if (backend != CS2D_FONT_BACKEND_GDIPLUS && backend != CS2D_FONT_BACKEND_STB) return false;
    if (s_state[backend] == 0) {
        bool ok = false;
#ifdef _WIN32
        if (backend == CS2D_FONT_BACKEND_GDIPLUS) ok = gdip_start();
#endif
        if (backend == CS2D_FONT_BACKEND_STB) ok = stb_start();
        s_state[backend] = ok ? 1 : -1;
    }
    return s_state[backend] == 1;
}

bool cs2d_font_set_backend(cs2d_font_backend_t backend)
{
#ifndef _WIN32
    if (backend == CS2D_FONT_BACKEND_GDIPLUS) return false;
#endif
    s_backend = backend;
    return true;
}

cs2d_font_backend_t cs2d_font_get_backend(void)
{
    return s_backend;
}

bool cs2d_font_ready(void)
{
    return cs2d_font_backend_ready(s_backend);
}

bool cs2d_font_measure_with(cs2d_font_backend_t backend, cs2d_font_id_t font_id, const char *text, int len,
                            float *out_w, float *out_h)
{
    if (out_w) *out_w = 0.0f;
    if (out_h) *out_h = 0.0f;
    if (!text || len == 0 || !cs2d_font_backend_ready(backend)) return false;
#ifdef _WIN32
    if (backend == CS2D_FONT_BACKEND_GDIPLUS) return gdip_measure(font_id, text, len, out_w, out_h);
#endif
    return stb_measure(font_id, text, len, out_w, out_h);
}

bool cs2d_font_raster_with(cs2d_font_backend_t backend, cs2d_font_id_t font_id, const char *text,
                           uint8_t *cov, int w, int h)
{
    if (!text || !text[0] || !cov || w <= 0 || h <= 0 || !cs2d_font_backend_ready(backend)) return false;
#ifdef _WIN32
    if (backend == CS2D_FONT_BACKEND_GDIPLUS) return gdip_raster(font_id, text, cov, w, h);
#endif
    return stb_raster(font_id, text, cov, w, h);
}

bool cs2d_font_measure(cs2d_font_id_t font_id, const char *text, int len, float *out_w, float *out_h)
{
    return cs2d_font_measure_with(s_backend, font_id, text, len, out_w, out_h);
}

bool cs2d_font_raster(cs2d_font_id_t font_id, const char *text, uint8_t *cov, int w, int h)
{
    return cs2d_font_raster_with(s_backend, font_id, text, cov, w, h);
}
