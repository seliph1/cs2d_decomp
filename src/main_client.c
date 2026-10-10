#include <winsock2.h>
#include <windows.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "cs2d_client.h"
#include "cs2d_render.h"

static cs2d_client_t g_client;
static bool          g_running = true;

static int sdl_key_to_cs2d_key(SDL_Keycode key)
{
    switch (key) {
        case SDLK_W:      return 'W';
        case SDLK_S:      return 'S';
        case SDLK_A:      return 'A';
        case SDLK_D:      return 'D';
        case SDLK_R:      return 'R';
        case SDLK_B:      return 'B';
        case SDLK_G:      return 'G';
        case SDLK_1:      return '1';
        case SDLK_2:      return '2';
        case SDLK_3:      return '3';
        case SDLK_4:      return '4';
        case SDLK_5:      return '5';
        case SDLK_UP:     return VK_UP;
        case SDLK_DOWN:   return VK_DOWN;
        case SDLK_LEFT:   return VK_LEFT;
        case SDLK_RIGHT:  return VK_RIGHT;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: return VK_SHIFT;
        default:          return 0;
    }
}

static void save_gl_screenshot_bmp(const char *filepath, int w, int h)
{
    int row_stride = (w * 3 + 3) & ~3;
    int data_size  = row_stride * h;
    uint8_t *pixels = (uint8_t *)calloc((size_t)data_size, 1);
    if (!pixels) return;

    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    /* 0x80E0 = GL_BGR_EXT */
    glReadPixels(0, 0, w, h, 0x80E0, GL_UNSIGNED_BYTE, pixels);

    FILE *f = fopen(filepath, "wb");
    if (f) {
        uint8_t file_hdr[14] = {
            'B', 'M',
            0, 0, 0, 0,
            0, 0, 0, 0,
            54, 0, 0, 0
        };
        uint32_t file_size = 54u + (uint32_t)data_size;
        memcpy(&file_hdr[2], &file_size, 4);

        uint8_t info_hdr[40] = {0};
        uint32_t hdr_size = 40;
        int32_t  bw = w, bh = h;
        uint16_t planes = 1, bpp = 24;
        uint32_t img_size = (uint32_t)data_size;
        memcpy(&info_hdr[0],  &hdr_size, 4);
        memcpy(&info_hdr[4],  &bw,       4);
        memcpy(&info_hdr[8],  &bh,       4);
        memcpy(&info_hdr[12], &planes,   2);
        memcpy(&info_hdr[14], &bpp,      2);
        memcpy(&info_hdr[20], &img_size, 4);

        fwrite(file_hdr, 1, 14, f);
        fwrite(info_hdr, 1, 40, f);
        fwrite(pixels,   1, (size_t)data_size, f);
        fclose(f);
    }
    free(pixels);
}

