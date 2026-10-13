#include <winsock2.h>
#include <windows.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "cs2d_client.h"
#include "cs2d_render.h"

static cs2d_client_t g_client;
static bool          g_running = true;

static int sdl_key_to_cs2d_key(SDL_Keycode key)
{
    /* Letters: SDL3 defines SDLK_A..SDLK_Z as 'a'..'z' (0x61..0x7a) */
    if (key >= SDLK_A && key <= SDLK_Z) {
        return 'A' + (int)(key - SDLK_A);
    }
    if (key >= 'a' && key <= 'z') {
        return 'A' + (int)(key - 'a');
    }
    if (key >= 'A' && key <= 'Z') {
        return (int)key;
    }
    /* Number row */
    if (key >= SDLK_0 && key <= SDLK_9) {
        return '0' + (int)(key - SDLK_0);
    }
    /* Keypad digits */
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
        return '1' + (int)(key - SDLK_KP_1);
    }
    if (key == SDLK_KP_0) return '0';

    switch (key) {
        case SDLK_COMMA:     return ',';
        case SDLK_PERIOD:    return '.';
        case SDLK_TAB:       return VK_TAB;
        case SDLK_BACKSPACE: return VK_BACK;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  return VK_RETURN;
        case SDLK_ESCAPE:    return VK_ESCAPE;
        case SDLK_UP:        return VK_UP;
        case SDLK_DOWN:      return VK_DOWN;
        case SDLK_LEFT:      return VK_LEFT;
        case SDLK_RIGHT:     return VK_RIGHT;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:    return VK_SHIFT;
        case SDLK_SPACE:     return VK_SPACE;
        case SDLK_LCTRL:
        case SDLK_RCTRL:     return VK_LCONTROL;
        case SDLK_LALT:
        case SDLK_RALT:      return VK_MENU;
        default:             return 0;
    }
}

