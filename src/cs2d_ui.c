#include "cs2d_ui.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

cs2d_ui_id_t cs2d_ui_hash_id(const char *label, uint32_t index)
{
    if (!label) return index;
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)label; *p != '\0'; p++) {
        h ^= (uint32_t)*p;
        h *= 16777619u;
    }
    if (index != 0) {
        h ^= index;
        h *= 16777619u;
    }
    return (h == 0) ? 1u : h;
}

void cs2d_ui_init(cs2d_ui_context_t *ctx)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
}

void cs2d_ui_feed_text(cs2d_ui_context_t *ctx, const char *text)
{
    if (!ctx || !text) return;
    size_t cur_len = strlen(ctx->text_input);
    for (const char *p = text; *p != '\0'; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 32 || ch == '`' || ch == '~') continue;
        if (cur_len + 1 < sizeof(ctx->text_input)) {
            ctx->text_input[cur_len++] = (char)ch;
            ctx->text_input[cur_len]   = '\0';
        }
    }
}

static uint32_t current_mods(void)
{
    SDL_Keymod m = SDL_GetModState();
    uint32_t mods = 0;
    if (m & SDL_KMOD_SHIFT) mods |= CS2D_UI_MOD_SHIFT;
    if (m & SDL_KMOD_CTRL)  mods |= CS2D_UI_MOD_CTRL;
    if (m & SDL_KMOD_ALT)   mods |= CS2D_UI_MOD_ALT;
    return mods;
}

void cs2d_ui_feed_key(cs2d_ui_context_t *ctx, int sdl_key)
{
    if (!ctx || ctx->key_event_count >= CS2D_UI_MAX_KEY_EVENTS) return;
    cs2d_ui_key_event_t *ev = &ctx->key_events[ctx->key_event_count++];
    ev->key  = sdl_key;
    ev->mods = current_mods();
}

void cs2d_ui_clear_input(cs2d_ui_context_t *ctx)
{
    if (!ctx) return;
    ctx->text_input[0]   = '\0';
    ctx->key_event_count = 0;
}

void cs2d_ui_edit_reset(cs2d_ui_context_t *ctx)
{
    if (!ctx) return;
    ctx->edit.id = 0;
}

void cs2d_ui_begin_layout(
    cs2d_ui_context_t *ctx,
    int screen_w, int screen_h,
    float mouse_x, float mouse_y,
    bool mouse_down,
    int wheel_delta
)
{
    if (!ctx) return;

    ctx->screen_w         = screen_w;
    ctx->screen_h         = screen_h;
    ctx->mouse_x          = mouse_x;
    ctx->mouse_y          = mouse_y;
    ctx->mouse_pressed    = (mouse_down && !ctx->prev_mouse_down);
    ctx->mouse_released   = (!mouse_down && ctx->prev_mouse_down);
    ctx->mouse_down       = mouse_down;
    ctx->prev_mouse_down  = mouse_down;
    ctx->wheel_delta      = wheel_delta;

    ctx->node_count        = 0;
    ctx->open_stack_depth  = 0;
    ctx->render_cmd_count  = 0;
    ctx->hot_id            = 0;
    ctx->focus_order_count = 0;
    ctx->focus_move        = 0;

    /* Multi-click detection: presses within 400ms and 4px of the previous one count up to 3 */
    if (ctx->mouse_pressed) {
        uint32_t now  = (uint32_t)GetTickCount();
        bool     close_by = fabsf(mouse_x - ctx->last_click_x) <= 4.0f && fabsf(mouse_y - ctx->last_click_y) <= 4.0f;
        ctx->click_count   = (close_by && now - ctx->last_click_ms <= 400u && ctx->click_count < 3) ? ctx->click_count + 1 : 1;
        ctx->last_click_ms = now;
        ctx->last_click_x  = mouse_x;
        ctx->last_click_y  = mouse_y;
    }

    /* Determine top-most hovered element from previous frame's solved box cache */
    int best_z = -999999;
    for (int i = 0; i < ctx->box_cache_count; i++) {
        const cs2d_ui_box_entry_t *b = &ctx->box_cache[i];
        if (b->id != 0 &&
            mouse_x >= b->rect.x && mouse_x < (b->rect.x + b->rect.w) &&
            mouse_y >= b->rect.y && mouse_y < (b->rect.y + b->rect.h)) {
            if (b->z_index >= best_z) {
                best_z      = b->z_index;
                ctx->hot_id = b->id;
            }
        }
    }

    if (ctx->mouse_pressed) {
        ctx->active_id = ctx->hot_id;
    }

    /* Create implicit root screen container (node 0) */
    cs2d_ui_element_decl_t root_decl;
    memset(&root_decl, 0, sizeof(root_decl));
    root_decl.layout.sizing.width  = CS2D_UI_FIXED((float)screen_w);
    root_decl.layout.sizing.height = CS2D_UI_FIXED((float)screen_h);
    root_decl.layout.dir           = CS2D_UI_TOP_TO_BOTTOM;
    cs2d_ui_open_element(ctx, CS2D_UI_ID("__Root"), &root_decl);
}

void cs2d_ui_open_element(cs2d_ui_context_t *ctx, cs2d_ui_id_t id, const cs2d_ui_element_decl_t *decl)
{
    if (!ctx || ctx->node_count >= CS2D_UI_MAX_NODES) return;

    int idx = ctx->node_count++;
    cs2d_ui_node_t *node = &ctx->nodes[idx];
    memset(node, 0, sizeof(*node));
    node->id = id;
    if (decl) {
        node->decl = *decl;
    }

    if (ctx->open_stack_depth > 0) {
        int p_idx = ctx->open_stack[ctx->open_stack_depth - 1];
        node->parent_idx  = p_idx;
        node->effective_z = node->decl.floating.enabled ? node->decl.floating.z_index : ctx->nodes[p_idx].effective_z;
        cs2d_ui_node_t *parent = &ctx->nodes[p_idx];
        if (parent->child_count < CS2D_UI_MAX_CHILDREN) {
            parent->children[parent->child_count++] = idx;
        }
    } else {
        node->parent_idx  = -1;
        node->effective_z = 0;
    }

    if (ctx->open_stack_depth < CS2D_UI_MAX_STACK) {
        ctx->open_stack[ctx->open_stack_depth++] = idx;
    }
}

void cs2d_ui_close_element(cs2d_ui_context_t *ctx)
{
    if (!ctx || ctx->open_stack_depth <= 0) return;
    ctx->open_stack_depth--;
}

void cs2d_ui_text(cs2d_ui_context_t *ctx, const char *text, cs2d_font_id_t font_id, cs2d_ui_color_t color)
{
    if (!ctx || !text || ctx->node_count >= CS2D_UI_MAX_NODES) return;

    int idx = ctx->node_count++;
    cs2d_ui_node_t *node = &ctx->nodes[idx];
    memset(node, 0, sizeof(*node));

    node->is_text    = true;
    node->font_id    = font_id;
    node->text_color = color;
    strncpy(node->text, text, sizeof(node->text) - 1);
    node->text[sizeof(node->text) - 1] = '\0';

    cs2d_render_text_measure(font_id, node->text, &node->text_w, &node->text_h);
    node->decl.layout.sizing.width  = CS2D_UI_FIXED(node->text_w);
    node->decl.layout.sizing.height = CS2D_UI_FIXED(node->text_h);

    if (ctx->open_stack_depth > 0) {
        int p_idx = ctx->open_stack[ctx->open_stack_depth - 1];
        node->parent_idx  = p_idx;
        node->effective_z = ctx->nodes[p_idx].effective_z;
        cs2d_ui_node_t *parent = &ctx->nodes[p_idx];
        if (parent->child_count < CS2D_UI_MAX_CHILDREN) {
            parent->children[parent->child_count++] = idx;
        }
    } else {
        node->parent_idx = -1;
    }
}

static void push_render_cmd(cs2d_ui_context_t *ctx, const cs2d_ui_render_cmd_t *cmd)
{
    if (!ctx || ctx->render_cmd_count >= CS2D_UI_MAX_RENDER_CMDS) return;
    ctx->render_cmds[ctx->render_cmd_count++] = *cmd;
}

