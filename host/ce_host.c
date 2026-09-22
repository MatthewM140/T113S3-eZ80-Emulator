#include "ce/emulator.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * CEmu's LCD pipeline (core/lcd.c: lcd_argb8888out/lcd_rgb565out/lcd_rgb888out)
 * always produces pixels as 0xAARRGGBB in a native uint32_t: alpha in the top
 * byte, then red, green, blue. That is SDL_PIXELFORMAT_ARGB8888, NOT
 * RGBA8888. Extract channels accordingly.
 */
static int write_ppm(const char *path, const uint32_t *pixels)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) return -1;
    fprintf(file, "P6\n320 240\n255\n");
    for (unsigned int index = 0U; index < 320U * 240U; ++index) {
        uint8_t rgb[3] = {
            (uint8_t)(pixels[index] >> 16U),
            (uint8_t)(pixels[index] >> 8U),
            (uint8_t)(pixels[index])
        };
        if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
            fclose(file);
            return -1;
        }
    }
    fclose(file);
    return 0;
}

static int key_position(SDL_Scancode scancode, unsigned int *row,
                        unsigned int *column)
{
    static const struct { SDL_Scancode key; unsigned int row; unsigned int column; } map[] = {
        {SDL_SCANCODE_F5, 1U, 0U}, {SDL_SCANCODE_F4, 1U, 1U},
        {SDL_SCANCODE_F3, 1U, 2U}, {SDL_SCANCODE_F2, 1U, 3U},
        {SDL_SCANCODE_F1, 1U, 4U}, {SDL_SCANCODE_TAB, 1U, 5U},
        {SDL_SCANCODE_HOME, 1U, 6U}, {SDL_SCANCODE_DELETE, 1U, 7U},
        {SDL_SCANCODE_F12, 2U, 0U}, {SDL_SCANCODE_X, 2U, 1U},
        {SDL_SCANCODE_S, 2U, 2U}, {SDL_SCANCODE_N, 2U, 3U},
        {SDL_SCANCODE_I, 2U, 4U}, {SDL_SCANCODE_D, 2U, 5U},
        {SDL_SCANCODE_M, 2U, 6U}, {SDL_SCANCODE_APOSTROPHE, 2U, 7U},
        {SDL_SCANCODE_0, 3U, 0U}, {SDL_SCANCODE_1, 3U, 1U},
        {SDL_SCANCODE_4, 3U, 2U}, {SDL_SCANCODE_7, 3U, 3U},
        {SDL_SCANCODE_COMMA, 3U, 4U}, {SDL_SCANCODE_E, 3U, 5U},
        {SDL_SCANCODE_PAGEUP, 3U, 6U}, {SDL_SCANCODE_X, 3U, 7U},
        {SDL_SCANCODE_PERIOD, 4U, 0U}, {SDL_SCANCODE_2, 4U, 1U},
        {SDL_SCANCODE_5, 4U, 2U}, {SDL_SCANCODE_8, 4U, 3U},
        {SDL_SCANCODE_LEFTBRACKET, 4U, 4U}, {SDL_SCANCODE_F, 4U, 5U},
        {SDL_SCANCODE_PAGEDOWN, 4U, 6U}, {SDL_SCANCODE_END, 4U, 7U},
        {SDL_SCANCODE_GRAVE, 5U, 0U}, {SDL_SCANCODE_3, 5U, 1U},
        {SDL_SCANCODE_6, 5U, 2U}, {SDL_SCANCODE_9, 5U, 3U},
        {SDL_SCANCODE_RIGHTBRACKET, 5U, 4U}, {SDL_SCANCODE_G, 5U, 5U},
        {SDL_SCANCODE_INSERT, 5U, 6U},
        {SDL_SCANCODE_RETURN, 6U, 0U}, {SDL_SCANCODE_EQUALS, 6U, 1U},
        {SDL_SCANCODE_MINUS, 6U, 2U}, {SDL_SCANCODE_KP_MULTIPLY, 6U, 3U},
        {SDL_SCANCODE_KP_DIVIDE, 6U, 4U}, {SDL_SCANCODE_H, 6U, 5U},
        {SDL_SCANCODE_ESCAPE, 6U, 6U},
        {SDL_SCANCODE_DOWN, 7U, 0U}, {SDL_SCANCODE_LEFT, 7U, 1U},
        {SDL_SCANCODE_RIGHT, 7U, 2U}, {SDL_SCANCODE_UP, 7U, 3U}
    };
    for (unsigned int index = 0U; index < sizeof(map) / sizeof(map[0]); ++index) {
        if (map[index].key == scancode) {
            *row = map[index].row;
            *column = map[index].column;
            return 0;
        }
    }
    return -1;
}

