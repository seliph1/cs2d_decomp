#include "cs2d_atlas.h"
#include "cs2d_image.h"
#include "cs2d_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

typedef struct {
    int y;
    int height;
    int current_x;
} atlas_shelf_t;

typedef struct {
    atlas_shelf_t shelves[256];
    int           shelf_count;
} atlas_page_layout_t;

static int compare_items_desc(const void *a, const void *b)
{
    const cs2d_atlas_item_t *const *ia = (const cs2d_atlas_item_t *const *)a;
    const cs2d_atlas_item_t *const *ib = (const cs2d_atlas_item_t *const *)b;
    /* Compare height descending */
    if ((*ia)->h != (*ib)->h) {
        return (*ib)->h - (*ia)->h;
    }
    /* Compare width descending */
    if ((*ia)->w != (*ib)->w) {
        return (*ib)->w - (*ia)->w;
    }
    /* Compare path for determinism */
    return strcmp((*ia)->path, (*ib)->path);
}

int cs2d_atlas_pack_items(
    cs2d_atlas_item_t *items,
    int count,
    int page_w,
    int page_h,
    int pad
)
{
    if (!items || count <= 0 || page_w <= 0 || page_h <= 0) return 0;

    for (int i = 0; i < count; i++) {
        items[i].placed = false;
        items[i].page_index = -1;
        items[i].x = 0;
        items[i].y = 0;
        items[i].src_x = 0;
        items[i].src_y = 0;
    }

    cs2d_atlas_item_t **sorted = (cs2d_atlas_item_t **)malloc((size_t)count * sizeof(cs2d_atlas_item_t *));
    if (!sorted) return 0;
    for (int i = 0; i < count; i++) {
        sorted[i] = &items[i];
    }
    qsort(sorted, (size_t)count, sizeof(cs2d_atlas_item_t *), compare_items_desc);

    atlas_page_layout_t pages[64];
    memset(pages, 0, sizeof(pages));
    int total_pages = 0;

    for (int i = 0; i < count; i++) {
        cs2d_atlas_item_t *item = sorted[i];
        int bw = item->w + pad * 2;
        int bh = item->h + pad * 2;

        /* If item exceeds page dimensions, it cannot be packed */
        if (bw > page_w || bh > page_h) {
            item->placed = false;
            continue;
        }

        bool placed = false;
        /* Try existing pages */
        for (int p = 0; p < total_pages && !placed; p++) {
            atlas_page_layout_t *layout = &pages[p];
            /* Try existing shelves on page p */
            for (int s = 0; s < layout->shelf_count; s++) {
                atlas_shelf_t *sh = &layout->shelves[s];
                if (sh->current_x + bw <= page_w && bh <= sh->height) {
                    item->page_index = p;
                    item->x = sh->current_x;
                    item->y = sh->y;
                    item->src_x = sh->current_x + pad;
                    item->src_y = sh->y + pad;
                    item->placed = true;
                    sh->current_x += bw;
                    placed = true;
                    break;
                }
            }
            if (placed) break;

            /* Try starting a new shelf on page p */
            int next_y = 0;
            if (layout->shelf_count > 0) {
                atlas_shelf_t *last = &layout->shelves[layout->shelf_count - 1];
                next_y = last->y + last->height;
            }
            if (next_y + bh <= page_h && layout->shelf_count < 256) {
                atlas_shelf_t *sh = &layout->shelves[layout->shelf_count++];
                sh->y = next_y;
                sh->height = bh;
                sh->current_x = bw;

                item->page_index = p;
                item->x = 0;
                item->y = next_y;
                item->src_x = pad;
                item->src_y = next_y + pad;
                item->placed = true;
                placed = true;
                break;
            }
        }

        /* If not placed on existing pages, start a new page */
        if (!placed && total_pages < 64) {
            int p = total_pages++;
            atlas_page_layout_t *layout = &pages[p];
            atlas_shelf_t *sh = &layout->shelves[layout->shelf_count++];
            sh->y = 0;
            sh->height = bh;
            sh->current_x = bw;

            item->page_index = p;
            item->x = 0;
            item->y = 0;
            item->src_x = pad;
            item->src_y = pad;
            item->placed = true;
        }
    }

    free(sorted);
    return total_pages;
}

static bool bank_add_atlas_page(cs2d_gfx_bank_t *bank, uint32_t tid, int w, int h, int mask_mode)
{
    if (bank->atlas_page_count >= bank->atlas_page_cap) {
        int new_cap = bank->atlas_page_cap ? bank->atlas_page_cap * 2 : 8;
        cs2d_atlas_page_t *p = (cs2d_atlas_page_t *)realloc(
            bank->atlas_pages, (size_t)new_cap * sizeof(cs2d_atlas_page_t));
        if (!p) return false;
        bank->atlas_pages = p;
        bank->atlas_page_cap = new_cap;
    }
    cs2d_atlas_page_t *page = &bank->atlas_pages[bank->atlas_page_count++];
    page->id = tid;
    page->width = w;
    page->height = h;
    page->mask_mode = mask_mode;
    return true;
}

