#ifndef CE_EMULATOR_H
#define CE_EMULATOR_H

#include "ce/rom.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ce_backend {
    CE_BACKEND_FROM_SCRATCH = 0,
    CE_BACKEND_CEMU = 1
} ce_backend;

typedef struct ce_emulator_config {
    ce_backend backend;
    ce_revision revision;
} ce_emulator_config;

typedef struct ce_emulator_debug_state {
    uint32_t pc;
    uint32_t sp;
    uint32_t af;
    uint32_t bc;
    uint32_t de;
    uint32_t hl;
    uint32_t ix;
    uint32_t iy;
    uint32_t mbase;
    uint64_t cycles;
    uint8_t adl_mode;
    uint8_t mixed_data_mode;
    uint8_t iff1;
    uint8_t iff2;
    uint8_t interrupt_mode;
    uint8_t halted;
} ce_emulator_debug_state;

typedef struct ce_emulator ce_emulator;

ce_emulator *ce_emulator_create(const ce_emulator_config *config);
void ce_emulator_destroy(ce_emulator *emulator);
int ce_emulator_load_rom(ce_emulator *emulator, const char *path);
void ce_emulator_reset(ce_emulator *emulator);
void ce_emulator_run(ce_emulator *emulator, uint64_t ticks);
void ce_emulator_pause(ce_emulator *emulator);
void ce_emulator_resume(ce_emulator *emulator);
int ce_emulator_is_paused(const ce_emulator *emulator);
const uint32_t *ce_emulator_framebuffer(ce_emulator *emulator);
int ce_emulator_get_debug_state(const ce_emulator *emulator,
                                ce_emulator_debug_state *state);
void ce_emulator_key(ce_emulator *emulator, unsigned int row,
                     unsigned int column, int pressed);

#ifdef __cplusplus
}
#endif

#endif
