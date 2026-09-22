#define _POSIX_C_SOURCE 200809L

#include "ce/emulator.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CEMU_TICKS_PER_SECOND UINT64_C(60)
#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)

enum exit_status {
    EXIT_STATUS_OK = 0,
    EXIT_STATUS_USAGE = 2,
    EXIT_STATUS_ROM = 3,
    EXIT_STATUS_RUNTIME = 4
};

typedef struct application_options {
    const char *rom_path;
    ce_revision revision;
    uint64_t benchmark_ticks;
    int benchmark_enabled;
} application_options;

static volatile sig_atomic_t stop_requested;

static void print_usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s --rom PATH [OPTIONS]\n"
            "\n"
            "Standalone TI-84 Plus CE emulator application. Until a target\n"
            "display/input adapter is selected, normal operation is headless.\n"
            "\n"
            "Options:\n"
            "  --rom PATH              External private TI ROM (required)\n"
            "  --revision pre-a|i|m    ASIC revision (default: i)\n"
            "  --benchmark-ticks N     Run N scheduler ticks unthrottled, report\n"
            "                          throughput, and exit\n"
            "  --help                  Show this help and exit\n",
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

static int parse_positive_u64(const char *value, uint64_t *result)
{
    char *end = NULL;
    uintmax_t parsed;

    if ((value == NULL) || (*value == '\0') || (*value == '-')) {
        return -1;
    }
    errno = 0;
    parsed = strtoumax(value, &end, 10);
    if ((errno == ERANGE) || (*end != '\0') || (parsed == 0U) ||
        (parsed > UINT64_MAX)) {
        return -1;
    }
    *result = (uint64_t)parsed;
    return 0;
}

/* Returns 1 for --help, 0 for success, and -1 for invalid arguments. */
static int parse_options(int argc, char **argv, application_options *options)
{
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--help") == 0) {
            return 1;
        }
        if ((strcmp(argv[index], "--rom") == 0) && (index + 1 < argc)) {
            options->rom_path = argv[++index];
        } else if ((strcmp(argv[index], "--revision") == 0) &&
                   (index + 1 < argc)) {
            if (parse_revision(argv[++index], &options->revision) != 0) {
                fprintf(stderr, "invalid revision: %s\n", argv[index]);
                return -1;
            }
        } else if ((strcmp(argv[index], "--benchmark-ticks") == 0) &&
                   (index + 1 < argc)) {
            if (parse_positive_u64(argv[++index],
                                   &options->benchmark_ticks) != 0) {
                fprintf(stderr, "invalid benchmark tick count: %s\n",
                        argv[index]);
                return -1;
            }
            options->benchmark_enabled = 1;
        } else {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[index]);
            return -1;
        }
    }
    if (options->rom_path == NULL) {
        fprintf(stderr, "missing required --rom PATH\n");
        return -1;
    }
    return 0;
}

static void handle_stop_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_stop_signal;
    if ((sigemptyset(&action.sa_mask) != 0) ||
        (sigaction(SIGINT, &action, NULL) != 0) ||
        (sigaction(SIGTERM, &action, NULL) != 0)) {
        return -1;
    }
    return 0;
}

static uint64_t timespec_to_nanoseconds(const struct timespec *time_value)
{
    return (uint64_t)time_value->tv_sec * NANOSECONDS_PER_SECOND +
           (uint64_t)time_value->tv_nsec;
}

static struct timespec nanoseconds_to_timespec(uint64_t nanoseconds)
{
    struct timespec result;
    result.tv_sec = (time_t)(nanoseconds / NANOSECONDS_PER_SECOND);
    result.tv_nsec = (long)(nanoseconds % NANOSECONDS_PER_SECOND);
    return result;
}