void cs2d_ui_emit_line(cs2d_ui_context_t *ctx, float x1, float y1, float x2, float y2, cs2d_ui_color_t color)
{
    if (!ctx) return;
    cs2d_ui_render_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type    = CS2D_UI_CMD_LINE;
    cmd.rect.x  = x1;
    cmd.rect.y  = y1;
    cmd.rect.w  = x2;
    cmd.rect.h  = y2;
    cmd.color   = color;
    cmd.z_index = (ctx->open_stack_depth > 0) ? ctx->nodes[ctx->open_stack[ctx->open_stack_depth - 1]].effective_z : 0;
    push_render_cmd(ctx, &cmd);
}

static float clamp_axis(float val, const cs2d_ui_sizing_axis_t *ax)
{
    if (ax->min_size > 0.0f && val < ax->min_size) val = ax->min_size;
    if (ax->max_size > 0.0f && val > ax->max_size) val = ax->max_size;
    return val;
}

static void emit_node_commands_recursive(cs2d_ui_context_t *ctx, int idx)
{
    if (idx < 0 || idx >= ctx->node_count) return;
    const cs2d_ui_node_t *n = &ctx->nodes[idx];

    /* Record solved bounding box in cache for interactive hit-testing */
    if (n->id != 0 && ctx->box_cache_count < CS2D_UI_MAX_BOX_CACHE) {
        cs2d_ui_box_entry_t *b = &ctx->box_cache[ctx->box_cache_count++];
        b->id      = n->id;
        b->rect    = n->rect;
        b->z_index = n->effective_z;
    }

    if (n->is_text) {
        if (n->text[0] != '\0' && n->text_color.a > 0) {
            cs2d_ui_render_cmd_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.type    = CS2D_UI_CMD_TEXT;
            cmd.rect    = n->rect;
            cmd.color   = n->text_color;
            cmd.font_id = n->font_id;
            cmd.z_index = n->effective_z;
            strncpy(cmd.text, n->text, sizeof(cmd.text) - 1);
            push_render_cmd(ctx, &cmd);
        }
        return;
    }

    /* 1. Background Fill or Vertical Gradient */
    if (n->decl.bg_color.a > 0) {
        cs2d_ui_render_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        if (n->decl.bg_gradient_bottom.a > 0) {
            cmd.type   = CS2D_UI_CMD_GRADIENT_RECT;
            cmd.color  = n->decl.bg_color;
            cmd.color2 = n->decl.bg_gradient_bottom;
        } else {
            cmd.type  = CS2D_UI_CMD_RECT;
            cmd.color = n->decl.bg_color;
        }
        cmd.rect    = n->rect;
        cmd.z_index = n->effective_z;
        push_render_cmd(ctx, &cmd);
    }

    /* 1b. Text selection highlight (under the text) */
    if (n->decl.draw_selection && n->decl.selection_color.a > 0) {
        float x0 = n->rect.x + n->decl.selection_x0;
        float x1 = n->rect.x + n->decl.selection_x1;
        if (x0 < n->rect.x) x0 = n->rect.x;
        if (x1 > n->rect.x + n->rect.w) x1 = n->rect.x + n->rect.w;
        if (x1 > x0) {
            cs2d_ui_render_cmd_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.type    = CS2D_UI_CMD_RECT;
            cmd.rect.x  = x0;
            cmd.rect.y  = n->rect.y + 1.0f;
            cmd.rect.w  = x1 - x0;
            cmd.rect.h  = n->rect.h - 2.0f;
            cmd.color   = n->decl.selection_color;
            cmd.z_index = n->effective_z;
            push_render_cmd(ctx, &cmd);
        }
    }

    /* 2. Image / Texture Sprite */
    if (n->decl.image.tex && n->decl.image.tex->id != 0) {
        cs2d_ui_render_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type    = CS2D_UI_CMD_IMAGE;
        cmd.rect    = n->rect;
        cmd.tex     = n->decl.image.tex;
        cmd.color   = (n->decl.image.tint.a > 0) ? n->decl.image.tint : CS2D_UI_RGBA(255, 255, 255, 255);
        cmd.z_index = n->effective_z;
        push_render_cmd(ctx, &cmd);
    }

    /* 3. Scissor Clip Begin (for scrollable lists/log boxes) */
    if (n->decl.clip.enabled) {
        cs2d_ui_render_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type    = CS2D_UI_CMD_SCISSOR_START;
        cmd.rect    = n->rect;
        cmd.z_index = n->effective_z;
        push_render_cmd(ctx, &cmd);
    }

    /* 4. Children */
    for (int c = 0; c < n->child_count; c++) {
        emit_node_commands_recursive(ctx, n->children[c]);
    }

    /* 5. Scissor Clip End */
    if (n->decl.clip.enabled) {
        cs2d_ui_render_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type    = CS2D_UI_CMD_SCISSOR_END;
        cmd.z_index = n->effective_z;
        push_render_cmd(ctx, &cmd);
    }

    /* 5b. Cross ('X') mark for checkboxes & window close buttons */
    if (n->decl.draw_cross && n->decl.cross_color.a > 0) {
        float ins = (n->decl.cross_inset > 0.0f) ? n->decl.cross_inset : 2.0f;
        float x0  = n->rect.x + ins;
        float y0  = n->rect.y + ins;
        float x1  = n->rect.x + n->rect.w - ins - 1.0f;
        float y1  = n->rect.y + n->rect.h - ins - 1.0f;

        cs2d_ui_render_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type    = CS2D_UI_CMD_LINE;
        cmd.rect.x  = x0;
        cmd.rect.y  = y0;
        cmd.rect.w  = x1;
        cmd.rect.h  = y1;
        cmd.color   = n->decl.cross_color;
        cmd.z_index = n->effective_z;
        push_render_cmd(ctx, &cmd);

        cmd.rect.x  = x1;
        cmd.rect.y  = y0;
        cmd.rect.w  = x0;
        cmd.rect.h  = y1;
        push_render_cmd(ctx, &cmd);
    }

    /* 5c. Blinking text cursor bar for focused textboxes */
    if (n->decl.draw_cursor && n->decl.cursor_color.a > 0) {
        float cx = n->rect.x + n->decl.cursor_offset_x;
        cs2d_ui_render_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type    = CS2D_UI_CMD_RECT;
        cmd.rect.x  = cx;
        cmd.rect.y  = n->rect.y + 2.0f;
        cmd.rect.w  = 1.0f;
        cmd.rect.h  = n->rect.h - 4.0f;
        cmd.color   = n->decl.cursor_color;
        cmd.z_index = n->effective_z;
        push_render_cmd(ctx, &cmd);
    }

    /* 6. Border Outline (drawn after children so container border stays crisp on top) */
    if (n->decl.border.width > 0.0f && n->decl.border.color.a > 0) {
        cs2d_ui_render_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type         = CS2D_UI_CMD_BORDER;
        cmd.rect         = n->rect;
        cmd.color        = n->decl.border.color;
        cmd.border_width = n->decl.border.width;
        cmd.z_index      = n->effective_z;
        push_render_cmd(ctx, &cmd);
    }
}