static void save_gl_screenshot_bmp(const char *filepath, int w, int h)
{
    int row_stride = (w * 3 + 3) & ~3;
    int data_size  = row_stride * h;
    uint8_t *pixels = (uint8_t *)calloc((size_t)data_size, 1);
    if (!pixels) return;

    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    /* GL_BGR_EXT is gone in a core profile: read RGB and swap to the BGR byte order a BMP wants */
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    for (int y = 0; y < h; y++) {
        uint8_t *row = pixels + (size_t)y * (size_t)row_stride;
        for (int x = 0; x < w; x++) {
            uint8_t tmp = row[x * 3 + 0];
            row[x * 3 + 0] = row[x * 3 + 2];
            row[x * 3 + 2] = tmp;
        }
    }

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

/*
 * One --capture step: a console command, or a UI test action
 *   !console                      open the console (stays open for the capture)
 *   !text <text>                  type text into the focused textbox
 *   !key <SDL key name> [shift] [ctrl]   press a key with modifiers (e.g. "!key Left shift ctrl")
 *   !wheel <n>                    mouse wheel (positive = up)
 *   !mouse <x> <y> [down|up|rdown|rup]  move the pointer, optionally pressing / releasing the left (or right) button
 *   !hold <key> down|up           press or release a game key and keep it ("tab" for the scoreboard)
 *   !wait <frames>                wait before the next step
 *   !shot <file.bmp>              save the current frame (steps run every 5 frames)
 */
static void run_capture_step(const char *cmd)
{
    if (strcmp(cmd, "!console") == 0) {
        cs2d_client_handle_console_key(&g_client, SDLK_GRAVE);
    } else if (strncmp(cmd, "!text ", 6) == 0) {
        cs2d_client_handle_text_input(&g_client, cmd + 6);
    } else if (strncmp(cmd, "!mouse ", 7) == 0) {
        float mx = 0.0f, my = 0.0f;
        char state[8] = {0};
        if (sscanf(cmd + 7, "%f %f %7s", &mx, &my, state) >= 2) {
            int  button  = state[0] == 'r' ? 2 : (state[0] ? 1 : 0);
            bool pressed = (strcmp(state, "down") == 0 || strcmp(state, "rdown") == 0);
            cs2d_client_handle_mouse(&g_client, mx, my, button, pressed);
        }
    } else if (strncmp(cmd, "!press ", 7) == 0) {
        /* Game key press + release (e.g. "!press E" = use) */
        int k = toupper((unsigned char)cmd[7]);
        cs2d_client_handle_key(&g_client, k, true);
        cs2d_client_handle_key(&g_client, k, false);
    } else if (strncmp(cmd, "!hold ", 6) == 0) {
        char name[16] = {0}, state[8] = {0};
        if (sscanf(cmd + 6, "%15s %7s", name, state) == 2) {
            int k = (_stricmp(name, "tab") == 0) ? VK_TAB : toupper((unsigned char)name[0]);
            cs2d_client_handle_key(&g_client, k, strcmp(state, "down") == 0);
        }
    } else if (strncmp(cmd, "!wait ", 6) == 0) {
        /* Handled by the capture scheduler */
    } else if (strncmp(cmd, "!wheel ", 7) == 0) {
        cs2d_client_handle_mouse_wheel(&g_client, atoi(cmd + 7));
    } else if (strncmp(cmd, "!key ", 5) == 0) {
        char name[32] = {0};
        sscanf(cmd + 5, "%31s", name);
        SDL_Keycode key = SDL_GetKeyFromName(name);
        if (key == SDLK_UNKNOWN || g_client.ui.key_event_count >= CS2D_UI_MAX_KEY_EVENTS) return;
        uint32_t mods = 0;
        if (strstr(cmd + 5, " shift")) mods |= CS2D_UI_MOD_SHIFT;
        if (strstr(cmd + 5, " ctrl"))  mods |= CS2D_UI_MOD_CTRL;
        g_client.ui.key_events[g_client.ui.key_event_count].key  = (int)key;
        g_client.ui.key_events[g_client.ui.key_event_count].mods = mods;
        g_client.ui.key_event_count++;
    } else {
        /* Commands don't change whether the console is shown (only "!console" opens it) */
        bool was_open = g_client.console_open;
        cs2d_client_console_exec(&g_client, cmd);
        g_client.console_open = was_open;
    }
}

static int run_client_app(const char *cmdline)
{
    /* No argument opens the main menu; a map name starts a game on that map */
    const char *map_name = NULL;
    bool auto_screenshot = false;
    const char *capture_cmd = NULL; /* --capture[:<cmd>[;<cmd>...]] -> screens/capture.bmp */
    int capture_frame = 30;
    int capture_resume = 5;   /* Next frame to run a capture step (default 5 frames apart, or !wait <frames>) */
    double frame_ms_sum = 0.0, frame_ms_max = 0.0;
    int    frame_ms_count = 0;
    if (cmdline && strlen(cmdline) > 0) {
        if (strstr(cmdline, "--screenshot") != NULL) {
            auto_screenshot = true;
            map_name = "de_dust2";
        } else if (strncmp(cmdline, "--capture", 9) == 0) {
            capture_cmd = (cmdline[9] == ':') ? cmdline + 10 : "";
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

    /* GL 3.3 core profile (the renderer is a shader/batcher backend now, see cs2d_gl.c) */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

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
                    if (cs2d_client_any_ui_open(&g_client)) {
                        cs2d_client_handle_text_input(&g_client, event.text.text);
                    }
                    break;

                case SDL_EVENT_MOUSE_WHEEL:
                    cs2d_client_handle_mouse_wheel(&g_client, (int)event.wheel.y);
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if ((event.key.key == SDLK_GRAVE || event.key.key == SDLK_F1 ||
                         event.key.key == SDLK_F2    || event.key.key == SDLK_F3) && !event.key.repeat) {
                        cs2d_client_handle_console_key(&g_client, (int)event.key.key);
                        break;
                    }
                    if (cs2d_client_any_ui_open(&g_client)) {
                        cs2d_client_handle_console_key(&g_client, (int)event.key.key);
                        break;
                    }
                    if (event.key.key == SDLK_ESCAPE && !event.key.repeat) {
                        cs2d_client_handle_escape(&g_client);
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
        if (capture_cmd) {
            /* Capture mode: time update + render including GPU work (excludes the 16ms pacing delay) */
            glFinish();
            double work_ms = (double)(SDL_GetPerformanceCounter() - current_time) * 1000.0 / (double)freq;
            frame_ms_sum += work_ms;
            frame_ms_count++;
            if (work_ms > frame_ms_max) frame_ms_max = work_ms;
        }
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
            g_client.options_open = false;
            cs2d_client_console_exec(&g_client, "map prison_funst3r");
            cs2d_client_console_exec(&g_client, "tp 44 25");
        } else if (auto_screenshot && frame_count == 75) {
            save_gl_screenshot_bmp("C:/Users/Gabriel/.gemini/antigravity/brain/b1e9fe06-c5b4-4a13-a42a-859765d696e1/scratch/client_screenshot_prison.bmp",
                                   g_client.config.window_width, g_client.config.window_height);
            cs2d_render_end_frame(window);
            break;
        }
        if (capture_cmd) {
            /* Runs one ';'-separated console command every 5 frames (or after !wait <frames>), then captures 30 frames later */
            if (capture_cmd[0] && frame_count >= capture_resume) {
                char cmd[CS2D_CONSOLE_LINE_LEN];
                size_t len = strcspn(capture_cmd, ";");
                snprintf(cmd, sizeof(cmd), "%.*s", (int)len, capture_cmd);
                capture_cmd += len + (capture_cmd[len] == ';' ? 1 : 0);
                if (strncmp(cmd, "!wait ", 6) == 0) {
                    int w = atoi(cmd + 6);
                    capture_resume = frame_count + (w > 0 ? w : 1);
                } else if (strncmp(capture_cmd, "!shot ", 6) == 0 || strncmp(capture_cmd, "!wait ", 6) == 0) {
                    capture_resume = frame_count + 1;
                } else {
                    capture_resume = frame_count + 5;
                }
                capture_frame = capture_resume + 25;
                if (strncmp(cmd, "!shot ", 6) == 0) {
                    save_gl_screenshot_bmp(cmd + 6, g_client.config.window_width, g_client.config.window_height);
                } else {
                    run_capture_step(cmd);
                }
                if (strncmp(cmd, "!wheel", 6) != 0) { /* Scrolling steps accumulate into one measurement */
                    frame_ms_sum = frame_ms_max = 0.0;
                    frame_ms_count = 0;
                }
            } else if (frame_count == capture_frame) {
                CreateDirectoryA("screens", NULL);
                save_gl_screenshot_bmp("screens/capture.bmp", g_client.config.window_width, g_client.config.window_height);
                printf("[capture] frame time over last %d frames: avg %.3f ms, max %.3f ms\n",
                       frame_ms_count, frame_ms_count ? frame_ms_sum / frame_ms_count : 0.0, frame_ms_max);
                cs2d_render_end_frame(window);
                break;
            }
        }
        cs2d_render_end_frame(window);
        frame_count++;

        SDL_Delay((auto_screenshot || capture_cmd) ? 16 : 1);
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