static int run_benchmark(ce_emulator *emulator, uint64_t tick_count)
{
    struct timespec start;
    struct timespec finish;
    uint64_t elapsed_nanoseconds;
    double elapsed_seconds;
    double ticks_per_second;

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime");
        return -1;
    }
    for (uint64_t tick = 0U; tick < tick_count; ++tick) {
        ce_emulator_run(emulator, 1U);
    }
    if (clock_gettime(CLOCK_MONOTONIC, &finish) != 0) {
        perror("clock_gettime");
        return -1;
    }

    elapsed_nanoseconds = timespec_to_nanoseconds(&finish) -
                          timespec_to_nanoseconds(&start);
    elapsed_seconds = (double)elapsed_nanoseconds /
                      (double)NANOSECONDS_PER_SECOND;
    ticks_per_second = elapsed_seconds > 0.0 ?
                       (double)tick_count / elapsed_seconds : 0.0;
    printf("benchmark_ticks=%" PRIu64 "\n", tick_count);
    printf("emulated_seconds=%.6f\n",
           (double)tick_count / (double)CEMU_TICKS_PER_SECOND);
    printf("elapsed_seconds=%.6f\n", elapsed_seconds);
    printf("ticks_per_second=%.3f\n", ticks_per_second);
    printf("realtime_factor=%.3f\n",
           ticks_per_second / (double)CEMU_TICKS_PER_SECOND);
    return 0;
}

static int run_headless_realtime(ce_emulator *emulator)
{
    struct timespec now;
    struct timespec deadline;
    uint64_t deadline_nanoseconds;
    uint64_t tick_remainder = 0U;

    if (install_signal_handlers() != 0) {
        perror("sigaction");
        return -1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        return -1;
    }
    deadline_nanoseconds = timespec_to_nanoseconds(&now);
    fprintf(stderr,
            "calculator-emulator: running headless; SIGINT/SIGTERM exits cleanly\n");

    while (stop_requested == 0) {
        int sleep_result;

        ce_emulator_run(emulator, 1U);
        deadline_nanoseconds += NANOSECONDS_PER_SECOND /
                                CEMU_TICKS_PER_SECOND;
        tick_remainder += NANOSECONDS_PER_SECOND % CEMU_TICKS_PER_SECOND;
        if (tick_remainder >= CEMU_TICKS_PER_SECOND) {
            ++deadline_nanoseconds;
            tick_remainder -= CEMU_TICKS_PER_SECOND;
        }
        deadline = nanoseconds_to_timespec(deadline_nanoseconds);
        do {
            sleep_result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                           &deadline, NULL);
        } while ((sleep_result == EINTR) && (stop_requested == 0));
        if ((sleep_result != 0) && (sleep_result != EINTR)) {
            errno = sleep_result;
            perror("clock_nanosleep");
            return -1;
        }
    }
    fprintf(stderr, "calculator-emulator: shutdown requested\n");
    return 0;
}

int main(int argc, char **argv)
{
    application_options options = {NULL, CE_REVISION_I, 0U, 0};
    ce_emulator_config config = {CE_BACKEND_CEMU, CE_REVISION_I};
    ce_emulator *emulator;
    int parsed = parse_options(argc, argv, &options);
    int run_result;

    if (parsed != 0) {
        print_usage(parsed > 0 ? stdout : stderr, argv[0]);
        return parsed > 0 ? EXIT_STATUS_OK : EXIT_STATUS_USAGE;
    }

    config.revision = options.revision;
    emulator = ce_emulator_create(&config);
    if (emulator == NULL) {
        fprintf(stderr, "failed to create CEmu backend\n");
        return EXIT_STATUS_RUNTIME;
    }
    if (ce_emulator_load_rom(emulator, options.rom_path) != 0) {
        fprintf(stderr, "failed to load ROM: %s\n", options.rom_path);
        ce_emulator_destroy(emulator);
        return EXIT_STATUS_ROM;
    }

    run_result = options.benchmark_enabled != 0 ?
                 run_benchmark(emulator, options.benchmark_ticks) :
                 run_headless_realtime(emulator);
    ce_emulator_destroy(emulator);
    return run_result == 0 ? EXIT_STATUS_OK : EXIT_STATUS_RUNTIME;
}