void cs2d_ui_end_layout(cs2d_ui_context_t *ctx)
{
    if (!ctx || ctx->node_count <= 0) return;

    /* Close implicit root if still open */
    while (ctx->open_stack_depth > 0) {
        cs2d_ui_close_element(ctx);
    }

    /*
     * PASS 1: Bottom-Up Intrinsic Fit Sizing (Post-Order: node_count - 1 down to 0)
     */
    for (int i = ctx->node_count - 1; i >= 0; i--) {
        cs2d_ui_node_t *n = &ctx->nodes[i];
        if (n->is_text) {
            n->rect.w = n->text_w;
            n->rect.h = n->text_h;
            continue;
        }

        const cs2d_ui_layout_config_t *lay = &n->decl.layout;
        float content_w = 0.0f;
        float content_h = 0.0f;
        int   flow_children = 0;

        for (int c = 0; c < n->child_count; c++) {
            const cs2d_ui_node_t *ch = &ctx->nodes[n->children[c]];
            if (ch->decl.floating.enabled) continue;
            flow_children++;
            if (lay->dir == CS2D_UI_LEFT_TO_RIGHT) {
                content_w += ch->rect.w;
                if (ch->rect.h > content_h) content_h = ch->rect.h;
            } else {
                content_h += ch->rect.h;
                if (ch->rect.w > content_w) content_w = ch->rect.w;
            }
        }

        if (flow_children > 1) {
            float total_gap = lay->child_gap * (float)(flow_children - 1);
            if (lay->dir == CS2D_UI_LEFT_TO_RIGHT) content_w += total_gap;
            else                                   content_h += total_gap;
        }

        float pad_w = lay->padding.left + lay->padding.right;
        float pad_h = lay->padding.top  + lay->padding.bottom;

        if (lay->sizing.width.type == CS2D_UI_SIZING_TYPE_FIXED) {
            n->rect.w = lay->sizing.width.min_size;
        } else {
            n->rect.w = clamp_axis(content_w + pad_w, &lay->sizing.width);
        }

        if (lay->sizing.height.type == CS2D_UI_SIZING_TYPE_FIXED) {
            n->rect.h = lay->sizing.height.min_size;
        } else {
            n->rect.h = clamp_axis(content_h + pad_h, &lay->sizing.height);
        }
    }

    /*
     * PASS 2: Top-Down Grow & Percent Distribution (Pre-Order: 0 to node_count - 1)
     */
    for (int i = 0; i < ctx->node_count; i++) {
        cs2d_ui_node_t *n = &ctx->nodes[i];
        if (n->is_text || n->child_count == 0) continue;

        const cs2d_ui_layout_config_t *lay = &n->decl.layout;
        float inner_w = n->rect.w - (lay->padding.left + lay->padding.right);
        float inner_h = n->rect.h - (lay->padding.top  + lay->padding.bottom);
        if (inner_w < 0.0f) inner_w = 0.0f;
        if (inner_h < 0.0f) inner_h = 0.0f;

        /* First resolve PERCENT children and cross-axis GROW children */
        int   flow_count = 0;
        int   grow_main  = 0;
        float used_main  = 0.0f;

        for (int c = 0; c < n->child_count; c++) {
            cs2d_ui_node_t *ch = &ctx->nodes[n->children[c]];
            if (ch->decl.floating.enabled) continue;
            flow_count++;

            const cs2d_ui_sizing_t *csz = &ch->decl.layout.sizing;
            if (csz->width.type == CS2D_UI_SIZING_TYPE_PERCENT) {
                ch->rect.w = inner_w * csz->width.percent;
            }
            if (csz->height.type == CS2D_UI_SIZING_TYPE_PERCENT) {
                ch->rect.h = inner_h * csz->height.percent;
            }

            if (lay->dir == CS2D_UI_LEFT_TO_RIGHT) {
                /* Main axis = X, Cross axis = Y */
                if (csz->height.type == CS2D_UI_SIZING_TYPE_GROW) {
                    ch->rect.h = clamp_axis(inner_h, &csz->height);
                }
                if (csz->width.type == CS2D_UI_SIZING_TYPE_GROW) {
                    grow_main++;
                } else {
                    used_main += ch->rect.w;
                }
            } else {
                /* Main axis = Y, Cross axis = X */
                if (csz->width.type == CS2D_UI_SIZING_TYPE_GROW) {
                    ch->rect.w = clamp_axis(inner_w, &csz->width);
                }
                if (csz->height.type == CS2D_UI_SIZING_TYPE_GROW) {
                    grow_main++;
                } else {
                    used_main += ch->rect.h;
                }
            }
        }

        if (flow_count > 1) {
            used_main += lay->child_gap * (float)(flow_count - 1);
        }

        if (grow_main > 0) {
            float avail = (lay->dir == CS2D_UI_LEFT_TO_RIGHT) ? (inner_w - used_main) : (inner_h - used_main);
            if (avail < 0.0f) avail = 0.0f;
            float each = floorf(avail / (float)grow_main);

            for (int c = 0; c < n->child_count; c++) {
                cs2d_ui_node_t *ch = &ctx->nodes[n->children[c]];
                if (ch->decl.floating.enabled) continue;
                const cs2d_ui_sizing_t *csz = &ch->decl.layout.sizing;
                if (lay->dir == CS2D_UI_LEFT_TO_RIGHT && csz->width.type == CS2D_UI_SIZING_TYPE_GROW) {
                    ch->rect.w = clamp_axis(each, &csz->width);
                } else if (lay->dir == CS2D_UI_TOP_TO_BOTTOM && csz->height.type == CS2D_UI_SIZING_TYPE_GROW) {
                    ch->rect.h = clamp_axis(each, &csz->height);
                }
            }
        }
    }

    /*
     * PASS 3: Absolute Screen Positioning & Alignment (Pre-Order: 0 to node_count - 1)
     */
    ctx->nodes[0].rect.x = 0.0f;
    ctx->nodes[0].rect.y = 0.0f;

    for (int i = 0; i < ctx->node_count; i++) {
        cs2d_ui_node_t *n = &ctx->nodes[i];
        if (n->is_text || n->child_count == 0) continue;

        const cs2d_ui_layout_config_t *lay = &n->decl.layout;
        float inner_x = n->rect.x + lay->padding.left - n->decl.clip.scroll_x;
        float inner_y = n->rect.y + lay->padding.top  - n->decl.clip.scroll_y;
        float inner_w = n->rect.w - (lay->padding.left + lay->padding.right);
        float inner_h = n->rect.h - (lay->padding.top  + lay->padding.bottom);

        /* Calculate total main-axis extent of flow children for main-axis alignment */
        float total_main = 0.0f;
        int   flow_count = 0;
        for (int c = 0; c < n->child_count; c++) {
            const cs2d_ui_node_t *ch = &ctx->nodes[n->children[c]];
            if (ch->decl.floating.enabled) continue;
            flow_count++;
            total_main += (lay->dir == CS2D_UI_LEFT_TO_RIGHT) ? ch->rect.w : ch->rect.h;
        }
        if (flow_count > 1) {
            total_main += lay->child_gap * (float)(flow_count - 1);
        }

        float cur_main = 0.0f;
        if (lay->dir == CS2D_UI_LEFT_TO_RIGHT) {
            if (lay->align_x == CS2D_UI_ALIGN_X_CENTER)     cur_main = floorf((inner_w - total_main) * 0.5f);
            else if (lay->align_x == CS2D_UI_ALIGN_X_RIGHT) cur_main = inner_w - total_main;
        } else {
            if (lay->align_y == CS2D_UI_ALIGN_Y_CENTER)      cur_main = floorf((inner_h - total_main) * 0.5f);
            else if (lay->align_y == CS2D_UI_ALIGN_Y_BOTTOM) cur_main = inner_h - total_main;
        }

        for (int c = 0; c < n->child_count; c++) {
            cs2d_ui_node_t *ch = &ctx->nodes[n->children[c]];
            if (ch->decl.floating.enabled) {
                ch->rect.x = ch->decl.floating.x;
                ch->rect.y = ch->decl.floating.y;
                continue;
            }

            if (lay->dir == CS2D_UI_LEFT_TO_RIGHT) {
                ch->rect.x = roundf(inner_x + cur_main);
                if (lay->align_y == CS2D_UI_ALIGN_Y_CENTER) {
                    ch->rect.y = roundf(inner_y + (inner_h - ch->rect.h) * 0.5f);
                } else if (lay->align_y == CS2D_UI_ALIGN_Y_BOTTOM) {
                    ch->rect.y = roundf(inner_y + inner_h - ch->rect.h);
                } else {
                    ch->rect.y = roundf(inner_y);
                }
                cur_main += ch->rect.w + lay->child_gap;
            } else {
                ch->rect.y = roundf(inner_y + cur_main);
                if (lay->align_x == CS2D_UI_ALIGN_X_CENTER) {
                    ch->rect.x = roundf(inner_x + (inner_w - ch->rect.w) * 0.5f);
                } else if (lay->align_x == CS2D_UI_ALIGN_X_RIGHT) {
                    ch->rect.x = roundf(inner_x + inner_w - ch->rect.w);
                } else {
                    ch->rect.x = roundf(inner_x);
                }
                cur_main += ch->rect.h + lay->child_gap;
            }
        }
    }

    /*
     * PASS 4: Rebuild Box Cache & Emit Render Commands
     */
    ctx->box_cache_count = 0;
    emit_node_commands_recursive(ctx, 0);

    if (ctx->mouse_released) {
        ctx->active_id = 0;
    }

    /* Tab / Shift+Tab: move focus to the next / previous textbox in build order */
    if (ctx->focus_move != 0 && ctx->focus_order_count > 0) {
        int n = ctx->focus_order_count;
        for (int i = 0; i < n; i++) {
            if (ctx->focus_order[i] == ctx->focused_id) {
                ctx->focused_id          = ctx->focus_order[((i + ctx->focus_move) % n + n) % n];
                ctx->select_all_on_focus = true;
                break;
            }
        }
    }
    ctx->focus_move = 0;

    /* Clear single-frame keyboard input queues */
    cs2d_ui_clear_input(ctx);
}