static ce_revision parse_revision(const char *value)
{
    if (strcmp(value, "pre-a") == 0) return CE_REVISION_PRE_A;
    if (strcmp(value, "m") == 0) return CE_REVISION_M;
    return CE_REVISION_I;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL;
    const char *screenshot_path = NULL;
    unsigned int frame_limit = 0U;
    unsigned int scale = 3U;
    ce_emulator_config config = {CE_BACKEND_CEMU, CE_REVISION_I};
    ce_emulator *emulator;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Event event;
    int running = 1;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--rom") == 0 && index + 1 < argc) {
            rom_path = argv[++index];
        } else if (strcmp(argv[index], "--revision") == 0 && index + 1 < argc) {
            config.revision = parse_revision(argv[++index]);
        } else if (strcmp(argv[index], "--screenshot") == 0 && index + 1 < argc) {
            screenshot_path = argv[++index];
        } else if (strcmp(argv[index], "--frames") == 0 && index + 1 < argc) {
            frame_limit = (unsigned int)strtoul(argv[++index], NULL, 10);
        } else if (strcmp(argv[index], "--scale") == 0 && index + 1 < argc) {
            scale = (unsigned int)strtoul(argv[++index], NULL, 10);
            if (scale == 0U) scale = 1U;
        }
    }
    if (rom_path == NULL || SDL_Init(SDL_INIT_VIDEO) < 0) return EXIT_FAILURE;
    emulator = ce_emulator_create(&config);
    if (emulator == NULL || ce_emulator_load_rom(emulator, rom_path) != 0) return EXIT_FAILURE;
    window = SDL_CreateWindow("TI-84 Plus CE", 320 * (int)scale,
                              240 * (int)scale, 0);
    renderer = window == NULL ? NULL : SDL_CreateRenderer(window, NULL);
    /* Matches CEmu's native 0xAARRGGBB pixel layout (see write_ppm above). */
    texture = renderer == NULL ? NULL : SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                                            SDL_TEXTUREACCESS_STREAMING, 320, 240);
    if (window == NULL || renderer == NULL || texture == NULL) return EXIT_FAILURE;
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderLogicalPresentation(renderer, 320, 240, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = 0;
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                unsigned int row;
                unsigned int column;
                if (key_position(event.key.scancode, &row, &column) == 0) {
                    ce_emulator_key(emulator, row, column,
                                    event.type == SDL_EVENT_KEY_DOWN);
                }
            }
        }
        /*
         * The CEmu backend's "ticks" are CLOCK_RUN ticks, which run at
         * 60 Hz by default (see third_party/cemu/core/schedule.c
         * sched_reset()). The real CEmu Qt frontend advances exactly one
         * tick per loop iteration (gui/qt/emuthread.cpp
         * EmuThread::run(): emu_run(1u)), i.e. 1/60 emulated second per
         * iteration. Advancing a huge tick count per host video frame (as
         * this used to do, 1000000 ticks =~ 4.6 emulated hours per call)
         * made --frames hang, and gave TI-OS's own auto-power-down long
         * enough to blank the LCD (clearing LCD control bit 11, the panel
         * power-enable bit) before the first screenshot was ever taken --
         * which is why the framebuffer came back solid black. One tick
         * per host frame matches real hardware/GUI pacing.
         */
        ce_emulator_run(emulator, 1U);
        const uint32_t *pixels = ce_emulator_framebuffer(emulator);
        SDL_UpdateTexture(texture, NULL, pixels, 320 * 4);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        if (frame_limit != 0U) {
            --frame_limit;
            if (frame_limit == 0U) {
                if (screenshot_path != NULL) {
                    write_ppm(screenshot_path, pixels);
                }
                running = 0;
            }
        } else if (screenshot_path != NULL) {
            /* Unbounded run: keep refreshing the same path every frame. */
            write_ppm(screenshot_path, pixels);
        }
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    ce_emulator_destroy(emulator);
    SDL_Quit();
    return EXIT_SUCCESS;
}
