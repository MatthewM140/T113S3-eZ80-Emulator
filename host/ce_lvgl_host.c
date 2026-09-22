#include "ce/emulator.h"
#include "ce_lvgl_app.h"
#include "ce_lvgl_sdl3.h"

#include <lvgl/lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CEMU_TICKS_PER_SECOND 60U
#define TICK_ACCUMULATOR_SCALE 1000U
#define MAX_ELAPSED_MILLISECONDS 250U

typedef struct host_options {
    const char *rom_path;
    const char *screenshot_path;
    ce_revision revision;
    unsigned int scale;
    uint64_t frame_limit;
} host_options;

static void print_usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --rom PATH [--revision pre-a|i|m] [--scale N] "
            "[--frames N] [--screenshot PATH]\n",
            program);
}

static int parse_revision(const char *value, ce_revision *revision)
{
    if (strcmp(value, "pre-a") == 0) {
        *revision = CE_REVISION_PRE_A;
    } else if (strcmp(value, "i") == 0) {
        *revision = CE_REVISION_I;
    } else if (strcmp(value, "m") == 0) {
        *revision = CE_REVISION_M;
    } else {
        return -1;
    }
    return 0;
}

static int parse_unsigned(const char *value, unsigned long long maximum,
                          unsigned long long *result)
{
    char *end = NULL;
    unsigned long long parsed;

    if ((value == NULL) || (*value == '\0') || (*value == '-')) {
        return -1;
    }
    parsed = strtoull(value, &end, 10);
    if ((*end != '\0') || (parsed > maximum)) {
        return -1;
    }
    *result = parsed;
    return 0;
}

static int parse_options(int argc, char **argv, host_options *options)
{
    for (int index = 1; index < argc; ++index) {
        if ((strcmp(argv[index], "--rom") == 0) && (index + 1 < argc)) {
            options->rom_path = argv[++index];
        } else if ((strcmp(argv[index], "--revision") == 0) &&
                   (index + 1 < argc)) {
            if (parse_revision(argv[++index], &options->revision) != 0) {
                return -1;
            }
        } else if ((strcmp(argv[index], "--scale") == 0) &&
                   (index + 1 < argc)) {
            unsigned long long scale;
            if ((parse_unsigned(argv[++index], 16ULL, &scale) != 0) ||
                (scale == 0ULL)) {
                return -1;
            }
            options->scale = (unsigned int)scale;
        } else if ((strcmp(argv[index], "--frames") == 0) &&
                   (index + 1 < argc)) {
            unsigned long long frames;
            if (parse_unsigned(argv[++index], UINT64_MAX, &frames) != 0) {
                return -1;
            }
            options->frame_limit = (uint64_t)frames;
        } else if ((strcmp(argv[index], "--screenshot") == 0) &&
                   (index + 1 < argc)) {
            options->screenshot_path = argv[++index];
        } else if (strcmp(argv[index], "--help") == 0) {
            return 1;
        } else {
            return -1;
        }
    }
    return options->rom_path == NULL ? -1 : 0;
}

static void forward_key(void *context, unsigned int row,
                        unsigned int column, int pressed)
{
    ce_emulator_key(context, row, column, pressed);
}

int main(int argc, char **argv)
{
    host_options options = {NULL, NULL, CE_REVISION_I, 3U, 0U};
    ce_emulator_config emulator_config = {CE_BACKEND_CEMU, CE_REVISION_I};
    ce_emulator *emulator = NULL;
    ce_lvgl_sdl3 *platform = NULL;
    ce_lvgl_app app = {0};
    uint32_t last_milliseconds;
    uint32_t accumulator = TICK_ACCUMULATOR_SCALE;
    uint64_t completed_frames = 0U;
    int exit_code = EXIT_FAILURE;
    int parsed = parse_options(argc, argv, &options);

    if (parsed != 0) {
        print_usage(argv[0]);
        return parsed > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    emulator_config.revision = options.revision;
    emulator = ce_emulator_create(&emulator_config);
    if ((emulator == NULL) ||
        (ce_emulator_load_rom(emulator, options.rom_path) != 0)) {
        fprintf(stderr, "failed to initialize CEmu backend\n");
        goto cleanup;
    }

    lv_init();
    platform = ce_lvgl_sdl3_create(CE_LVGL_LCD_WIDTH, CE_LVGL_LCD_HEIGHT,
                                   options.scale, "TI-84 Plus CE - LVGL",
                                   forward_key, emulator);
    if ((platform == NULL) || (ce_lvgl_app_init(&app, emulator) != 0)) {
        fprintf(stderr, "failed to initialize LVGL frontend\n");
        goto cleanup_lvgl;
    }

    last_milliseconds = lv_tick_get();
    while (ce_lvgl_sdl3_process_events(platform)) {
        uint32_t now = lv_tick_get();
        uint32_t elapsed = now - last_milliseconds;
        int limit_reached = 0;
        last_milliseconds = now;
        if (elapsed > MAX_ELAPSED_MILLISECONDS) {
            elapsed = MAX_ELAPSED_MILLISECONDS;
        }
        accumulator += elapsed * CEMU_TICKS_PER_SECOND;

        while (accumulator >= TICK_ACCUMULATOR_SCALE) {
            if (ce_lvgl_app_tick(&app) != 0) {
                fprintf(stderr, "failed to advance emulator frontend\n");
                goto cleanup_lvgl;
            }
            accumulator -= TICK_ACCUMULATOR_SCALE;
            ++completed_frames;
            if ((options.frame_limit != 0U) &&
                (completed_frames >= options.frame_limit)) {
                limit_reached = 1;
                break;
            }
        }

        (void)lv_timer_handler();
        if (limit_reached != 0) {
            lv_refr_now(ce_lvgl_sdl3_display(platform));
            if ((options.screenshot_path != NULL) &&
                (ce_lvgl_sdl3_write_ppm(platform,
                                         options.screenshot_path) != 0)) {
                fprintf(stderr, "failed to write LVGL screenshot: %s\n",
                        options.screenshot_path);
                goto cleanup_lvgl;
            }
            break;
        }
        lv_delay_ms(1U);
    }

    exit_code = EXIT_SUCCESS;

cleanup_lvgl:
    ce_lvgl_sdl3_destroy(platform);
    lv_deinit();
cleanup:
    ce_emulator_destroy(emulator);
    return exit_code;
}