cs2d_ui_rect_t cs2d_ui_get_box(const cs2d_ui_context_t *ctx, cs2d_ui_id_t id)
{
    cs2d_ui_rect_t empty = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (!ctx || id == 0) return empty;
    for (int i = 0; i < ctx->box_cache_count; i++) {
        if (ctx->box_cache[i].id == id) {
            return ctx->box_cache[i].rect;
        }
    }
    return empty;
}

bool cs2d_ui_hovered(const cs2d_ui_context_t *ctx, cs2d_ui_id_t id)
{
    if (!ctx || id == 0) return false;
    return (ctx->hot_id == id);
}

bool cs2d_ui_clicked(const cs2d_ui_context_t *ctx, cs2d_ui_id_t id)
{
    if (!ctx || id == 0) return false;
    return (ctx->mouse_released && ctx->hot_id == id && ctx->active_id == id);
}

static void draw_border_rect(const cs2d_ui_rect_t *r, float bw, cs2d_ui_color_t c)
{
    float fr = (float)c.r / 255.0f;
    float fg = (float)c.g / 255.0f;
    float fb = (float)c.b / 255.0f;
    float fa = (float)c.a / 255.0f;

    cs2d_render_rect(r->x,             r->y,             r->w, bw,   fr, fg, fb, fa, true);
    cs2d_render_rect(r->x,             r->y + r->h - bw, r->w, bw,   fr, fg, fb, fa, true);
    cs2d_render_rect(r->x,             r->y,             bw,   r->h, fr, fg, fb, fa, true);
    cs2d_render_rect(r->x + r->w - bw, r->y,             bw,   r->h, fr, fg, fb, fa, true);
}

void cs2d_ui_render(const cs2d_ui_context_t *ctx)
{
    if (!ctx) return;

    for (int i = 0; i < ctx->render_cmd_count; i++) {
        const cs2d_ui_render_cmd_t *cmd = &ctx->render_cmds[i];
        float r1 = (float)cmd->color.r  / 255.0f;
        float g1 = (float)cmd->color.g  / 255.0f;
        float b1 = (float)cmd->color.b  / 255.0f;
        float a1 = (float)cmd->color.a  / 255.0f;

        switch (cmd->type) {
            case CS2D_UI_CMD_RECT:
                cs2d_render_rect(cmd->rect.x, cmd->rect.y, cmd->rect.w, cmd->rect.h,
                                 r1, g1, b1, a1, true);
                break;

            case CS2D_UI_CMD_GRADIENT_RECT: {
                float r2 = (float)cmd->color2.r / 255.0f;
                float g2 = (float)cmd->color2.g / 255.0f;
                float b2 = (float)cmd->color2.b / 255.0f;
                float a2 = (float)cmd->color2.a / 255.0f;
                cs2d_render_gradient_rect(cmd->rect.x, cmd->rect.y, cmd->rect.w, cmd->rect.h,
                                          r1, g1, b1, a1, r2, g2, b2, a2);
                break;
            }

            case CS2D_UI_CMD_BORDER:
                draw_border_rect(&cmd->rect, cmd->border_width, cmd->color);
                break;

            case CS2D_UI_CMD_TEXT:
                cs2d_render_text_ex(cmd->font_id, cmd->rect.x, cmd->rect.y, cmd->text,
                                    r1, g1, b1, a1);
                break;

            case CS2D_UI_CMD_IMAGE:
                if (cmd->tex && cmd->tex->id != 0) {
                    cs2d_render_sprite(cmd->tex,
                                       cmd->rect.x + cmd->rect.w * 0.5f,
                                       cmd->rect.y + cmd->rect.h * 0.5f,
                                       cmd->rect.w, cmd->rect.h,
                                       0.0f, r1, g1, b1, a1);
                }
                break;

            case CS2D_UI_CMD_LINE:
                cs2d_render_line(cmd->rect.x, cmd->rect.y, cmd->rect.w, cmd->rect.h,
                                 r1, g1, b1, a1);
                break;

            case CS2D_UI_CMD_SCISSOR_START:
                cs2d_render_scissor_begin((int)cmd->rect.x, (int)cmd->rect.y,
                                          (int)cmd->rect.w, (int)cmd->rect.h);
                break;

            case CS2D_UI_CMD_SCISSOR_END:
                cs2d_render_scissor_end();
                break;

            default:
                break;
        }
    }
}

/* ============================================================================
 * Authentic CS2D Theme Widgets (Built on the Declarative Layout Engine)
 * ============================================================================ */

bool cs2d_ui_window_begin(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char *title,
    float x, float y, float w, float h,
    bool *is_open
)
{
    if (!ctx || (is_open && !*is_open)) return false;

    cs2d_ui_open_element(ctx, id, &(cs2d_ui_element_decl_t){
        .layout = {
            .sizing    = { CS2D_UI_FIXED(w), CS2D_UI_FIXED(h) },
            .padding   = CS2D_UI_PAD(6, 6, 3, 6),
            .child_gap = 4.0f,
            .dir       = CS2D_UI_TOP_TO_BOTTOM
        },
        .floating = { .enabled = true, .x = x, .y = y, .z_index = 10 },
        .bg_color = CS2D_UI_RGBA(58, 58, 58, 228),
        .border   = { .width = 1.0f, .color = CS2D_UI_RGBA(96, 96, 96, 245) }
    });

    /* Title Bar Row */
    cs2d_ui_id_t hdr_id   = id ^ 0x1001u;
    cs2d_ui_id_t close_id = id ^ 0x1002u;

    CS2D_UI_ELEMENT(ctx, hdr_id, {
        .layout = {
            .sizing  = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(16.0f) },
            .padding = CS2D_UI_PAD(5, 2, 0, 0),
            .dir     = CS2D_UI_LEFT_TO_RIGHT,
            .align_y = CS2D_UI_ALIGN_Y_CENTER
        }
    }) {
        cs2d_ui_text(ctx, title ? title : "Window", CS2D_FONT_REGULAR, CS2D_UI_RGB(230, 230, 230));

        /* Spacer */
        CS2D_UI_ELEMENT(ctx, 0, {
            .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(1.0f) } }
        }) {}

        /* Close 'X' button */
        bool close_hov = cs2d_ui_hovered(ctx, close_id);
        CS2D_UI_ELEMENT(ctx, close_id, {
            .layout = {
                .sizing  = { CS2D_UI_FIXED(14.0f), CS2D_UI_FIXED(14.0f) },
                .align_x = CS2D_UI_ALIGN_X_CENTER,
                .align_y = CS2D_UI_ALIGN_Y_CENTER
            },
            .bg_color    = close_hov ? CS2D_UI_RGBA(110, 45, 45, 220) : CS2D_UI_RGBA(0, 0, 0, 0),
            .draw_cross  = true,
            .cross_inset = 3.0f,
            .cross_color = close_hov ? CS2D_UI_RGB(255, 255, 255) : CS2D_UI_RGB(185, 185, 185)
        }) {}
    }

    if (is_open && cs2d_ui_clicked(ctx, close_id)) {
        *is_open = false;
    }

    return true;
}

void cs2d_ui_window_end(cs2d_ui_context_t *ctx)
{
    cs2d_ui_close_element(ctx);
}