static bool bank_add_sprite_cache(
    cs2d_gfx_bank_t *bank,
    const char *path,
    int mask_mode,
    uint32_t tid,
    int w, int h,
    int gpu_w, int gpu_h,
    int src_x, int src_y,
    bool is_atlas
)
{
    if (bank->sprite_cache_count >= bank->sprite_cache_cap) {
        int new_cap = bank->sprite_cache_cap ? bank->sprite_cache_cap * 2 : 256;
        cs2d_sprite_cache_entry_t *c = (cs2d_sprite_cache_entry_t *)realloc(
            bank->sprite_cache, (size_t)new_cap * sizeof(cs2d_sprite_cache_entry_t));
        if (!c) return false;
        bank->sprite_cache = c;
        bank->sprite_cache_cap = new_cap;
    }
    cs2d_sprite_cache_entry_t *entry = &bank->sprite_cache[bank->sprite_cache_count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->mask_mode  = mask_mode;
    entry->tex.id     = tid;
    entry->tex.width  = w;
    entry->tex.height = h;
    entry->tex.gpu_w  = gpu_w;
    entry->tex.gpu_h  = gpu_h;
    entry->tex.src_x  = src_x;
    entry->tex.src_y  = src_y;
    entry->tex.cell_w = 0;
    entry->tex.cell_h = 0;
    entry->tex.pad    = 0;
    entry->is_atlas   = is_atlas;
    return true;
}

typedef struct {
    char path[128];
    int  mask_mode;
} sprite_request_t;

static void pack_and_upload_group(
    cs2d_gfx_bank_t *bank,
    cs2d_atlas_item_t *items,
    int count,
    int mask_mode
)
{
    if (count <= 0) return;

    int num_pages = cs2d_atlas_pack_items(items, count, CS2D_ATLAS_PAGE_SIZE, CS2D_ATLAS_PAGE_SIZE, CS2D_ATLAS_PAD);
    for (int p = 0; p < num_pages; p++) {
        uint32_t *page_rgba = (uint32_t *)calloc((size_t)CS2D_ATLAS_PAGE_SIZE * (size_t)CS2D_ATLAS_PAGE_SIZE, sizeof(uint32_t));
        if (!page_rgba) continue;

        for (int i = 0; i < count; i++) {
            cs2d_atlas_item_t *it = &items[i];
            if (!it->placed || it->page_index != p || !it->rgba) continue;

            int px = it->src_x;
            int py = it->src_y;
            int w  = it->w;
            int h  = it->h;

            for (int dy = -CS2D_ATLAS_PAD; dy < h + CS2D_ATLAS_PAD; dy++) {
                int sy = (dy < 0) ? 0 : ((dy >= h) ? (h - 1) : dy);
                uint32_t *dst_row = &page_rgba[(py + dy) * CS2D_ATLAS_PAGE_SIZE + px];
                const uint32_t *src_row = &it->rgba[sy * w];
                for (int dx = -CS2D_ATLAS_PAD; dx < w + CS2D_ATLAS_PAD; dx++) {
                    int sx = (dx < 0) ? 0 : ((dx >= w) ? (w - 1) : dx);
                    dst_row[dx] = src_row[sx];
                }
            }
        }

        uint32_t tid = cs2d_render_upload_atlas_page(page_rgba, CS2D_ATLAS_PAGE_SIZE, CS2D_ATLAS_PAGE_SIZE);
        free(page_rgba);

        if (tid != 0) {
            bank_add_atlas_page(bank, tid, CS2D_ATLAS_PAGE_SIZE, CS2D_ATLAS_PAGE_SIZE, mask_mode);
        }

        for (int i = 0; i < count; i++) {
            cs2d_atlas_item_t *it = &items[i];
            if (!it->placed || it->page_index != p) continue;
            bank_add_sprite_cache(bank, it->path, it->mask_mode, tid, it->w, it->h,
                                  CS2D_ATLAS_PAGE_SIZE, CS2D_ATLAS_PAGE_SIZE,
                                  it->src_x, it->src_y, true);
        }
    }

    /* Handle any unplaced items (e.g. oversize) as standalone textures */
    for (int i = 0; i < count; i++) {
        cs2d_atlas_item_t *it = &items[i];
        if (!it->placed && it->rgba) {
            cs2d_texture_t tex;
            memset(&tex, 0, sizeof(tex));
            if (!cs2d_texture_load_ex(&tex, it->path, it->mask_mode)) {
                char alt[160];
                snprintf(alt, sizeof(alt), "gfx/sprites/%s", it->path);
                cs2d_texture_load_ex(&tex, alt, it->mask_mode);
            }
            bank_add_sprite_cache(bank, it->path, it->mask_mode, tex.id, tex.width, tex.height,
                                  tex.gpu_w, tex.gpu_h, 0, 0, false);
        }
    }
}

bool cs2d_gfx_atlas_load_map(cs2d_gfx_bank_t *bank, const cs2d_map_t *map)
{
    if (!bank || !map) return false;

    uint64_t t0 = SDL_GetPerformanceCounter();

    /* Free any previous map's atlas pages and sprite cache */
    cs2d_gfx_bank_free_sprites(bank);

    /* Collect unique (path, mask_mode) requests from map entities */
    int req_cap = 256;
    int req_count = 0;
    sprite_request_t *reqs = (sprite_request_t *)malloc((size_t)req_cap * sizeof(sprite_request_t));
    if (!reqs) return false;

    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *ent = &map->entities[i];
        const char *path = NULL;
        int mask_mode = 1;

        if (ent->type == CS2D_ENT_ENV_SPRITE) {
            path = ent->str_settings[0];
            if (!path || !path[0]) continue;
            int blend_mode = ent->int_settings[9];
            bool black_mask = (blend_mode == 3 || blend_mode == 4 ||
                               strstr(path, "flare") != NULL ||
                               strstr(path, "light") != NULL);
            mask_mode = black_mask ? 3 : 1;
        } else if (ent->type == CS2D_ENT_ENV_IMAGE) {
            path = ent->str_settings[0];
            if (!path || !path[0]) continue;
            mask_mode = 1;
        } else {
            continue;
        }

        bool found = false;
        for (int k = 0; k < req_count; k++) {
            if (reqs[k].mask_mode == mask_mode && _stricmp(reqs[k].path, path) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            if (req_count >= req_cap) {
                req_cap *= 2;
                sprite_request_t *nr = (sprite_request_t *)realloc(reqs, (size_t)req_cap * sizeof(sprite_request_t));
                if (!nr) { free(reqs); return false; }
                reqs = nr;
            }
            snprintf(reqs[req_count].path, sizeof(reqs[req_count].path), "%s", path);
            reqs[req_count].mask_mode = mask_mode;
            req_count++;
        }
    }

    if (req_count == 0) {
        free(reqs);
        return true;
    }

    /* Load images and divide into mask 1 and mask 3 sets */
    cs2d_atlas_item_t *items_m1 = (cs2d_atlas_item_t *)calloc((size_t)req_count, sizeof(cs2d_atlas_item_t));
    cs2d_atlas_item_t *items_m3 = (cs2d_atlas_item_t *)calloc((size_t)req_count, sizeof(cs2d_atlas_item_t));
    int count_m1 = 0;
    int count_m3 = 0;

    for (int i = 0; i < req_count; i++) {
        cs2d_image_t img;
        memset(&img, 0, sizeof(img));
        bool ok = cs2d_image_load(&img, reqs[i].path);
        if (!ok) {
            char alt[160];
            snprintf(alt, sizeof(alt), "gfx/sprites/%s", reqs[i].path);
            ok = cs2d_image_load(&img, alt);
        }

        if (!ok) {
            /* Missing file on disk: record entry with id=0 so future lookups fail silently */
            bank_add_sprite_cache(bank, reqs[i].path, reqs[i].mask_mode, 0, 0, 0, 0, 0, 0, 0, false);
            continue;
        }

        /* Process transparency & edge dilation */
        cs2d_render_apply_mask_mode(img.rgba, img.width * img.height, img.has_embedded_alpha, reqs[i].mask_mode);
        cs2d_render_dilate_transparent(img.rgba, img.width, img.height, 0, 0);

        if (reqs[i].mask_mode == 3) {
            cs2d_atlas_item_t *it = &items_m3[count_m3++];
            snprintf(it->path, sizeof(it->path), "%s", reqs[i].path);
            it->mask_mode = 3;
            it->w = img.width;
            it->h = img.height;
            it->rgba = img.rgba;
        } else {
            cs2d_atlas_item_t *it = &items_m1[count_m1++];
            snprintf(it->path, sizeof(it->path), "%s", reqs[i].path);
            it->mask_mode = reqs[i].mask_mode;
            it->w = img.width;
            it->h = img.height;
            it->rgba = img.rgba;
        }
    }

    free(reqs);

    /* Pack and upload mask_mode 1 atlas pages */
    pack_and_upload_group(bank, items_m1, count_m1, 1);
    for (int i = 0; i < count_m1; i++) {
        if (items_m1[i].rgba) free(items_m1[i].rgba);
    }
    free(items_m1);

    /* Pack and upload mask_mode 3 atlas pages */
    pack_and_upload_group(bank, items_m3, count_m3, 3);
    for (int i = 0; i < count_m3; i++) {
        if (items_m3[i].rgba) free(items_m3[i].rgba);
    }
    free(items_m3);

    uint64_t t1 = SDL_GetPerformanceCounter();
    double ms = (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    printf("[atlas] loaded map sprite atlas in %.2f ms (%d items: %d mask1, %d mask3; %d pages)\n",
           ms, count_m1 + count_m3, count_m1, count_m3, bank->atlas_page_count);

    return true;
}

