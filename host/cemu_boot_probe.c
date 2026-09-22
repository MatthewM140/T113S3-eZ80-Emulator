#include "ce/emulator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ce_revision parse_revision(const char *value)
{
    if (strcmp(value, "pre-a") == 0) return CE_REVISION_PRE_A;
    if (strcmp(value, "m") == 0) return CE_REVISION_M;
    return CE_REVISION_I;
}

int main(int argc, char **argv)
{
    ce_emulator_config config = {CE_BACKEND_CEMU, CE_REVISION_I};
    ce_emulator_debug_state state;
    ce_emulator *emulator;
    unsigned long ticks = 1000000UL;

    if ((argc < 3) || strcmp(argv[1], "--rom") != 0) {
        fprintf(stderr, "usage: %s --rom PATH [--revision pre-a|i|m] [--ticks N]\n", argv[0]);
        return EXIT_FAILURE;
    }
    for (int index = 3; index < argc; ++index) {
        if ((strcmp(argv[index], "--revision") == 0) && index + 1 < argc) {
            config.revision = parse_revision(argv[++index]);
        } else if ((strcmp(argv[index], "--ticks") == 0) && index + 1 < argc) {
            ticks = strtoul(argv[++index], NULL, 10);
        }
    }
    emulator = ce_emulator_create(&config);
    if (emulator == NULL || ce_emulator_load_rom(emulator, argv[2]) != 0) {
        fprintf(stderr, "failed to initialize CEmu backend\n");
        ce_emulator_destroy(emulator);
        return EXIT_FAILURE;
    }
    ce_emulator_run(emulator, ticks);
    if (ce_emulator_get_debug_state(emulator, &state) != 0) {
        ce_emulator_destroy(emulator);
        return EXIT_FAILURE;
    }
    printf("backend=cemu pc=%06X sp=%06X af=%04X bc=%06X de=%06X hl=%06X "
           "ix=%06X iy=%06X mbase=%02X adl=%u madl=%u iff1=%u iff2=%u im=%u "
           "halted=%u cycles=%llu\n",
           state.pc, state.sp, state.af, state.bc, state.de, state.hl,
           state.ix, state.iy, state.mbase, state.adl_mode,
           state.mixed_data_mode, state.iff1, state.iff2,
           state.interrupt_mode, state.halted,
           (unsigned long long)state.cycles);
    ce_emulator_destroy(emulator);
    return EXIT_SUCCESS;
}