int cs2d_ui_tab_bar(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char **tab_labels,
    int tab_count,
    int *active_tab
)
{
    if (!ctx || !tab_labels || tab_count <= 0) return 0;
    int cur = active_tab ? *active_tab : 0;

    CS2D_UI_ELEMENT(ctx, id, {
        .layout = {
            .sizing    = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(20.0f) },
            .child_gap = 0.0f,
            .dir       = CS2D_UI_LEFT_TO_RIGHT
        },
        .border = { .width = 1.0f, .color = CS2D_UI_RGBA(92, 92, 92, 240) }
    }) {
        for (int i = 0; i < tab_count; i++) {
            cs2d_ui_id_t tab_id = id ^ (uint32_t)(0x2000 + i);
            bool is_sel = (cur == i);
            bool is_hov = cs2d_ui_hovered(ctx, tab_id);

            cs2d_ui_color_t top_col = is_sel ? CS2D_UI_RGBA(98, 98, 98, 255)
                                             : (is_hov ? CS2D_UI_RGBA(68, 68, 68, 255) : CS2D_UI_RGBA(48, 48, 48, 255));
            cs2d_ui_color_t bot_col = is_sel ? CS2D_UI_RGBA(58, 58, 58, 255)
                                             : (is_hov ? CS2D_UI_RGBA(45, 45, 45, 255) : CS2D_UI_RGBA(34, 34, 34, 255));

            CS2D_UI_ELEMENT(ctx, tab_id, {
                .layout = {
                    .sizing  = { CS2D_UI_FIT(44.0f, 0.0f), CS2D_UI_FIXED(20.0f) },
                    .padding = CS2D_UI_PAD_XY(8.0f, 2.0f),
                    .align_x = CS2D_UI_ALIGN_X_CENTER,
                    .align_y = CS2D_UI_ALIGN_Y_CENTER
                },
                .bg_color           = top_col,
                .bg_gradient_bottom = bot_col,
                .border             = { .width = 1.0f, .color = CS2D_UI_RGBA(95, 95, 95, 240) }
            }) {
                cs2d_ui_text(ctx, tab_labels[i],
                             CS2D_FONT_REGULAR,
                             is_sel ? CS2D_UI_RGB(255, 255, 255) : CS2D_UI_RGB(175, 175, 175));
            }

            if (cs2d_ui_clicked(ctx, tab_id)) {
                cur = i;
                if (active_tab) *active_tab = i;
            }
        }
    }

    return cur;
}

bool cs2d_ui_button(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char *label,
    float width,
    float height
)
{
    if (!ctx) return false;
    bool hov  = cs2d_ui_hovered(ctx, id);
    bool down = (hov && ctx->mouse_down && ctx->active_id == id);

    cs2d_ui_color_t top_col = down ? CS2D_UI_RGBA(25, 25, 25, 250)
                                   : (hov ? CS2D_UI_RGBA(58, 58, 58, 250) : CS2D_UI_RGBA(38, 38, 38, 250));
    cs2d_ui_color_t bot_col = down ? CS2D_UI_RGBA(35, 35, 35, 250)
                                   : (hov ? CS2D_UI_RGBA(38, 38, 38, 250) : CS2D_UI_RGBA(22, 22, 22, 250));
    cs2d_ui_color_t brd_col = hov ? CS2D_UI_RGBA(185, 185, 185, 255) : CS2D_UI_RGBA(115, 115, 115, 245);

    CS2D_UI_ELEMENT(ctx, id, {
        .layout = {
            .sizing  = {
                (width > 0.0f)  ? CS2D_UI_FIXED(width)  : CS2D_UI_FIT(56.0f, 0.0f),
                (height > 0.0f) ? CS2D_UI_FIXED(height) : CS2D_UI_FIXED(19.0f)
            },
            .padding = CS2D_UI_PAD_XY(10.0f, 1.0f),
            .align_x = CS2D_UI_ALIGN_X_CENTER,
            .align_y = CS2D_UI_ALIGN_Y_CENTER
        },
        .bg_color           = top_col,
        .bg_gradient_bottom = bot_col,
        .border             = { .width = 1.0f, .color = brd_col }
    }) {
        cs2d_ui_text(ctx, label ? label : "",
                     CS2D_FONT_REGULAR,
                     hov ? CS2D_UI_RGB(255, 255, 255) : CS2D_UI_RGB(210, 210, 210));
    }

    return cs2d_ui_clicked(ctx, id);
}

bool cs2d_ui_checkbox(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char *label,
    bool *checked
)
{
    if (!ctx) return false;
    bool val = checked ? *checked : false;
    bool hov = cs2d_ui_hovered(ctx, id);
    cs2d_ui_id_t box_id = id ^ 0x3001u;

    CS2D_UI_ELEMENT(ctx, id, {
        .layout = {
            .sizing    = { CS2D_UI_FIT(0, 0), CS2D_UI_FIXED(16.0f) },
            .child_gap = 6.0f,
            .dir       = CS2D_UI_LEFT_TO_RIGHT,
            .align_y   = CS2D_UI_ALIGN_Y_CENTER
        }
    }) {
        CS2D_UI_ELEMENT(ctx, box_id, {
            .layout = {
                .sizing = { CS2D_UI_FIXED(12.0f), CS2D_UI_FIXED(12.0f) }
            },
            .bg_color    = CS2D_UI_RGBA(32, 32, 32, 250),
            .border      = { .width = 1.0f, .color = hov ? CS2D_UI_RGB(190, 190, 190) : CS2D_UI_RGB(125, 125, 125) },
            .draw_cross  = val,
            .cross_inset = 2.0f,
            .cross_color = CS2D_UI_RGB(215, 215, 215)
        }) {}

        if (label && label[0]) {
            cs2d_ui_text(ctx, label, CS2D_FONT_REGULAR,
                         hov ? CS2D_UI_RGB(235, 235, 235) : CS2D_UI_RGB(185, 185, 185));
        }
    }

    if (cs2d_ui_clicked(ctx, id) || cs2d_ui_clicked(ctx, box_id)) {
        val = !val;
        if (checked) *checked = val;
        return true;
    }
    return false;
}

bool cs2d_ui_radio(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char *label,
    int value,
    int *current_value
)
{
    if (!ctx) return false;
    bool sel = (current_value && *current_value == value);
    bool hov = cs2d_ui_hovered(ctx, id);
    cs2d_ui_id_t box_id = id ^ 0x4001u;

    CS2D_UI_ELEMENT(ctx, id, {
        .layout = {
            .sizing    = { CS2D_UI_FIT(0, 0), CS2D_UI_FIXED(16.0f) },
            .child_gap = 6.0f,
            .dir       = CS2D_UI_LEFT_TO_RIGHT,
            .align_y   = CS2D_UI_ALIGN_Y_CENTER
        }
    }) {
        CS2D_UI_ELEMENT(ctx, box_id, {
            .layout = {
                .sizing  = { CS2D_UI_FIXED(11.0f), CS2D_UI_FIXED(11.0f) },
                .align_x = CS2D_UI_ALIGN_X_CENTER,
                .align_y = CS2D_UI_ALIGN_Y_CENTER
            },
            .bg_color = CS2D_UI_RGBA(32, 32, 32, 250),
            .border   = { .width = 1.0f, .color = hov ? CS2D_UI_RGB(190, 190, 190) : CS2D_UI_RGB(125, 125, 125) }
        }) {
            if (sel) {
                CS2D_UI_ELEMENT(ctx, 0, {
                    .layout   = { .sizing = { CS2D_UI_FIXED(5.0f), CS2D_UI_FIXED(5.0f) } },
                    .bg_color = CS2D_UI_RGB(215, 215, 215)
                }) {}
            }
        }

        if (label && label[0]) {
            cs2d_ui_text(ctx, label, CS2D_FONT_REGULAR,
                         hov ? CS2D_UI_RGB(235, 235, 235) : CS2D_UI_RGB(185, 185, 185));
        }
    }

    if (cs2d_ui_clicked(ctx, id) || cs2d_ui_clicked(ctx, box_id)) {
        if (current_value) *current_value = value;
        return true;
    }
    return false;
}

bool cs2d_ui_select(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char **options,
    int option_count,
    int *selected_index,
    float width
)
{
    if (!ctx || !options || option_count <= 0) return false;
    int idx = selected_index ? *selected_index : 0;
    if (idx < 0 || idx >= option_count) idx = 0;

    bool hov = cs2d_ui_hovered(ctx, id);

    CS2D_UI_ELEMENT(ctx, id, {
        .layout = {
            .sizing  = { CS2D_UI_FIXED(width), CS2D_UI_FIXED(18.0f) },
            .padding = CS2D_UI_PAD(5, 0, 0, 0),
            .dir     = CS2D_UI_LEFT_TO_RIGHT,
            .align_y = CS2D_UI_ALIGN_Y_CENTER
        },
        .bg_color = CS2D_UI_RGBA(26, 26, 26, 248),
        .border   = { .width = 1.0f, .color = hov ? CS2D_UI_RGB(165, 165, 165) : CS2D_UI_RGB(108, 108, 108) }
    }) {
        cs2d_ui_text(ctx, options[idx], CS2D_FONT_REGULAR, CS2D_UI_RGB(205, 205, 205));

        CS2D_UI_ELEMENT(ctx, 0, {
            .layout = { .sizing = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(1.0f) } }
        }) {}

        /* Right-side [+] button matching CS2D selector widget */
        CS2D_UI_ELEMENT(ctx, 0, {
            .layout = {
                .sizing  = { CS2D_UI_FIXED(16.0f), CS2D_UI_FIXED(18.0f) },
                .align_x = CS2D_UI_ALIGN_X_CENTER,
                .align_y = CS2D_UI_ALIGN_Y_CENTER
            },
            .bg_color = hov ? CS2D_UI_RGBA(55, 55, 55, 255) : CS2D_UI_RGBA(36, 36, 36, 255),
            .border   = { .width = 1.0f, .color = CS2D_UI_RGB(108, 108, 108) }
        }) {
            cs2d_ui_text(ctx, "+", CS2D_FONT_SMALL, CS2D_UI_RGB(185, 185, 185));
        }
    }

    if (cs2d_ui_clicked(ctx, id)) {
        idx = (idx + 1) % option_count;
        if (selected_index) *selected_index = idx;
        return true;
    }
    return false;
}

