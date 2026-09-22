#include "ce/emulator.h"

#include "ce/machine.h"

#include "third_party/cemu/core/asic.h"
#include "third_party/cemu/core/cpu.h"
#include "third_party/cemu/core/emu.h"
#include "third_party/cemu/core/keypad.h"
#include "third_party/cemu/core/lcd.h"

#include <stdlib.h>

void ce_cemu_set_revision(asic_rev_t revision);

struct ce_emulator {
    ce_emulator_config config;
    ce_machine machine;
    ce_rom rom;
    uint32_t *framebuffer;
    uint8_t paused;
    uint8_t loaded;
};

ce_emulator *ce_emulator_create(const ce_emulator_config *config)
{
    ce_emulator *emulator;
    if (config == NULL) {
        return NULL;
    }
    emulator = calloc(1U, sizeof(*emulator));
    if (emulator == NULL) {
        return NULL;
    }
    emulator->config = *config;
    if (config->backend == CE_BACKEND_FROM_SCRATCH) {
        return emulator;
    }
    emulator->framebuffer = calloc(320U * 240U, sizeof(*emulator->framebuffer));
    if (emulator->framebuffer == NULL) {
        free(emulator);
        return NULL;
    }
    ce_cemu_set_revision(config->revision == CE_REVISION_PRE_A ?
                         ASIC_REV_PRE_A :
                         config->revision == CE_REVISION_M ?
                         ASIC_REV_M : ASIC_REV_I);
    return emulator;
}

void ce_emulator_destroy(ce_emulator *emulator)
{
    if (emulator == NULL) {
        return;
    }
    if (emulator->config.backend == CE_BACKEND_FROM_SCRATCH) {
        if (emulator->loaded != 0U) {
            ce_machine_destroy(&emulator->machine);
        }
    } else if (emulator->loaded != 0U) {
        asic_free();
    }
    free(emulator->framebuffer);
    free(emulator);
}

int ce_emulator_load_rom(ce_emulator *emulator, const char *path)
{
    emu_state_t state;
    if ((emulator == NULL) || (path == NULL)) {
        return -1;
    }
    if (emulator->config.backend == CE_BACKEND_FROM_SCRATCH) {
        ce_machine_config machine_config = {emulator->config.revision, 1U};
        if (ce_rom_load_file(path, &emulator->rom) != 0 ||
            ce_machine_init(&emulator->machine, &machine_config,
                            &emulator->rom) != 0) {
            ce_rom_destroy(&emulator->rom);
            return -1;
        }
        emulator->loaded = 1U;
        return 0;
    }
    state = emu_load(EMU_DATA_ROM, path);
    if (state == EMU_STATE_INVALID) {
        return -1;
    }
    emulator->loaded = 1U;
    return 0;
}

void ce_emulator_reset(ce_emulator *emulator)
{
    if ((emulator != NULL) && (emulator->loaded != 0U) &&
        emulator->config.backend == CE_BACKEND_CEMU) {
        emu_reset();
    } else if ((emulator != NULL) && (emulator->loaded != 0U)) {
        ce_machine_reset(&emulator->machine);
    }
}

void ce_emulator_run(ce_emulator *emulator, uint64_t ticks)
{
    if ((emulator != NULL) && (emulator->loaded != 0U) &&
        (emulator->paused == 0U)) {
        if (emulator->config.backend == CE_BACKEND_CEMU) {
            emu_run(ticks);
        } else {
            ce_step_result result;
            for (uint64_t index = 0U; index < ticks; ++index) {
                if (ce_machine_step(&emulator->machine, &result) != CE_STEP_OK) {
                    break;
                }
            }
        }
    }
}

void ce_emulator_pause(ce_emulator *emulator)
{
    if (emulator != NULL) {
        emulator->paused = 1U;
    }
}

void ce_emulator_resume(ce_emulator *emulator)
{
    if (emulator != NULL) {
        emulator->paused = 0U;
    }
}

int ce_emulator_is_paused(const ce_emulator *emulator)
{
    return (emulator != NULL) && (emulator->paused != 0U);
}

const uint32_t *ce_emulator_framebuffer(ce_emulator *emulator)
{
    if (emulator == NULL || emulator->framebuffer == NULL) {
        return NULL;
    }
    if (emulator->config.backend == CE_BACKEND_CEMU) {
        emu_lcd_drawframe(emulator->framebuffer);
    }
    return emulator->framebuffer;
}

int ce_emulator_get_debug_state(const ce_emulator *emulator,
                                ce_emulator_debug_state *state)
{
    if ((emulator == NULL) || (state == NULL) || (emulator->loaded == 0U)) {
        return -1;
    }
    if (emulator->config.backend == CE_BACKEND_FROM_SCRATCH) {
        const ce_cpu_state *cpu_state = ce_machine_cpu_state(&emulator->machine);
        state->pc = cpu_state->pc;
        state->sp = cpu_state->sp;
        state->af = cpu_state->af;
        state->bc = cpu_state->bc;
        state->de = cpu_state->de;
        state->hl = cpu_state->hl;
        state->ix = cpu_state->ix;
        state->iy = cpu_state->iy;
        state->mbase = cpu_state->mbase;
        state->cycles = cpu_state->cycles;
        state->adl_mode = cpu_state->control_adl_mode;
        state->mixed_data_mode = cpu_state->data_adl_mode;
        state->iff1 = cpu_state->iff1;
        state->iff2 = cpu_state->iff2;
        state->interrupt_mode = cpu_state->interrupt_mode;
        state->halted = cpu_state->halted;
        return 0;
    }
    state->pc = cpu.registers.PC;
    state->sp = cpu.registers.stack[cpu.ADL].hl;
    state->af = cpu.registers.AF;
    state->bc = cpu.registers.BC;
    state->de = cpu.registers.DE;
    state->hl = cpu.registers.HL;
    state->ix = cpu.registers.IX;
    state->iy = cpu.registers.IY;
    state->mbase = cpu.registers.MBASE;
    state->cycles = cpu.cycles;
    state->adl_mode = cpu.ADL;
    state->mixed_data_mode = cpu.MADL;
    state->iff1 = cpu.IEF1;
    state->iff2 = cpu.IEF2;
    state->interrupt_mode = cpu.IM;
    state->halted = cpu.halted;
    return 0;
}

void ce_emulator_key(ce_emulator *emulator, unsigned int row,
                     unsigned int column, int pressed)
{
    if ((emulator != NULL) && (emulator->loaded != 0U) &&
        emulator->config.backend == CE_BACKEND_CEMU) {
        emu_keypad_event(row, column, pressed != 0);
    }
}
