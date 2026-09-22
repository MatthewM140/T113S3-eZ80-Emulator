#ifndef CE_MACHINE_H
#define CE_MACHINE_H

#include "ce/rom.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CE_RAM_SIZE_BYTES (256U * 1024U)
#define CE_VRAM_SIZE_BYTES (320U * 240U * 2U)
#define CE_FRAMEBUFFER_WIDTH 320U
#define CE_FRAMEBUFFER_HEIGHT 240U

typedef struct ce_machine_config {
    /* Unknown is rejected until reliable ROM identification exists. */
    ce_revision revision;
    uint8_t initial_adl_mode;
} ce_machine_config;

typedef struct ce_cpu_state {
    uint32_t pc;
    uint32_t sp;
    uint32_t ix;
    uint32_t iy;
    uint32_t hl;
    uint32_t af;
    uint32_t bc;
    uint32_t de;
    uint32_t mbase;
    uint64_t cycles;
    uint8_t adl_mode;
    uint8_t data_adl_mode;
    uint8_t control_adl_mode;
    uint8_t halted;
    uint8_t iff1;
    uint8_t iff2;
    uint8_t interrupt_mode;
    uint8_t ei_delay;
} ce_cpu_state;

typedef struct ce_machine {
    ce_rom rom;
    uint8_t *ram;
    uint8_t *vram;
    uint16_t *framebuffer;
    ce_revision revision;
    uint8_t initial_adl_mode;
    ce_cpu_state cpu;
    uint16_t last_io_port;
    uint8_t last_io_value;
    uint64_t io_write_count;
    uint8_t io_ports[256];
    uint32_t last_bus_address;
    uint8_t last_bus_write;
    uint32_t privileged_end;
    uint8_t nmi_pending;
    uint8_t flash_write_attempted;
    uint8_t flash_unlocked;
    uint8_t paused;
    uint32_t interrupt_raw;
    uint32_t interrupt_latched;
    uint32_t interrupt_enable;
    uint32_t interrupt_latch;
    uint32_t interrupt_invert;
} ce_machine;

typedef struct ce_fetch_result {
    uint32_t address;
    uint8_t opcode;
    uint64_t cycles;
} ce_fetch_result;

typedef enum ce_step_status {
    CE_STEP_OK = 0,
    CE_STEP_HALTED,
    CE_STEP_UNIMPLEMENTED,
    CE_STEP_IO_UNIMPLEMENTED,
    CE_STEP_FLASH_UNIMPLEMENTED,
    CE_STEP_ERROR
} ce_step_status;

typedef struct ce_step_result {
    ce_step_status status;
    uint32_t address;
    uint8_t opcode;
    uint64_t cycles;
} ce_step_result;

int ce_machine_init(ce_machine *machine,
                    const ce_machine_config *config,
                    ce_rom *rom);

void ce_machine_destroy(ce_machine *machine);

void ce_machine_reset(ce_machine *machine);

void ce_machine_pause(ce_machine *machine);
void ce_machine_resume(ce_machine *machine);
int ce_machine_is_paused(const ce_machine *machine);

const ce_cpu_state *ce_machine_cpu_state(const ce_machine *machine);
const uint16_t *ce_machine_framebuffer(const ce_machine *machine);

uint32_t ce_machine_effective_data_address(const ce_machine *machine,
                                           uint32_t address);

/* Sets a modeled first-bank interrupt source level for deterministic tests. */
void ce_machine_set_interrupt_source(ce_machine *machine,
                                     unsigned int source,
                                     uint8_t asserted);

uint32_t ce_machine_interrupt_masked_status(const ce_machine *machine);
void ce_machine_acknowledge_interrupt(ce_machine *machine, uint32_t mask);

/* Fetches one opcode byte and advances PC; decoding comes in the CPU phase. */
int ce_machine_fetch_opcode(ce_machine *machine, ce_fetch_result *result);
ce_step_status ce_machine_step(ce_machine *machine, ce_step_result *result);

#ifdef __cplusplus
}
#endif

#endif