/* ============================================================================
 * Textbox editing engine
 * ============================================================================ */

enum { EDIT_KIND_NONE = 0, EDIT_KIND_TYPE, EDIT_KIND_DELETE, EDIT_KIND_OTHER };
enum { CHAR_SPACE = 0, CHAR_WORD, CHAR_PUNCT };

/* Working copy of the buffer being edited; positions are byte offsets on UTF-8 boundaries */
typedef struct {
    char    *buf;
    size_t   size;
    int      len;
    int      cursor;
    int      anchor;
    uint32_t flags;
} textedit_t;

static bool utf8_is_cont(char c)
{
    return ((unsigned char)c & 0xC0u) == 0x80u;
}

static int utf8_prev(const char *s, int i)
{
    if (i <= 0) return 0;
    i--;
    while (i > 0 && utf8_is_cont(s[i])) i--;
    return i;
}

static int utf8_next(const char *s, int len, int i)
{
    if (i >= len) return len;
    i++;
    while (i < len && utf8_is_cont(s[i])) i++;
    return i;
}

static int utf8_snap(const char *s, int len, int i)
{
    if (i < 0) return 0;
    if (i > len) return len;
    while (i > 0 && i < len && utf8_is_cont(s[i])) i--;
    return i;
}

static int char_class(char c)
{
    unsigned char u = (unsigned char)c;
    if (u == ' ' || u == '\t') return CHAR_SPACE;
    if ((u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_' || u >= 0x80) {
        return CHAR_WORD;
    }
    return CHAR_PUNCT;
}

/* Ctrl+Left target: skip spaces, then the run of same-class characters before them */
static int word_left(const char *s, int i)
{
    while (i > 0 && char_class(s[utf8_prev(s, i)]) == CHAR_SPACE) i = utf8_prev(s, i);
    if (i > 0) {
        int cls = char_class(s[utf8_prev(s, i)]);
        while (i > 0 && char_class(s[utf8_prev(s, i)]) == cls) i = utf8_prev(s, i);
    }
    return i;
}

/* Ctrl+Right target: skip the current run, then the spaces after it (start of the next word) */
static int word_right(const char *s, int len, int i)
{
    if (i < len) {
        int cls = char_class(s[i]);
        if (cls != CHAR_SPACE) {
            while (i < len && char_class(s[i]) == cls) i = utf8_next(s, len, i);
        }
    }
    while (i < len && char_class(s[i]) == CHAR_SPACE) i = utf8_next(s, len, i);
    return i;
}

/* Double-click selection: the run of same-class characters around i */
static void word_bounds(const char *s, int len, int i, int *out_a, int *out_b)
{
    if (len == 0) {
        *out_a = *out_b = 0;
        return;
    }
    int probe = (i >= len) ? utf8_prev(s, len) : i;
    int cls   = char_class(s[probe]);
    int a = probe, b = utf8_next(s, len, probe);
    while (a > 0 && char_class(s[utf8_prev(s, a)]) == cls) a = utf8_prev(s, a);
    while (b < len && char_class(s[b]) == cls) b = utf8_next(s, len, b);
    *out_a = a;
    *out_b = b;
}

static bool te_has_selection(const textedit_t *t)
{
    return t->cursor != t->anchor;
}

static void te_selection(const textedit_t *t, int *a, int *b)
{
    *a = (t->cursor < t->anchor) ? t->cursor : t->anchor;
    *b = (t->cursor < t->anchor) ? t->anchor : t->cursor;
}

static void te_delete_range(textedit_t *t, int a, int b)
{
    if (b <= a) return;
    memmove(&t->buf[a], &t->buf[b], (size_t)(t->len - b + 1));
    t->len   -= (b - a);
    t->cursor = t->anchor = a;
}

static void te_delete_selection(textedit_t *t)
{
    int a, b;
    te_selection(t, &a, &b);
    te_delete_range(t, a, b);
}

/* Inserts text at the cursor, dropping control characters (and non-digits in NUMERIC mode)
 * and truncating on a codepoint boundary when the buffer is full */
static void te_insert(textedit_t *t, const char *text)
{
    char filtered[CS2D_UI_EDIT_MAX_TEXT];
    int  n = 0;
    for (const char *p = text; *p != '\0' && n < (int)sizeof(filtered) - 1; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\r') continue;
        if (c == '\n' || c == '\t') c = ' ';
        if (c < 32 || c == 127) continue;
        if ((t->flags & CS2D_UI_TEXTBOX_NUMERIC) && (c < '0' || c > '9')) continue;
        filtered[n++] = (char)c;
    }
    filtered[n] = '\0';

    int room = (int)t->size - 1 - t->len;
    if (n > room) n = utf8_snap(filtered, n, room);
    if (n <= 0) return;

    memmove(&t->buf[t->cursor + n], &t->buf[t->cursor], (size_t)(t->len - t->cursor + 1));
    memcpy(&t->buf[t->cursor], filtered, (size_t)n);
    t->len    += n;
    t->cursor += n;
    t->anchor  = t->cursor;
}

static void undo_store(cs2d_ui_undo_entry_t *u, const textedit_t *t)
{
    memcpy(u->text, t->buf, (size_t)t->len + 1);
    u->cursor = t->cursor;
    u->anchor = t->anchor;
}

static void undo_restore(const cs2d_ui_undo_entry_t *u, textedit_t *t)
{
    int n = (int)strlen(u->text);
    if (n > (int)t->size - 1) n = utf8_snap(u->text, n, (int)t->size - 1);
    memcpy(t->buf, u->text, (size_t)n);
    t->buf[n] = '\0';
    t->len    = n;
    t->cursor = (u->cursor <= n) ? u->cursor : n;
    t->anchor = (u->anchor <= n) ? u->anchor : n;
}

/* Snapshots the text before an edit. Consecutive typing / deleting within 1s merges into one step. */
static void undo_push(cs2d_ui_edit_state_t *e, const textedit_t *t, int kind)
{
    uint32_t now = (uint32_t)GetTickCount();
    bool merge = (kind != EDIT_KIND_OTHER && kind == e->last_edit_kind &&
                  now - e->last_edit_ms < 1000u && e->undo_pos > 0);
    e->last_edit_kind = kind;
    e->last_edit_ms   = now;
    if (merge || t->len >= CS2D_UI_EDIT_MAX_TEXT) return;

    if (e->undo_pos >= CS2D_UI_UNDO_DEPTH) {
        memmove(&e->undo[0], &e->undo[1], sizeof(e->undo[0]) * (CS2D_UI_UNDO_DEPTH - 1));
        e->undo_pos = CS2D_UI_UNDO_DEPTH - 1;
    }
    undo_store(&e->undo[e->undo_pos++], t);
    e->undo_count = e->undo_pos; /* A new edit drops the redo branch */
}

static void undo_step(cs2d_ui_edit_state_t *e, textedit_t *t, bool redo)
{
    if (redo) {
        if (e->undo_pos + 1 >= e->undo_count) return;
        undo_restore(&e->undo[++e->undo_pos], t);
    } else {
        if (e->undo_pos <= 0 || t->len >= CS2D_UI_EDIT_MAX_TEXT) return;
        /* Save the current text once so redo can come back to it */
        if (e->undo_pos == e->undo_count) {
            if (e->undo_count >= CS2D_UI_UNDO_DEPTH) {
                memmove(&e->undo[0], &e->undo[1], sizeof(e->undo[0]) * (CS2D_UI_UNDO_DEPTH - 1));
                e->undo_pos--;
                e->undo_count--;
            }
            undo_store(&e->undo[e->undo_count++], t);
        }
        undo_restore(&e->undo[--e->undo_pos], t);
    }
    e->last_edit_kind = EDIT_KIND_NONE;
}

static void te_copy(const textedit_t *t)
{
    if (!te_has_selection(t) || (t->flags & CS2D_UI_TEXTBOX_PASSWORD)) return;
    int a, b;
    te_selection(t, &a, &b);
    char tmp[CS2D_UI_EDIT_MAX_TEXT];
    int  n = b - a;
    if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    memcpy(tmp, &t->buf[a], (size_t)n);
    tmp[n] = '\0';
    SDL_SetClipboardText(tmp);
}

static void te_paste(cs2d_ui_edit_state_t *e, textedit_t *t)
{
    if ((t->flags & CS2D_UI_TEXTBOX_READONLY) || !SDL_HasClipboardText()) return;
    char *clip = SDL_GetClipboardText();
    if (!clip) return;
    if (clip[0] != '\0') {
        undo_push(e, t, EDIT_KIND_OTHER);
        te_delete_selection(t);
        te_insert(t, clip);
    }
    SDL_free(clip);
}

/* Builds the displayed text ('*' per codepoint in password mode) and the byte -> display offset map */
static int te_display(const char *buf, int len, bool password, char *disp, int disp_size, int *map)
{
    int j = 0;
    for (int i = 0; i < len && j < disp_size - 1;) {
        int next = utf8_next(buf, len, i);
        for (int k = i; k < next; k++) map[k] = j;
        if (password) {
            disp[j++] = '*';
        } else {
            for (int k = i; k < next && j < disp_size - 1; k++) disp[j++] = buf[k];
        }
        i = next;
    }
    map[len] = j;
    disp[j] = '\0';
    return j;
}

/* Recomputes caret x positions when the displayed text changed */
static void te_update_metrics(cs2d_ui_edit_state_t *e, const char *buf, int len, bool password, cs2d_font_id_t font)
{
    if (len >= CS2D_UI_EDIT_MAX_TEXT) len = utf8_snap(buf, len, CS2D_UI_EDIT_MAX_TEXT - 1);
    bool same = (e->measured_password == password && strncmp(e->measured, buf, (size_t)len) == 0 &&
                 e->measured[len] == '\0' && (len == 0 || e->caret_x[len] > 0.0f));
    if (same) return;

    char disp[CS2D_UI_EDIT_MAX_TEXT];
    int  map[CS2D_UI_EDIT_MAX_TEXT + 1];
    te_display(buf, len, password, disp, sizeof(disp), map);

    e->caret_x[0] = 0.0f;
    for (int i = 1; i <= len; i++) {
        e->caret_x[i] = (i == len || !utf8_is_cont(buf[i]))
                      ? cs2d_render_text_prefix_width(font, disp, map[i])
                      : e->caret_x[i - 1];
    }
    memcpy(e->measured, buf, (size_t)len);
    e->measured[len]     = '\0';
    e->measured_password = password;
}

static int te_index_at(const cs2d_ui_edit_state_t *e, const char *buf, int len, float x)
{
    if (len >= CS2D_UI_EDIT_MAX_TEXT) len = utf8_snap(buf, len, CS2D_UI_EDIT_MAX_TEXT - 1);
    int   best   = 0;
    float best_d = 1e9f;
    for (int i = 0; i <= len; i = (i < len) ? utf8_next(buf, len, i) : len + 1) {
        float d = fabsf(e->caret_x[i] - x);
        if (d < best_d) {
            best_d = d;
            best   = i;
        }
    }
    return best;
}

static float te_caret_x(const cs2d_ui_edit_state_t *e, int i)
{
    if (i >= CS2D_UI_EDIT_MAX_TEXT) i = CS2D_UI_EDIT_MAX_TEXT - 1;
    return e->caret_x[i];
}

/* Applies one queued key to the focused textbox; returns true on Enter */
static bool te_handle_key(cs2d_ui_context_t *ctx, textedit_t *t, const cs2d_ui_key_event_t *ev)
{
    cs2d_ui_edit_state_t *e = &ctx->edit;
    bool shift    = (ev->mods & CS2D_UI_MOD_SHIFT) != 0;
    bool ctrl     = (ev->mods & CS2D_UI_MOD_CTRL)  != 0;
    bool readonly = (t->flags & CS2D_UI_TEXTBOX_READONLY) != 0;
    int  a, b;

    /* Cursor movement ends the current undo group so the next edit starts a new step */
    if (ev->key == SDLK_LEFT || ev->key == SDLK_RIGHT || ev->key == SDLK_HOME || ev->key == SDLK_END ||
        (ctrl && ev->key == SDLK_A)) {
        e->last_edit_kind = EDIT_KIND_NONE;
    }

    switch (ev->key) {
        case SDLK_LEFT:
            if (te_has_selection(t) && !shift) {
                te_selection(t, &a, &b);
                t->cursor = t->anchor = a;
            } else {
                t->cursor = ctrl ? word_left(t->buf, t->cursor) : utf8_prev(t->buf, t->cursor);
                if (!shift) t->anchor = t->cursor;
            }
            break;
        case SDLK_RIGHT:
            if (te_has_selection(t) && !shift) {
                te_selection(t, &a, &b);
                t->cursor = t->anchor = b;
            } else {
                t->cursor = ctrl ? word_right(t->buf, t->len, t->cursor) : utf8_next(t->buf, t->len, t->cursor);
                if (!shift) t->anchor = t->cursor;
            }
            break;
        case SDLK_HOME:
            t->cursor = 0;
            if (!shift) t->anchor = t->cursor;
            break;
        case SDLK_END:
            t->cursor = t->len;
            if (!shift) t->anchor = t->cursor;
            break;
        case SDLK_BACKSPACE:
            if (readonly) break;
            if (te_has_selection(t)) {
                undo_push(e, t, EDIT_KIND_OTHER);
                te_delete_selection(t);
            } else if (t->cursor > 0) {
                undo_push(e, t, ctrl ? EDIT_KIND_OTHER : EDIT_KIND_DELETE); /* Word deletes are separate steps */
                te_delete_range(t, ctrl ? word_left(t->buf, t->cursor) : utf8_prev(t->buf, t->cursor), t->cursor);
            }
            break;
        case SDLK_DELETE:
            if (shift && !ctrl) { /* Shift+Delete: cut */
                if (readonly || !te_has_selection(t) || (t->flags & CS2D_UI_TEXTBOX_PASSWORD)) break;
                te_copy(t);
                undo_push(e, t, EDIT_KIND_OTHER);
                te_delete_selection(t);
                break;
            }
            if (readonly) break;
            if (te_has_selection(t)) {
                undo_push(e, t, EDIT_KIND_OTHER);
                te_delete_selection(t);
            } else if (t->cursor < t->len) {
                undo_push(e, t, ctrl ? EDIT_KIND_OTHER : EDIT_KIND_DELETE);
                te_delete_range(t, t->cursor, ctrl ? word_right(t->buf, t->len, t->cursor) : utf8_next(t->buf, t->len, t->cursor));
            }
            break;
        case SDLK_INSERT:
            if (ctrl) te_copy(t);
            else if (shift) te_paste(e, t);
            break;
        case SDLK_A:
            if (ctrl) {
                t->anchor = 0;
                t->cursor = t->len;
            }
            break;
        case SDLK_C:
            if (ctrl) te_copy(t);
            break;
        case SDLK_X:
            if (ctrl && !readonly && te_has_selection(t) && !(t->flags & CS2D_UI_TEXTBOX_PASSWORD)) {
                te_copy(t);
                undo_push(e, t, EDIT_KIND_OTHER);
                te_delete_selection(t);
            }
            break;
        case SDLK_V:
            if (ctrl) te_paste(e, t);
            break;
        case SDLK_Z:
            if (ctrl && !readonly) undo_step(e, t, shift);
            break;
        case SDLK_Y:
            if (ctrl && !readonly) undo_step(e, t, true);
            break;
        case SDLK_TAB:
            ctx->focus_move = shift ? -1 : 1;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return true;
        default:
            return false;
    }
    e->activity_ms = (uint32_t)GetTickCount();
    return false;
}

bool cs2d_ui_textbox_ex(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    char *buffer,
    size_t buffer_size,
    int *cursor_pos,
    float width,
    uint32_t flags
)
{
    if (!ctx || !buffer || buffer_size == 0) return false;

    const cs2d_font_id_t font = CS2D_FONT_REGULAR;
    cs2d_ui_edit_state_t *e   = &ctx->edit;
    cs2d_ui_id_t inner_id     = id ^ 0x5001u;
    bool password             = (flags & CS2D_UI_TEXTBOX_PASSWORD) != 0;
    bool submitted            = false;

    if (ctx->focus_order_count < CS2D_UI_MAX_FOCUSABLE) {
        ctx->focus_order[ctx->focus_order_count++] = id;
    }

    bool hov          = (ctx->hot_id == id || ctx->hot_id == inner_id);
    bool clicked_here = (ctx->mouse_pressed && hov);
    if (clicked_here) ctx->focused_id = id;
    bool focused = (ctx->focused_id == id);

    buffer[buffer_size - 1] = '\0';
    int len = (int)strlen(buffer);
    int cur = utf8_snap(buffer, len, cursor_pos ? *cursor_pos : len);

    if (focused) {
        /* (Re)initialize the edit state when focus arrives at this textbox */
        if (e->id != id) {
            e->id             = id;
            e->anchor         = cur;
            e->scroll_x       = 0.0f;
            e->dragging       = false;
            e->undo_count     = 0;
            e->undo_pos       = 0;
            e->last_edit_kind = EDIT_KIND_NONE;
            e->activity_ms    = (uint32_t)GetTickCount();
            if (ctx->select_all_on_focus) {
                e->anchor = 0;
                cur       = len;
            }
        }
        ctx->select_all_on_focus = false;

        textedit_t t = { buffer, buffer_size, len, cur, utf8_snap(buffer, len, e->anchor), flags };
        te_update_metrics(e, buffer, len, password, font);

        /* Mouse: place caret, drag-select, Shift+click extend, double-click word, triple-click all */
        cs2d_ui_rect_t inner = cs2d_ui_get_box(ctx, inner_id);
        float local_x = ctx->mouse_x - inner.x + e->scroll_x;
        if (clicked_here) {
            int idx = te_index_at(e, buffer, len, local_x);
            if (ctx->click_count >= 3 || (ctx->click_count == 2 && password)) {
                t.anchor = 0;
                t.cursor = len;
            } else if (ctx->click_count == 2) {
                word_bounds(buffer, len, idx, &t.anchor, &t.cursor);
            } else {
                t.cursor = idx;
                if (!(current_mods() & CS2D_UI_MOD_SHIFT)) t.anchor = idx;
                e->dragging = true;
            }
            e->activity_ms    = (uint32_t)GetTickCount();
            e->last_edit_kind = EDIT_KIND_NONE;
        } else if (e->dragging && ctx->mouse_down) {
            t.cursor = te_index_at(e, buffer, len, local_x);
        }
        if (!ctx->mouse_down) e->dragging = false;

        /* Typed text replaces the selection */
        if (ctx->text_input[0] != '\0' && !(flags & CS2D_UI_TEXTBOX_READONLY)) {
            if (te_has_selection(&t)) e->last_edit_kind = EDIT_KIND_NONE; /* Replacing a selection is its own step */
            undo_push(e, &t, EDIT_KIND_TYPE);
            te_delete_selection(&t);
            te_insert(&t, ctx->text_input);
            e->activity_ms = (uint32_t)GetTickCount();
        }

        for (int k = 0; k < ctx->key_event_count; k++) {
            if (te_handle_key(ctx, &t, &ctx->key_events[k])) submitted = true;
        }

        len       = t.len;
        cur       = t.cursor;
        e->anchor = t.anchor;
        te_update_metrics(e, buffer, len, password, font);
        if (cursor_pos) *cursor_pos = cur;

        /* Keep the caret inside the visible part of the box */
        float view_w = (inner.w > 0.0f) ? inner.w : ((width > 10.0f) ? width - 10.0f : 100.0f);
        float caret  = te_caret_x(e, cur);
        float text_w = te_caret_x(e, len);
        if (caret - e->scroll_x > view_w - 2.0f) e->scroll_x = caret - view_w + 2.0f;
        if (caret - e->scroll_x < 0.0f)          e->scroll_x = caret;
        if (text_w - e->scroll_x < view_w - 2.0f) {
            e->scroll_x = text_w - view_w + 2.0f;
            if (e->scroll_x < 0.0f) e->scroll_x = 0.0f;
        }
    }

    char disp[192];
    int  map[CS2D_UI_EDIT_MAX_TEXT + 1];
    int  disp_len = (len < CS2D_UI_EDIT_MAX_TEXT) ? len : utf8_snap(buffer, len, CS2D_UI_EDIT_MAX_TEXT - 1);
    te_display(buffer, disp_len, password, disp, sizeof(disp), map);

    /* Caret blinks with a 1s period, held solid for 500ms after any activity */
    uint32_t since    = (uint32_t)GetTickCount() - e->activity_ms;
    bool show_cursor  = focused && ((since / 500u) & 1u) == 0;
    float scroll      = focused ? e->scroll_x : 0.0f;
    float cursor_off  = focused ? te_caret_x(e, cur) - scroll : 0.0f;
    bool has_sel      = focused && e->anchor != cur;
    int sel_a = has_sel ? ((e->anchor < cur) ? e->anchor : cur) : 0;
    int sel_b = has_sel ? ((e->anchor < cur) ? cur : e->anchor) : 0;

    CS2D_UI_ELEMENT(ctx, id, {
        .layout = {
            .sizing  = { (width > 0.0f) ? CS2D_UI_FIXED(width) : CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(18.0f) },
            .padding = CS2D_UI_PAD(5, 5, 1, 1),
            .dir     = CS2D_UI_LEFT_TO_RIGHT,
            .align_y = CS2D_UI_ALIGN_Y_CENTER
        },
        .bg_color = CS2D_UI_RGBA(22, 22, 22, 248),
        .border   = {
            .width = 1.0f,
            .color = focused ? CS2D_UI_RGB(175, 175, 175)
                             : (hov ? CS2D_UI_RGB(140, 140, 140) : CS2D_UI_RGB(108, 108, 108))
        }
    }) {
        CS2D_UI_ELEMENT(ctx, inner_id, {
            .layout = {
                .sizing  = { CS2D_UI_GROW(0, 0), CS2D_UI_GROW(0, 0) },
                .dir     = CS2D_UI_LEFT_TO_RIGHT,
                .align_y = CS2D_UI_ALIGN_Y_CENTER
            },
            .clip            = { .enabled = true, .scroll_x = scroll },
            .draw_cursor     = show_cursor,
            .cursor_offset_x = cursor_off,
            .cursor_color    = CS2D_UI_RGB(235, 235, 235),
            .draw_selection  = has_sel,
            .selection_x0    = has_sel ? te_caret_x(e, sel_a) - scroll : 0.0f,
            .selection_x1    = has_sel ? te_caret_x(e, sel_b) - scroll : 0.0f,
            .selection_color = CS2D_UI_RGBA(52, 92, 150, 235)
        }) {
            cs2d_ui_text(ctx, disp, font,
                         (flags & CS2D_UI_TEXTBOX_READONLY) ? CS2D_UI_RGB(185, 185, 185) : CS2D_UI_RGB(235, 235, 235));
        }
    }

    return submitted;
}

bool cs2d_ui_textbox(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    char *buffer,
    size_t buffer_size,
    int *cursor_pos,
    float width,
    bool password_mode
)
{
    return cs2d_ui_textbox_ex(ctx, id, buffer, buffer_size, cursor_pos, width,
                              password_mode ? CS2D_UI_TEXTBOX_PASSWORD : 0u);
}

void cs2d_ui_section_header(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char *title
)
{
    if (!ctx) return;
    CS2D_UI_ELEMENT(ctx, id, {
        .layout = {
            .sizing  = { CS2D_UI_GROW(0, 0), CS2D_UI_FIXED(18.0f) },
            .padding = CS2D_UI_PAD(5, 5, 1, 1),
            .dir     = CS2D_UI_LEFT_TO_RIGHT,
            .align_y = CS2D_UI_ALIGN_Y_CENTER
        },
        .bg_color           = CS2D_UI_RGBA(92, 92, 92, 245),
        .bg_gradient_bottom = CS2D_UI_RGBA(44, 44, 44, 245)
    }) {
        cs2d_ui_text(ctx, title ? title : "", CS2D_FONT_REGULAR, CS2D_UI_RGB(235, 235, 235));
    }
}