static int run_client_app(const char *cmdline)
{
    const char *map_name = "de_dust2";
    bool auto_screenshot = false;
    if (cmdline && strlen(cmdline) > 0) {
        if (strstr(cmdline, "--screenshot") != NULL) {
            auto_screenshot = true;
        } else {
            map_name = cmdline;
        }
    }

    /* Pre-load configuration to obtain window resolution before creating SDL3 GL window */
    cs2d_config_t cfg;
    cs2d_config_load(&cfg, "sys/config.cfg");
    int win_w = (cfg.window_width > 0)  ? cfg.window_width  : 850;
    int win_h = (cfg.window_height > 0) ? cfg.window_height : 480;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "[CS2D SDL3] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    SDL_Window *window = SDL_CreateWindow(
        "CS2D - Reconstructed Client (SDL3)",
        win_w, win_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        /* Fallback without MSAA if driver does not support 4x multisample buffer */
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow(
            "CS2D - Reconstructed Client (SDL3)",
            win_w, win_h,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
        );
    }

    if (!window) {
        fprintf(stderr, "[CS2D SDL3] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_HideCursor();
    SDL_StartTextInput(window);

    if (!cs2d_render_init(window, win_w, win_h)) {
        fprintf(stderr, "[CS2D SDL3] Failed to initialize OpenGL renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Initialize client AFTER OpenGL context is active so all textures upload to GPU */
    if (!cs2d_client_init(&g_client, map_name)) {
        fprintf(stderr, "[CS2D SDL3] Failed to initialize CS2D Client state.\n");
        cs2d_render_shutdown(window);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Center initial camera on local player */
    const cs2d_player_t *init_pl = &g_client.game.players[g_client.local_player_id];
    g_client.cam_x = init_pl->pos.x - (float)win_w * 0.5f;
    g_client.cam_y = init_pl->pos.y - (float)win_h * 0.5f;
    g_client.mouse_screen_x = (float)win_w * 0.5f + 110.0f;
    g_client.mouse_screen_y = (float)win_h * 0.5f - 10.0f;

    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t last_time = SDL_GetPerformanceCounter();

    int frame_count = 0;
    SDL_Event event;
    while (g_running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    g_running = false;
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    if (event.window.data1 > 0 && event.window.data2 > 0) {
                        g_client.config.window_width  = event.window.data1;
                        g_client.config.window_height = event.window.data2;
                        cs2d_render_resize(event.window.data1, event.window.data2);
                    }
                    break;

                case SDL_EVENT_TEXT_INPUT:
                    if (g_client.console_open || g_client.create_server_open || g_client.options_open) {
                        cs2d_client_handle_text_input(&g_client, event.text.text);
                    }
                    break;

                case SDL_EVENT_MOUSE_WHEEL:
                    if (g_client.console_open || g_client.create_server_open || g_client.options_open) {
                        cs2d_client_handle_mouse_wheel(&g_client, (int)event.wheel.y);
                    }
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if ((event.key.key == SDLK_GRAVE || event.key.key == SDLK_F1 ||
                         event.key.key == SDLK_F2    || event.key.key == SDLK_F3) && !event.key.repeat) {
                        cs2d_client_handle_console_key(&g_client, (int)event.key.key);
                        break;
                    }
                    if (g_client.console_open || g_client.create_server_open || g_client.options_open) {
                        cs2d_client_handle_console_key(&g_client, (int)event.key.key);
                        break;
                    }
                    if (event.key.key == SDLK_ESCAPE && !event.key.repeat) {
                        g_client.console_open = true;
                        break;
                    }
                    if (!event.key.repeat) {
                        int k = sdl_key_to_cs2d_key(event.key.key);
                        if (k != 0) cs2d_client_handle_key(&g_client, k, true);
                    }
                    break;

                case SDL_EVENT_KEY_UP: {
                    int k = sdl_key_to_cs2d_key(event.key.key);
                    if (k != 0) cs2d_client_handle_key(&g_client, k, false);
                    break;
                }

                case SDL_EVENT_MOUSE_MOTION:
                    cs2d_client_handle_mouse(&g_client, event.motion.x, event.motion.y, 0, false);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    bool pressed = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                    int btn = 0;
                    if (event.button.button == SDL_BUTTON_LEFT)       btn = 1;
                    else if (event.button.button == SDL_BUTTON_RIGHT) btn = 2;
                    if (btn != 0) {
                        cs2d_client_handle_mouse(&g_client, event.button.x, event.button.y, btn, pressed);
                    }
                    break;
                }

                default:
                    break;
            }
        }

        if (g_client.quit_requested) {
            g_running = false;
        }
        if (!g_running) break;

        uint64_t current_time = SDL_GetPerformanceCounter();
        float delta_time = (float)(current_time - last_time) / (float)freq;
        last_time = current_time;

        if (delta_time > 0.05f) delta_time = 0.05f;

        /* Update */
        cs2d_client_update(&g_client, delta_time);

        /* Render */
        cs2d_render_begin_frame();
        cs2d_client_render(&g_client, g_client.config.window_width, g_client.config.window_height);
        if (auto_screenshot && frame_count == 45) {
            save_gl_screenshot_bmp("C:/Users/Gabriel/.gemini/antigravity/brain/b1e9fe06-c5b4-4a13-a42a-859765d696e1/scratch/client_screenshot.bmp",
                                   g_client.config.window_width, g_client.config.window_height);
            g_client.console_open = true;
            g_client.mouse_screen_x = 180.0f;
            g_client.mouse_screen_y = 290.0f;
        } else if (auto_screenshot && frame_count == 50) {
            save_gl_screenshot_bmp("C:/Users/Gabriel/.gemini/antigravity/brain/b1e9fe06-c5b4-4a13-a42a-859765d696e1/scratch/client_screenshot_console.bmp",
                                   g_client.config.window_width, g_client.config.window_height);
            cs2d_client_console_exec(&g_client, "help");
            cs2d_client_console_exec(&g_client, "status");
            strcpy(g_client.console_input, "sun 135");
            g_client.console_cursor = 7;
        } else if (auto_screenshot && frame_count == 55) {
            save_gl_screenshot_bmp("C:/Users/Gabriel/.gemini/antigravity/brain/b1e9fe06-c5b4-4a13-a42a-859765d696e1/scratch/client_screenshot_console_cmd.bmp",
                                   g_client.config.window_width, g_client.config.window_height);
            g_client.console_open       = false;
            g_client.create_server_open = true;
            g_client.create_server_tab  = 0;
        } else if (auto_screenshot && frame_count == 60) {
            save_gl_screenshot_bmp("C:/Users/Gabriel/.gemini/antigravity/brain/b1e9fe06-c5b4-4a13-a42a-859765d696e1/scratch/client_screenshot_createserver.bmp",
                                   g_client.config.window_width, g_client.config.window_height);
            g_client.create_server_tab  = 1;
        } else if (auto_screenshot && frame_count == 65) {
            save_gl_screenshot_bmp("C:/Users/Gabriel/.gemini/antigravity/brain/b1e9fe06-c5b4-4a13-a42a-859765d696e1/scratch/client_screenshot_map.bmp",
                                   g_client.config.window_width, g_client.config.window_height);
            g_client.create_server_open = false;
            g_client.options_open       = true;
            g_client.options_tab        = 1;
        } else if (auto_screenshot && frame_count == 70) {
            save_gl_screenshot_bmp("C:/Users/Gabriel/.gemini/antigravity/brain/b1e9fe06-c5b4-4a13-a42a-859765d696e1/scratch/client_screenshot_options.bmp",
                                   g_client.config.window_width, g_client.config.window_height);
            cs2d_render_end_frame(window);
            break;
        }
        cs2d_render_end_frame(window);
        frame_count++;

        SDL_Delay(auto_screenshot ? 16 : 1);
    }

    SDL_StopTextInput(window);
    cs2d_render_shutdown(window);
    cs2d_client_shutdown(&g_client);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;
    return run_client_app(lpCmdLine);
}

int main(int argc, char **argv)
{
    return run_client_app((argc > 1) ? argv[1] : NULL);
}
