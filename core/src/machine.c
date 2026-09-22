#include "ce/machine.h"

#include <stdlib.h>
#include <string.h>

static void ce_machine_clear(ce_machine *machine) { memset(machine, 0, sizeof(*machine)); }
static int ce_machine_read_memory(const ce_machine *machine, uint32_t address, uint8_t *value);
static int ce_machine_write_memory(ce_machine *machine, uint32_t address, uint8_t value);
static uint32_t ce_machine_interrupt_status(const ce_machine *machine)
{
    uint32_t signal = machine->interrupt_raw ^ machine->interrupt_invert;
    return (signal & ~machine->interrupt_latch) |
           (machine->interrupt_latched & machine->interrupt_latch);
}

static int ce_machine_read_interrupt_register(const ce_machine *machine,
                                              uint32_t address,
                                              uint8_t *value)
{
    uint32_t register_value;
    uint32_t offset = address - 0xF00000U;
    uint32_t register_offset = offset & ~3U;
    if (offset > 0x17U) return -1;
    switch (register_offset) {
    case 0x00U: register_value = ce_machine_interrupt_status(machine); break;
    case 0x04U: register_value = machine->interrupt_enable; break;
    case 0x14U: register_value = ce_machine_interrupt_status(machine) &
                                       machine->interrupt_enable; break;
    default: return -1;
    }
    *value = (uint8_t)(register_value >> ((address & 3U) * 8U));
    return 0;
}

static int ce_machine_write_interrupt_register(ce_machine *machine,
                                               uint32_t address,
                                               uint8_t value)
{
    uint32_t offset = address - 0xF00000U;
    uint32_t shift = (address & 3U) * 8U;
    uint32_t mask = 0xFFU << shift;
    if (offset > 0x10U) return -1;
    switch (offset & ~3U) {
    case 0x04U:
        machine->interrupt_enable = (machine->interrupt_enable & ~mask) |
                                    ((uint32_t)value << shift);
        return 0;
    case 0x08U:
        machine->interrupt_latched &= ~((uint32_t)value << shift);
        return 0;
    case 0x0CU:
        machine->interrupt_latch = (machine->interrupt_latch & ~mask) |
                                   ((uint32_t)value << shift);
        return 0;
    case 0x10U:
        machine->interrupt_invert = (machine->interrupt_invert & ~mask) |
                                    ((uint32_t)value << shift);
        return 0;
    default: return -1;
    }
}
static int ce_machine_read_flash(const ce_machine *machine, uint32_t address, uint8_t *value)
{
    uint32_t physical = machine->cpu.control_adl_mode != 0U ? address & 0xFFFFFFU :
                        (machine->cpu.mbase << 16U) | (address & 0xFFFFU);
    return ce_machine_read_memory(machine, physical, value);
}
static unsigned ce_machine_control_width(const ce_machine *machine) { return machine->cpu.control_adl_mode != 0U ? 3U : 2U; }
static unsigned ce_machine_stack_width(const ce_machine *machine) { return ce_machine_control_width(machine); }
static int ce_machine_read_instruction_value(ce_machine *machine, uint32_t *value)
{
    uint32_t result = 0U;
    unsigned width = ce_machine_control_width(machine);
    for (unsigned index = 0U; index < width; ++index) {
        uint8_t byte;
        if (ce_machine_read_flash(machine, machine->cpu.pc + index, &byte) != 0) return -1;
        result |= (uint32_t)byte << (index * 8U);
    }
    machine->cpu.pc = (machine->cpu.pc + width) & 0xFFFFFFU;
    *value = result;
    return 0;
}
static int ce_machine_read_memory(const ce_machine *machine, uint32_t address, uint8_t *value)
{
    if ((machine == NULL) || (value == NULL)) return -1;
    if ((address >= 0xF00000U) && (address < 0xF00018U)) {
        return ce_machine_read_interrupt_register(machine, address, value);
    }
    if ((address >= 0xD00000U) && (address < 0xD00000U + CE_RAM_SIZE_BYTES)) { *value = machine->ram[address - 0xD00000U]; return 0; }
    if ((address >= 0xD40000U) && (address < 0xD40000U + CE_VRAM_SIZE_BYTES)) { *value = machine->vram[address - 0xD40000U]; return 0; }
    if (address < machine->rom.size) { *value = machine->rom.bytes[address]; return 0; }
    return -1;
}
static uint32_t ce_machine_data_address(const ce_machine *machine, uint32_t address)
{
    return machine->cpu.data_adl_mode != 0U ? address & 0xFFFFFFU :
           (machine->cpu.mbase << 16U) | (address & 0xFFFFU);
}

uint32_t ce_machine_effective_data_address(const ce_machine *machine, uint32_t address)
{
    return ce_machine_data_address(machine, address);
}

void ce_machine_set_interrupt_source(ce_machine *machine,
                                     unsigned int source,
                                     uint8_t asserted)
{
    uint32_t bit;
    uint32_t old_signal;
    uint32_t new_signal;

    if ((machine == NULL) || (source >= 22U)) return;
    bit = (uint32_t)1U << source;
    old_signal = machine->interrupt_raw ^ machine->interrupt_invert;
    if (asserted != 0U) machine->interrupt_raw |= bit;
    else machine->interrupt_raw &= ~bit;
    new_signal = machine->interrupt_raw ^ machine->interrupt_invert;
    if (((machine->interrupt_latch & bit) != 0U) &&
        ((old_signal & bit) == 0U) && ((new_signal & bit) != 0U)) {
        machine->interrupt_latched |= bit;
    }
}

uint32_t ce_machine_interrupt_masked_status(const ce_machine *machine)
{
    return machine == NULL ? 0U : ce_machine_interrupt_status(machine) &
                                      machine->interrupt_enable;
}

void ce_machine_acknowledge_interrupt(ce_machine *machine, uint32_t mask)
{
    if (machine != NULL) machine->interrupt_latched &= ~mask;
}

static int ce_machine_read_data(const ce_machine *machine, uint32_t address, uint8_t *value)
{
    return ce_machine_read_memory(machine, ce_machine_data_address(machine, address), value);
}

static uint32_t ce_machine_hl_address(const ce_machine *machine, uint32_t offset)
{
        return machine->cpu.data_adl_mode != 0U ?
           (machine->cpu.hl + offset) & 0xFFFFFFU :
           (machine->cpu.mbase << 16U) | ((machine->cpu.hl + offset) & 0xFFFFU);
}

static int ce_machine_read_hl(const ce_machine *machine, uint32_t offset,
                              uint8_t *value)
{
    return ce_machine_read_memory(machine, ce_machine_hl_address(machine, offset),
                                  value);
}

static int ce_machine_write_memory(ce_machine *machine, uint32_t address, uint8_t value)
{
    if (machine == NULL) return -1;
    machine->last_bus_address = address;
    machine->last_bus_write = 1U;
    if ((address >= 0xF00000U) && (address < 0xF00018U)) {
        return ce_machine_write_interrupt_register(machine, address, value);
    }
    if (address < machine->rom.size) {
        machine->flash_write_attempted = 1U;
        if (machine->cpu.pc > machine->privileged_end) { machine->nmi_pending = 1U; return -2; }
        return machine->flash_unlocked != 0U ? -3 : 0;
    }
    if ((address >= 0xD00000U) && (address < 0xD00000U + CE_RAM_SIZE_BYTES)) { machine->ram[address - 0xD00000U] = value; return 0; }
    if ((address >= 0xD40000U) && (address < 0xD40000U + CE_VRAM_SIZE_BYTES)) { machine->vram[address - 0xD40000U] = value; return 0; }
    return -1;
}

static int ce_machine_write_data(ce_machine *machine, uint32_t address, uint8_t value)
{
    return ce_machine_write_memory(machine, ce_machine_data_address(machine, address), value);
}

static uint32_t ce_machine_stack_address(const ce_machine *machine, uint32_t address)
{
    return machine->cpu.control_adl_mode != 0U ? address & 0xFFFFFFU :
           (machine->cpu.mbase << 16U) | (address & 0xFFFFU);
}

static int ce_machine_read_stack(const ce_machine *machine, uint32_t address, uint8_t *value)
{
    return ce_machine_read_memory(machine, ce_machine_stack_address(machine, address), value);
}

static int ce_machine_write_stack(ce_machine *machine, uint32_t address, uint8_t value)
{
    return ce_machine_write_memory(machine, ce_machine_stack_address(machine, address), value);
}

static int ce_machine_accept_interrupt(ce_machine *machine,
                                       ce_step_result *result)
{
    unsigned int width = ce_machine_stack_width(machine);
    uint32_t new_sp = machine->cpu.sp - width;
    uint32_t return_pc = machine->cpu.pc;

    for (unsigned int index = 0U; index < width; ++index) {
        if (ce_machine_write_stack(machine, new_sp + index,
                                   (uint8_t)(return_pc >> (index * 8U))) != 0) {
            result->status = CE_STEP_ERROR;
            return -1;
        }
    }
    machine->cpu.sp = new_sp;
    machine->cpu.iff1 = 0U;
    machine->cpu.iff2 = 0U;
    machine->cpu.halted = 0U;
    machine->cpu.pc = machine->cpu.interrupt_mode == 1U ? 0x0038U : 0x0038U;
    result->address = return_pc;
    result->opcode = 0xFFU;
    result->cycles = 7U;
    result->status = CE_STEP_OK;
    machine->cpu.cycles += result->cycles;
    return 0;
}

static void ce_machine_write_io_port(ce_machine *machine, uint16_t port, uint8_t value)
{
    uint8_t stored = value;
    if (port == 0x06U) {
        stored = (uint8_t)((machine->io_ports[port & 0xFFU] & 0x80U) |
                           (value & 0x7FU));
        if ((value & 0x04U) == 0U) machine->flash_unlocked = 0U;
    } else if (port == 0x28U) {
        stored = (uint8_t)((machine->io_ports[port & 0xFFU] & 0xF1U) |
                           (value & 0x04U));
    } else if (port == 0x0DU) {
        stored = (uint8_t)((value & 0x0FU) | ((value & 0x0FU) << 4U));
    }
    machine->last_io_port = port;
    machine->last_io_value = value;
    machine->io_ports[port & 0xFFU] = stored;
    machine->io_write_count++;
}

static uint8_t ce_cpu_cp_flags(uint8_t accumulator,
                               uint8_t operand,
                               uint8_t carry)
{
    uint8_t difference = (uint8_t)(accumulator - operand - carry);
    uint8_t flags = 0x02U;
    if ((difference & 0x80U) != 0U) flags |= 0x80U;
    if (difference == 0U) flags |= 0x40U;
    if ((accumulator & 0x0FU) < ((operand & 0x0FU) + carry)) flags |= 0x10U;
    if (((accumulator ^ operand) & (accumulator ^ difference) & 0x80U) != 0U) flags |= 0x04U;
    if ((uint16_t)accumulator < (uint16_t)operand + carry) flags |= 0x01U;
    return flags;
}

static uint8_t ce_cpu_adc_flags(uint8_t accumulator,
                                uint8_t operand,
                                uint8_t carry,
                                uint8_t *result)
{
    uint16_t sum = (uint16_t)((uint16_t)accumulator +
                              (uint16_t)operand + (uint16_t)carry);
    uint8_t value = (uint8_t)sum;
    uint8_t flags = (uint8_t)(value & 0xA8U);

    if (value == 0U) flags |= 0x40U;
    if (((accumulator ^ operand ^ value) & 0x10U) != 0U) flags |= 0x10U;
    if (((~(accumulator ^ operand) & (accumulator ^ value)) & 0x80U) != 0U) flags |= 0x04U;
    if ((sum & 0x100U) != 0U) flags |= 0x01U;
    *result = value;
    return flags;
}

static uint8_t ce_cpu_parity(uint8_t value);

static void ce_cpu_daa(ce_machine *machine)
{
    uint8_t old = (uint8_t)(machine->cpu.af >> 8U);
    uint8_t flags = (uint8_t)machine->cpu.af;
    uint8_t adjustment = 0U;
    uint8_t carry = (uint8_t)(flags & 0x01U);
    if ((flags & 0x10U) != 0U || (old & 0x0FU) > 9U) adjustment |= 0x06U;
    if (carry != 0U || old > 0x99U) {
        adjustment |= 0x60U;
        carry = 1U;
    }
    old = (flags & 0x02U) != 0U ? (uint8_t)(old - adjustment) :
                                  (uint8_t)(old + adjustment);
    flags = (uint8_t)(old & 0xA8U);
    if (old == 0U) flags |= 0x40U;
    if (ce_cpu_parity(old) != 0U) flags |= 0x04U;
    if (carry != 0U) flags |= 0x01U;
    machine->cpu.af = ((uint32_t)old << 8U) | flags;
}

static uint8_t ce_cpu_cb_read_register(const ce_machine *machine,
                                       unsigned int index)
{
    switch (index) {
    case 0U: return (uint8_t)(machine->cpu.bc >> 8U);
    case 1U: return (uint8_t)machine->cpu.bc;
    case 2U: return (uint8_t)(machine->cpu.de >> 8U);
    case 3U: return (uint8_t)machine->cpu.de;
    case 4U: return (uint8_t)(machine->cpu.hl >> 8U);
    case 5U: return (uint8_t)machine->cpu.hl;
    case 7U: return (uint8_t)(machine->cpu.af >> 8U);
    default: return 0U;
    }
}

static void ce_cpu_cb_write_register(ce_machine *machine,
                                     unsigned int index,
                                     uint8_t value)
{
    switch (index) {
    case 0U: machine->cpu.bc = (machine->cpu.bc & 0xFFFF00FFU) | ((uint32_t)value << 8U); break;
    case 1U: machine->cpu.bc = (machine->cpu.bc & 0xFFFFFF00U) | value; break;
    case 2U: machine->cpu.de = (machine->cpu.de & 0xFFFF00FFU) | ((uint32_t)value << 8U); break;
    case 3U: machine->cpu.de = (machine->cpu.de & 0xFFFFFF00U) | value; break;
    case 4U: machine->cpu.hl = (machine->cpu.hl & 0xFFFF00FFU) | ((uint32_t)value << 8U); break;
    case 5U: machine->cpu.hl = (machine->cpu.hl & 0xFFFFFF00U) | value; break;
    case 7U: machine->cpu.af = (machine->cpu.af & 0x0000FFU) | ((uint32_t)value << 8U); break;
    default: break;
    }
}

static uint8_t ce_cpu_parity(uint8_t value)
{
    value ^= (uint8_t)(value >> 4U);
    value ^= (uint8_t)(value >> 2U);
    value ^= (uint8_t)(value >> 1U);
    return (uint8_t)((uint8_t)(~value) & 1U);
}

static uint8_t ce_cpu_cb_rotate(unsigned int operation,
                                uint8_t value,
                                uint8_t carry,
                                uint8_t *result)
{
    uint8_t next_carry;
    switch (operation) {
    case 0U: next_carry = (uint8_t)(value >> 7U); *result = (uint8_t)((value << 1U) | next_carry); break;
    case 1U: next_carry = value & 1U; *result = (uint8_t)((value >> 1U) | (next_carry << 7U)); break;
    case 2U: next_carry = (uint8_t)(value >> 7U); *result = (uint8_t)((value << 1U) | carry); break;
    case 3U: next_carry = value & 1U; *result = (uint8_t)((value >> 1U) | (carry << 7U)); break;
    case 4U: next_carry = (uint8_t)(value >> 7U); *result = (uint8_t)(value << 1U); break;
    case 5U: next_carry = value & 1U; *result = (uint8_t)((value >> 1U) | (value & 0x80U)); break;
    default: next_carry = value & 1U; *result = (uint8_t)(value >> 1U); break;
    }
    return (uint8_t)(next_carry | (*result & 0xA8U) |
                     (*result == 0U ? 0x40U : 0U) |
                     (ce_cpu_parity(*result) != 0U ? 0x04U : 0U));
}

int ce_machine_init(ce_machine *machine,
                    const ce_machine_config *config,
                    ce_rom *rom)
{
    if ((machine == NULL) || (config == NULL) || (rom == NULL) ||
        (rom->bytes == NULL) || (rom->size != CE_FLASH_SIZE_BYTES) ||
        (config->revision == CE_REVISION_UNKNOWN)) {
        return -1;
    }

    ce_machine_clear(machine);
    machine->ram = calloc(CE_RAM_SIZE_BYTES, 1U);
    machine->vram = calloc(CE_VRAM_SIZE_BYTES, 1U);
    machine->framebuffer = calloc(CE_FRAMEBUFFER_WIDTH * CE_FRAMEBUFFER_HEIGHT,
                                  sizeof(*machine->framebuffer));
    if ((machine->ram == NULL) || (machine->vram == NULL) ||
        (machine->framebuffer == NULL)) {
        ce_machine_destroy(machine);
        return -1;
    }

    machine->rom = *rom;
    rom->bytes = NULL;
    rom->size = 0U;
    machine->revision = config->revision;
    machine->initial_adl_mode = config->initial_adl_mode;
    machine->privileged_end = 0x020000U;
    ce_machine_reset(machine);
    return 0;
}

void ce_machine_destroy(ce_machine *machine)
{
    if (machine == NULL) {
        return;
    }

    ce_rom_destroy(&machine->rom);
    free(machine->ram);
    free(machine->vram);
    free(machine->framebuffer);
    ce_machine_clear(machine);
}

void ce_machine_reset(ce_machine *machine)
{
    if (machine == NULL) {
        return;
    }

    memset(&machine->cpu, 0, sizeof(machine->cpu));
    machine->cpu.pc = 0U;
    machine->cpu.mbase = 0U;
    machine->cpu.adl_mode = machine->initial_adl_mode;
    machine->cpu.data_adl_mode = machine->initial_adl_mode;
    machine->cpu.control_adl_mode = machine->initial_adl_mode;
    machine->nmi_pending = 0U;
    machine->flash_write_attempted = 0U;
    machine->paused = 0U;
    memset(machine->ram, 0, CE_RAM_SIZE_BYTES);
    memset(machine->vram, 0, CE_VRAM_SIZE_BYTES);
    memset(machine->framebuffer, 0,
           CE_FRAMEBUFFER_WIDTH * CE_FRAMEBUFFER_HEIGHT *
               sizeof(*machine->framebuffer));
}

void ce_machine_pause(ce_machine *machine)
{
    if (machine != NULL) {
        machine->paused = 1U;
    }
}

void ce_machine_resume(ce_machine *machine)
{
    if (machine != NULL) {
        machine->paused = 0U;
    }
}

int ce_machine_is_paused(const ce_machine *machine)
{
    return (machine != NULL) && (machine->paused != 0U);
}

const ce_cpu_state *ce_machine_cpu_state(const ce_machine *machine)
{
    return machine == NULL ? NULL : &machine->cpu;
}

const uint16_t *ce_machine_framebuffer(const ce_machine *machine)
{
    return machine == NULL ? NULL : machine->framebuffer;
}

int ce_machine_fetch_opcode(ce_machine *machine, ce_fetch_result *result)
{
    uint32_t address;

    if ((machine == NULL) || (result == NULL) || (machine->rom.bytes == NULL) ||
        (machine->cpu.halted != 0U)) {
        return -1;
    }

    if (machine->cpu.control_adl_mode != 0U) {
        address = machine->cpu.pc & 0xFFFFFFU;
    } else {
        address = (machine->cpu.mbase << 16U) | (machine->cpu.pc & 0xFFFFU);
    }
    if (ce_machine_read_flash(machine, machine->cpu.pc, &result->opcode) != 0) {
        return -1;
    }

    result->address = address;
    result->cycles = 1U;
    machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFU;
    machine->cpu.cycles += result->cycles;
    return 0;
}

ce_step_status ce_machine_step(ce_machine *machine, ce_step_result *result)
{
    ce_fetch_result fetch;
    uint8_t operand;
    uint32_t target;
    uint8_t ei_block = 0U;

    if ((machine == NULL) || (result == NULL)) {
        return CE_STEP_ERROR;
    }
    memset(result, 0, sizeof(*result));
    if (machine->paused != 0U) {
        result->status = CE_STEP_HALTED;
        return result->status;
    }
    if (machine->cpu.ei_delay != 0U) {
        ei_block = 1U;
        machine->cpu.ei_delay = 0U;
        machine->cpu.iff1 = 1U;
        machine->cpu.iff2 = 1U;
    }
    if ((ei_block == 0U) && (machine->cpu.iff1 != 0U) &&
        (ce_machine_interrupt_masked_status(machine) != 0U)) {
        if (ce_machine_accept_interrupt(machine, result) != 0) {
            return result->status;
        }
        return result->status;
    }
    if (machine->cpu.halted != 0U) {
        result->status = CE_STEP_HALTED;
        return result->status;
    }
    if (ce_machine_fetch_opcode(machine, &fetch) != 0) {
        result->status = CE_STEP_ERROR;
        return result->status;
    }

    result->address = fetch.address;
    result->opcode = fetch.opcode;
    result->cycles = fetch.cycles;

    switch (fetch.opcode) {
    case 0x00U: /* NOP */
        break;
    case 0x76U: /* HALT */
        machine->cpu.halted = 1U;
        break;
    case 0x02U: /* LD (BC),A */
        if (ce_machine_write_data(machine, machine->cpu.bc,
                                  (uint8_t)(machine->cpu.af >> 8U)) != 0) {
            if (machine->nmi_pending != 0U) result->status = CE_STEP_ERROR;
            else result->status = CE_STEP_FLASH_UNIMPLEMENTED;
            return result->status;
        }
        break;
    case 0xF3U: /* DI */
        machine->cpu.iff1 = 0U;
        machine->cpu.iff2 = 0U;
        machine->cpu.ei_delay = 0U;
        break;
    case 0xFBU: /* EI */
        machine->cpu.ei_delay = 1U;
        break;
    case 0x5BU: /* LD E,E */
        break;
    case 0x40U: /* LD B,B */
        break;
    case 0x47U: /* LD B,A */
        machine->cpu.bc = (machine->cpu.bc & 0xFFFF00FFU) |
                          ((machine->cpu.af >> 8U) << 8U);
        break;
    case 0x4FU: /* LD C,A */
        machine->cpu.bc = (machine->cpu.bc & 0xFFFFFF00U) |
                          (machine->cpu.af >> 8U);
        break;
    case 0x46U: /* LD B,(HL) */
    {
        uint8_t value;
        if (ce_machine_read_hl(machine, 0U, &value) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.bc = (machine->cpu.bc & 0xFFFF00FFU) |
                          ((uint32_t)value << 8U);
        break;
    }
    case 0x4EU: /* LD C,(HL) */
    {
        uint8_t value;
        if (ce_machine_read_hl(machine, 0U, &value) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.bc = (machine->cpu.bc & 0xFFFFFF00U) | value;
        break;
    }
    case 0x78U: /* LD A,B */
        machine->cpu.af = (machine->cpu.af & 0x00FFU) |
                          (((machine->cpu.bc >> 8U) & 0xFFU) << 8U);
        break;
    case 0x79U: /* LD A,C */
        machine->cpu.af = (machine->cpu.af & 0x00FFU) |
                          ((machine->cpu.bc & 0xFFU) << 8U);
        break;
    case 0x7EU: /* LD A,(HL) */
    {
        uint8_t value;
        if (ce_machine_read_hl(machine, 0U, &value) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.af = (machine->cpu.af & 0x00FFU) |
                          ((uint32_t)value << 8U);
        break;
    }
    case 0xB8U: /* CP B */
        machine->cpu.af = (machine->cpu.af & 0xFF00U) |
                          ce_cpu_cp_flags((uint8_t)(machine->cpu.af >> 8U),
                                          (uint8_t)(machine->cpu.bc >> 8U),
                                          0U);
        break;
    case 0xB9U: /* CP C */
        machine->cpu.af = (machine->cpu.af & 0xFF00U) |
                          ce_cpu_cp_flags((uint8_t)(machine->cpu.af >> 8U),
                                          (uint8_t)machine->cpu.bc, 0U);
        break;
    case 0xBAU: /* CP D */
        machine->cpu.af = (machine->cpu.af & 0xFF00U) |
                          ce_cpu_cp_flags((uint8_t)(machine->cpu.af >> 8U),
                                          (uint8_t)(machine->cpu.de >> 8U), 0U);
        break;
    case 0xBBU: /* CP E */
        machine->cpu.af = (machine->cpu.af & 0xFF00U) |
                          ce_cpu_cp_flags((uint8_t)(machine->cpu.af >> 8U),
                                          (uint8_t)machine->cpu.de, 0U);
        break;
    case 0x52U: /* LD D,D */
        break;
    case 0x23U: /* INC HL */
        machine->cpu.hl = (machine->cpu.hl + 1U) &
                          (machine->cpu.data_adl_mode != 0U ?
                           0xFFFFFFU : 0xFFFFU);
        break;
    case 0x09U: /* ADD HL,BC */
    {
        unsigned int width = machine->cpu.data_adl_mode != 0U ? 24U : 16U;
        uint32_t mask = machine->cpu.data_adl_mode != 0U ? 0xFFFFFFU : 0xFFFFU;
        uint32_t left = machine->cpu.hl & mask;
        uint32_t right = machine->cpu.bc & mask;
        uint32_t sum = left + right;
        uint8_t flags = (uint8_t)(machine->cpu.af & 0xC4U);
        if (((left & 0x0FFFU) + (right & 0x0FFFU)) > 0x0FFFU) flags |= 0x10U;
        if ((sum >> width) != 0U) flags |= 0x01U;
        machine->cpu.hl = sum & mask;
        machine->cpu.af = (machine->cpu.af & 0xFF00U) | flags;
        break;
    }
    case 0x3BU: /* DEC SP */
        machine->cpu.sp = (machine->cpu.sp - 1U) &
                          (machine->cpu.control_adl_mode != 0U ?
                           0xFFFFFFU : 0xFFFFU);
        break;
    case 0x3CU: /* INC A */
    {
        uint8_t old = (uint8_t)(machine->cpu.af >> 8U);
        uint8_t value = (uint8_t)(old + 1U);
        uint8_t flags = (uint8_t)(machine->cpu.af & 0x01U);
        if ((value & 0x80U) != 0U) flags |= 0x80U;
        if (value == 0U) flags |= 0x40U;
        if ((old & 0x0FU) == 0x0FU) flags |= 0x10U;
        if (old == 0x7FU) flags |= 0x04U;
        machine->cpu.af = ((uint32_t)value << 8U) | flags;
        break;
    }
    case 0x0CU: /* INC C */
    {
        uint8_t old = (uint8_t)machine->cpu.bc;
        uint8_t value = (uint8_t)(old + 1U);
        uint8_t flags = (uint8_t)(machine->cpu.af & 0x01U);
        if ((value & 0x80U) != 0U) flags |= 0x80U;
        if (value == 0U) flags |= 0x40U;
        if ((old & 0x0FU) == 0x0FU) flags |= 0x10U;
        if (old == 0x7FU) flags |= 0x04U;
        machine->cpu.bc = (machine->cpu.bc & 0xFFFFFF00U) | value;
        machine->cpu.af = (machine->cpu.af & 0xFF00U) | flags;
        break;
    }
    case 0x06U: /* LD B,n */
    case 0x0EU: /* LD C,n */
    case 0x26U: /* LD H,n */
        if (ce_machine_read_flash(machine, machine->cpu.pc, &operand) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        if (fetch.opcode == 0x06U) {
            machine->cpu.bc = (machine->cpu.bc & 0xFFFF00FFU) |
                              ((uint32_t)operand << 8U);
        } else if (fetch.opcode == 0x0EU) {
            machine->cpu.bc = (machine->cpu.bc & 0xFFFFFF00U) | operand;
        } else {
            machine->cpu.hl = (machine->cpu.hl & 0xFFFF00FFU) |
                              ((uint32_t)operand << 8U);
        }
        machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFU;
        result->cycles++;
        machine->cpu.cycles++;
        break;
    case 0xAFU: /* XOR A */
        machine->cpu.af = 0x0044U;
        break;
    case 0xB7U: /* OR A */
    {
        uint8_t value = (uint8_t)(machine->cpu.af >> 8U);
        uint8_t flags = (uint8_t)(value & 0xA8U);
        if (value == 0U) flags |= 0x40U;
        if (ce_cpu_parity(value) != 0U) flags |= 0x04U;
        machine->cpu.af = ((uint32_t)value << 8U) | flags;
        break;
    }
    case 0x3FU: /* CCF */
    {
        uint8_t old_flags = (uint8_t)machine->cpu.af;
        uint8_t flags = (uint8_t)(old_flags & 0xC4U);
        if ((old_flags & 0x01U) != 0U) flags |= 0x10U;
        if ((old_flags & 0x01U) == 0U) flags |= 0x01U;
        flags |= (uint8_t)(machine->cpu.af >> 8U) & 0x28U;
        machine->cpu.af = (machine->cpu.af & 0xFF00U) | flags;
        break;
    }
    case 0x37U: /* SCF */
        machine->cpu.af = (machine->cpu.af & 0xFF00U) |
                          ((uint8_t)machine->cpu.af & 0xC4U) | 0x01U;
        break;
    case 0xEBU:
    {
        uint32_t value = machine->cpu.de;
        machine->cpu.de = machine->cpu.hl;
        machine->cpu.hl = value;
        break;
    }
    case 0x01U: /* LD BC,nn */
        {
            uint8_t low;
            uint8_t high;
            if (ce_machine_read_flash(machine, machine->cpu.pc, &low) != 0 ||
                ce_machine_read_flash(machine, machine->cpu.pc + 1U, &high) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            machine->cpu.bc = (uint32_t)low | ((uint32_t)high << 8U);
            machine->cpu.pc = (machine->cpu.pc + 2U) & 0xFFFFFFU;
            result->cycles += 2U;
            machine->cpu.cycles += 2U;
        }
        break;
    case 0x11U: /* LD DE,nn */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.de = target;
        result->cycles += ce_machine_control_width(machine);
        machine->cpu.cycles += ce_machine_control_width(machine);
        break;
    case 0x21U: /* LD HL,nn */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.hl = target;
        result->cycles += ce_machine_control_width(machine);
        machine->cpu.cycles += ce_machine_control_width(machine);
        break;
    case 0x2AU: /* LD HL,(nn) */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        {
            uint32_t value = 0U;
            unsigned int width = ce_machine_control_width(machine);
            for (unsigned int index = 0U; index < width; ++index) {
                uint8_t byte;
                int status = machine->cpu.control_adl_mode != 0U ?
                             ce_machine_read_memory(machine, target + index, &byte) :
                             ce_machine_read_data(machine, target + index, &byte);
                if (status != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
                value |= (uint32_t)byte << (index * 8U);
            }
            machine->cpu.hl = value;
        }
        result->cycles += ce_machine_control_width(machine);
        machine->cpu.cycles += ce_machine_control_width(machine);
        break;
    case 0x31U: /* LD SP,nn */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.sp = target;
        result->cycles += ce_machine_control_width(machine);
        machine->cpu.cycles += ce_machine_control_width(machine);
        break;
    case 0x3EU: /* LD A,n */
        if (ce_machine_read_flash(machine, machine->cpu.pc, &operand) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.af = (machine->cpu.af & 0x00FFU) |
                          ((uint32_t)operand << 8U);
        machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFU;
        result->cycles++;
        machine->cpu.cycles++;
        break;
    case 0xE6U: /* AND n */
        if (ce_machine_read_flash(machine, machine->cpu.pc, &operand) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        {
            uint8_t value = (uint8_t)(machine->cpu.af >> 8U) & operand;
            uint8_t flags = (uint8_t)(value & 0xA8U) | 0x10U;
            if (value == 0U) flags |= 0x40U;
            if (ce_cpu_parity(value) != 0U) flags |= 0x04U;
            machine->cpu.af = ((uint32_t)value << 8U) | flags;
        }
        machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFFFU;
        result->cycles++;
        machine->cpu.cycles++;
        break;
    case 0x3AU: /* LD A,(nn) */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        {
            uint8_t value;
            if (ce_machine_read_data(machine, target, &value) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            machine->cpu.af = (machine->cpu.af & 0x00FFU) |
                              ((uint32_t)value << 8U);
        }
        result->cycles += ce_machine_control_width(machine);
        machine->cpu.cycles += ce_machine_control_width(machine);
        break;
    case 0x32U: /* LD (nn),A */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        if (ce_machine_write_data(machine, target,
                                  (uint8_t)(machine->cpu.af >> 8U)) != 0) {
            result->status = machine->nmi_pending != 0U ?
                              CE_STEP_ERROR : CE_STEP_FLASH_UNIMPLEMENTED;
            return result->status;
        }
        result->cycles += ce_machine_control_width(machine);
        machine->cpu.cycles += ce_machine_control_width(machine);
        break;
    case 0x22U: /* LD (nn),HL */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        {
            uint32_t address = target;
            uint32_t value = machine->cpu.hl;
            unsigned int width = machine->cpu.data_adl_mode != 0U ? 3U : 2U;
            for (unsigned int index = 0U; index < width; ++index) {
                if (ce_machine_write_data(machine, address + index,
                                          (uint8_t)(value >> (index * 8U))) !=
                    0) {
                    if (machine->nmi_pending != 0U) {
                        result->status = CE_STEP_ERROR;
                    } else {
                        result->status = CE_STEP_FLASH_UNIMPLEMENTED;
                    }
                    return result->status;
                }
            }
        }
        result->cycles += ce_machine_control_width(machine) +
                          (machine->cpu.data_adl_mode != 0U ? 3U : 2U);
        machine->cpu.cycles += result->cycles - 1U;
        break;
    case 0xD1U: /* POP DE */
        {
            uint32_t value = 0U;
            unsigned width = ce_machine_stack_width(machine);
            for (unsigned index = 0U; index < width; ++index) {
                uint8_t byte;
                if (ce_machine_read_stack(machine, machine->cpu.sp + index,
                                          &byte) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
                value |= (uint32_t)byte << (index * 8U);
            }
            machine->cpu.de = value;
            machine->cpu.sp += width;
            result->cycles += width;
            machine->cpu.cycles += width;
        }
        break;
    case 0xC5U: /* PUSH BC */
    case 0xD5U: /* PUSH DE */
    case 0xE5U: /* PUSH HL */
    case 0xF5U: /* PUSH AF */
        {
            uint32_t value = fetch.opcode == 0xC5U ? machine->cpu.bc :
                             fetch.opcode == 0xD5U ? machine->cpu.de :
                             fetch.opcode == 0xE5U ? machine->cpu.hl :
                             machine->cpu.af;
            unsigned width = ce_machine_stack_width(machine);
            uint32_t new_sp = machine->cpu.sp - width;
            for (unsigned index = 0U; index < width; ++index) {
                if (ce_machine_write_stack(machine, new_sp + index,
                                           (uint8_t)(value >> (index * 8U))) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
            }
            machine->cpu.sp = new_sp;
            result->cycles += width;
            machine->cpu.cycles += width;
        }
        break;
    case 0xC1U: /* POP BC */
    case 0xE1U: /* POP HL */
    case 0xF1U: /* POP AF */
        {
            uint32_t value = 0U;
            unsigned width = ce_machine_stack_width(machine);
            for (unsigned index = 0U; index < width; ++index) {
                uint8_t byte;
                if (ce_machine_read_stack(machine, machine->cpu.sp + index,
                                          &byte) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
                value |= (uint32_t)byte << (index * 8U);
            }
            if (fetch.opcode == 0xC1U) machine->cpu.bc = value;
            else if (fetch.opcode == 0xE1U) machine->cpu.hl = value;
            else machine->cpu.af = value;
            machine->cpu.sp += width;
            result->cycles += width;
            machine->cpu.cycles += width;
        }
        break;
    case 0xC0U: /* RET NZ */
        if ((machine->cpu.af & 0x40U) != 0U) break;
        /* fall through */
    case 0xD8U: /* RET C */
        if ((fetch.opcode == 0xD8U) && ((machine->cpu.af & 0x01U) == 0U)) break;
        /* fall through */
    case 0xC9U: /* RET */
        {
            uint32_t value = 0U;
            unsigned width = ce_machine_stack_width(machine);
            for (unsigned index = 0U; index < width; ++index) {
                uint8_t byte;
                if (ce_machine_read_stack(machine, machine->cpu.sp + index,
                                          &byte) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
                value |= (uint32_t)byte << (index * 8U);
            }
            machine->cpu.pc = value;
            machine->cpu.sp += width;
            result->cycles += width;
            machine->cpu.cycles += width;
        }
        break;
    case 0x8DU: /* ADC A,L */
        {
            uint8_t value;
            uint8_t flags = ce_cpu_adc_flags((uint8_t)(machine->cpu.af >> 8U),
                                             (uint8_t)machine->cpu.hl,
                                             (uint8_t)(machine->cpu.af & 0x01U),
                                             &value);
            machine->cpu.af = ((uint32_t)value << 8U) | flags;
        }
        break;
    case 0xFEU: /* CP n */
        if (ce_machine_read_flash(machine, machine->cpu.pc, &operand) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.af = (machine->cpu.af & 0xFF00U) |
                          ce_cpu_cp_flags((uint8_t)(machine->cpu.af >> 8U),
                                          operand, 0U);
        machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFU;
        result->cycles++;
        machine->cpu.cycles++;
        break;
    case 0xC2U: /* JP NZ,nn in reset Z80 mode */
    case 0xCAU: /* JP Z,nn */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        if (((fetch.opcode == 0xC2U) && ((machine->cpu.af & 0x40U) == 0U)) ||
            ((fetch.opcode == 0xCAU) && ((machine->cpu.af & 0x40U) != 0U))) {
            machine->cpu.pc = target;
        }
        result->cycles += ce_machine_control_width(machine);
        machine->cpu.cycles += ce_machine_control_width(machine);
        break;
    case 0xCDU: /* CALL nn */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        {
            uint32_t return_pc = machine->cpu.pc;
            unsigned width = ce_machine_stack_width(machine);
            uint32_t new_sp = machine->cpu.sp - width;
            for (unsigned index = 0U; index < width; ++index) {
                if (ce_machine_write_stack(machine, new_sp + index,
                                           (uint8_t)(return_pc >>
                                                     (index * 8U))) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
            }
            machine->cpu.sp = new_sp;
        }
        machine->cpu.pc = target;
        result->cycles += 3U;
        machine->cpu.cycles += 3U;
        break;
    case 0x18U: /* JR e */
    case 0x20U: /* JR NZ,e */
    case 0x28U: /* JR Z,e */
    case 0x30U: /* JR NC,e */
    case 0x38U: /* JR C,e */
        if (ce_machine_read_flash(machine, machine->cpu.pc, &operand) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        if ((fetch.opcode == 0x18U) ||
            ((fetch.opcode == 0x20U) && ((machine->cpu.af & 0x40U) == 0U)) ||
            ((fetch.opcode == 0x28U) && ((machine->cpu.af & 0x40U) != 0U)) ||
            ((fetch.opcode == 0x30U) && ((machine->cpu.af & 0x01U) == 0U)) ||
            ((fetch.opcode == 0x38U) && ((machine->cpu.af & 0x01U) != 0U))) {
            machine->cpu.pc = (uint32_t)((int32_t)machine->cpu.pc + 1 +
                                         (int8_t)operand) & 0xFFFFU;
        } else {
            machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFU;
        }
        result->cycles++;
        machine->cpu.cycles++;
        break;
    case 0x10U: /* DJNZ e */
        if (ce_machine_read_flash(machine, machine->cpu.pc, &operand) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        {
            uint8_t b = (uint8_t)((uint16_t)(uint8_t)(machine->cpu.bc >> 8U) -
                                  1U);
            machine->cpu.bc = (machine->cpu.bc & 0xFFFF00FFU) |
                              ((uint32_t)b << 8U);
            machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFU;
            result->cycles++;
            machine->cpu.cycles++;
            if (b != 0U) {
                machine->cpu.pc = (uint32_t)((int32_t)machine->cpu.pc +
                                             (int8_t)operand) & 0xFFFFU;
                result->cycles++;
                machine->cpu.cycles++;
            }
        }
        break;
    case 0xEDU:
        if (ce_machine_read_flash(machine, machine->cpu.pc, &operand) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFU;
        result->cycles++;
        machine->cpu.cycles++;
        if (operand == 0x7EU) { /* RSMIX */
            machine->cpu.data_adl_mode = 0U;
            machine->cpu.control_adl_mode = 1U;
            machine->cpu.adl_mode = 1U;
        } else if (operand == 0x39U) { /* OUT0 (n),A */
            if (ce_machine_read_flash(machine, machine->cpu.pc, &operand) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            ce_machine_write_io_port(machine, operand,
                                     (uint8_t)(machine->cpu.af >> 8U));
            machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFU;
            result->cycles++;
            machine->cpu.cycles++;
        } else if (operand == 0x79U) { /* OUT0 (C),A */
            ce_machine_write_io_port(machine,
                                     (uint16_t)(machine->cpu.bc & 0x00FFU),
                                     (uint8_t)(machine->cpu.af >> 8U));
        } else if (operand == 0x38U) { /* IN0 A,(n) */
            if (ce_machine_read_flash(machine, machine->cpu.pc, &operand) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            {
                uint8_t value = machine->io_ports[operand];
                if (operand == 0x28U) {
                    value = (uint8_t)((value & 0x04U) |
                                      (machine->flash_unlocked != 0U ? 0x08U : 0U));
                }
                machine->cpu.af = ((uint32_t)value << 8U) |
                                  (ce_cpu_cp_flags(value, 0U, 0U) & 0xFEU);
            }
            machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFU;
            result->cycles++;
            machine->cpu.cycles++;
        } else if (operand == 0xEAU) { /* IND */
            uint8_t value;
            if (ce_machine_read_data(machine, machine->cpu.hl, &value) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            ce_machine_write_io_port(machine,
                                     machine->cpu.bc & 0xFFFFU, value);
            machine->cpu.hl = (machine->cpu.hl - 1U) &
                              (machine->cpu.data_adl_mode != 0U ?
                               0xFFFFFFU : 0xFFFFU);
            {
                uint8_t b = (uint8_t)((uint8_t)(machine->cpu.bc >> 8U) - 1U);
                uint8_t flags = (b == 0U ? 0x40U : 0U) |
                                ((value & 0x80U) != 0U ? 0x02U : 0U);
                machine->cpu.bc = (machine->cpu.bc & 0xFFFF00FFU) |
                                  ((uint32_t)b << 8U);
                machine->cpu.af = (machine->cpu.af & 0xFF00U) | flags;
            }
            result->cycles += 3U;
            machine->cpu.cycles += 3U;
        } else if ((operand == 0x42U) || (operand == 0x52U)) { /* SBC HL,BC/DE */
            unsigned int width = machine->cpu.control_adl_mode != 0U ? 24U : 16U;
            uint32_t mask = machine->cpu.control_adl_mode != 0U ? 0xFFFFFFU : 0xFFFFU;
            uint32_t left = machine->cpu.hl & mask;
            uint32_t right = (operand == 0x42U ? machine->cpu.bc :
                              machine->cpu.de) & mask;
            uint32_t carry = (machine->cpu.af & 0x01U) != 0U ? 1U : 0U;
            uint32_t difference = (left - right - carry) & mask;
            uint8_t flags = 0x02U;
            if ((difference & ((uint32_t)1U << (width - 1U))) != 0U) {
                flags |= 0x80U;
            }
            if (difference == 0U) {
                flags |= 0x40U;
            }
            if ((left & 0x0FFFU) < ((right & 0x0FFFU) + carry)) {
                flags |= 0x10U;
            }
            if (left < right + carry) {
                flags |= 0x01U;
            }
            machine->cpu.hl = difference;
            machine->cpu.af = (machine->cpu.af & 0xFF00U) | flags;
        } else if (operand == 0x6DU) { /* LD MB,A */
            if (machine->cpu.adl_mode != 0U) {
                machine->cpu.mbase = (machine->cpu.af >> 8U) & 0xFFU;
            }
        } else if (operand == 0x56U) { /* IM 1 */
            machine->cpu.interrupt_mode = 1U;
        } else if (operand == 0x73U) { /* LD (nn),SP */
            uint32_t address;
            unsigned int width = ce_machine_stack_width(machine);
            if (ce_machine_read_instruction_value(machine, &address) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            for (unsigned int index = 0U; index < width; ++index) {
                if (ce_machine_write_memory(machine, address + index,
                                            (uint8_t)(machine->cpu.sp >> (index * 8U))) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
            }
            result->cycles += ce_machine_control_width(machine) + width;
            machine->cpu.cycles += ce_machine_control_width(machine) + width;
        } else if (operand == 0x7BU) { /* LD SP,(nn) */
            uint32_t address;
            uint32_t value = 0U;
            unsigned int width = ce_machine_stack_width(machine);
            if (ce_machine_read_instruction_value(machine, &address) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            for (unsigned int index = 0U; index < width; ++index) {
                uint8_t byte;
                if (ce_machine_read_memory(machine, address + index, &byte) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
                value |= (uint32_t)byte << (index * 8U);
            }
            machine->cpu.sp = value;
            result->cycles += ce_machine_control_width(machine) + width;
            machine->cpu.cycles += ce_machine_control_width(machine) + width;
        } else if (operand == 0x2AU) { /* LD HL,(nn) */
            uint32_t address;
            uint32_t value = 0U;
            unsigned int width = machine->cpu.data_adl_mode != 0U ? 3U : 3U;
            if (ce_machine_read_instruction_value(machine, &address) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            for (unsigned int index = 0U; index < width; ++index) {
                uint8_t byte;
                if (ce_machine_read_memory(machine, address + index, &byte) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
                value |= (uint32_t)byte << (index * 8U);
            }
            machine->cpu.hl = value;
            result->cycles += ce_machine_control_width(machine) + width;
            machine->cpu.cycles += ce_machine_control_width(machine) + width;
        } else if (operand == 0x07U) { /* LD BC,(HL) */
            uint32_t value = 0U;
            unsigned int width = machine->cpu.data_adl_mode != 0U ? 3U : 2U;
            for (unsigned int index = 0U; index < width; ++index) {
                uint8_t byte;
                if (ce_machine_read_data(machine, machine->cpu.hl + index,
                                         &byte) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
                value |= (uint32_t)byte << (index * 8U);
            }
            machine->cpu.bc = value;
            result->cycles += width;
            machine->cpu.cycles += width;
        } else if (operand == 0x27U) { /* LD HL,(HL) */
            uint32_t value = 0U;
            unsigned int width = machine->cpu.data_adl_mode != 0U ? 3U : 2U;
            for (unsigned int index = 0U; index < width; ++index) {
                uint8_t byte;
                if (ce_machine_read_data(machine, machine->cpu.hl + index,
                                         &byte) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
                value |= (uint32_t)byte << (index * 8U);
            }
            machine->cpu.hl = value;
            result->cycles += width;
            machine->cpu.cycles += width;
        } else if (operand == 0x0FU) { /* LD (HL),BC */
            unsigned int width = machine->cpu.control_adl_mode != 0U ? 3U : 2U;
            for (unsigned int index = 0U; index < width; ++index) {
                if (ce_machine_write_memory(machine, machine->cpu.hl + index,
                                            (uint8_t)(machine->cpu.bc >> (index * 8U))) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
            }
            result->cycles += width;
            machine->cpu.cycles += width;
        } else if (operand == 0x93U) { /* OTIMR */
            uint8_t count = (uint8_t)(machine->cpu.bc >> 8U);
            unsigned int iterations = count == 0U ? 256U : count;
            uint8_t transferred = 0U;
            uint8_t last_old_b = count;
            for (unsigned int index = 0U; index < iterations; ++index) {
                uint8_t current_b = (uint8_t)(machine->cpu.bc >> 8U);
                uint8_t current_c = (uint8_t)machine->cpu.bc;
                last_old_b = current_b;
                if (ce_machine_read_data(machine, machine->cpu.hl,
                                         &transferred) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
                machine->last_io_port = (uint16_t)(machine->cpu.bc & 0x00FFU);
                machine->last_io_value = transferred;
                machine->io_write_count++;
                machine->cpu.hl = (machine->cpu.hl + 1U) &
                                  (machine->cpu.data_adl_mode != 0U ?
                                   0xFFFFFFU : 0xFFFFU);
                current_b--;
                current_c++;
                machine->cpu.bc = (machine->cpu.bc & 0xFFFF0000U) |
                                  (uint32_t)current_c |
                                  ((uint32_t)current_b << 8U);
            }
            {
                uint8_t final_b = (uint8_t)(machine->cpu.bc >> 8U);
                uint8_t flags = (uint8_t)(machine->cpu.af & 0x01U);
                if ((final_b & 0x80U) != 0U) {
                    flags |= 0x80U;
                }
                if (final_b == 0U) {
                    flags |= 0x40U;
                }
                if ((last_old_b & 0x0FU) == 0U) {
                    flags |= 0x10U;
                }
                if ((transferred & 0x80U) != 0U) {
                    flags |= 0x02U;
                }
                machine->cpu.af = (machine->cpu.af & 0xFF00U) | flags;
            }
            result->cycles += 3U * iterations;
            machine->cpu.cycles += 3U * iterations;
        } else {
            result->status = CE_STEP_UNIMPLEMENTED;
            return result->status;
        }
        break;
    case 0xDDU:
    {
        uint8_t prefixed_opcode;
        unsigned int width = ce_machine_control_width(machine);
        if (ce_machine_read_flash(machine, machine->cpu.pc, &prefixed_opcode) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFFFU;
        result->cycles++;
        machine->cpu.cycles++;
        if (prefixed_opcode == 0xCBU) {
            uint8_t displacement;
            uint8_t indexed_opcode;
            uint32_t address;
            uint8_t value;
            unsigned int operation;
            if (ce_machine_read_flash(machine, machine->cpu.pc, &displacement) != 0 ||
                ce_machine_read_flash(machine, machine->cpu.pc + 1U,
                                      &indexed_opcode) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            machine->cpu.pc = (machine->cpu.pc + 2U) & 0xFFFFFFU;
            result->cycles += 2U;
            machine->cpu.cycles += 2U;
            address = (uint32_t)((int32_t)machine->cpu.ix + (int8_t)displacement) &
                      0xFFFFFFU;
            if (ce_machine_read_memory(machine, address, &value) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            operation = (indexed_opcode >> 3U) & 0x07U;
            if ((indexed_opcode & 0xC0U) == 0x00U) {
                uint8_t rotated;
                uint8_t flags = ce_cpu_cb_rotate(operation, value,
                                                  (uint8_t)(machine->cpu.af & 0x01U),
                                                  &rotated);
                value = rotated;
                machine->cpu.af = (machine->cpu.af & 0xFF00U) | flags;
                if (ce_machine_write_memory(machine, address, value) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
            } else if ((indexed_opcode & 0xC0U) == 0x40U) {
                uint8_t mask = (uint8_t)(1U << operation);
                uint8_t flags = (uint8_t)((machine->cpu.af & 0x01U) | 0x10U);
                flags |= (uint8_t)(value & 0x28U);
                if ((value & mask) == 0U) flags |= 0x44U;
                if ((operation == 7U) && ((value & 0x80U) != 0U)) flags |= 0x80U;
                machine->cpu.af = (machine->cpu.af & 0xFF00U) | flags;
            } else {
                uint8_t mask = (uint8_t)(1U << operation);
                value = (indexed_opcode & 0xC0U) == 0x80U ?
                        (uint8_t)(value & (uint8_t)~mask) :
                        (uint8_t)(value | mask);
                if (ce_machine_write_memory(machine, address, value) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
            }
            result->cycles += 2U;
            machine->cpu.cycles += 2U;
        } else if (prefixed_opcode == 0xE5U) {
            uint32_t new_sp = machine->cpu.sp - width;
            for (unsigned int index = 0U; index < width; ++index) {
                if (ce_machine_write_stack(machine, new_sp + index,
                                           (uint8_t)(machine->cpu.ix >> (index * 8U))) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
            }
            machine->cpu.sp = new_sp;
        } else if ((prefixed_opcode >= 0x70U) &&
                   (prefixed_opcode <= 0x77U) &&
                   (prefixed_opcode != 0x76U)) {
            uint8_t displacement;
            uint8_t value;
            if (ce_machine_read_flash(machine, machine->cpu.pc, &displacement) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFFFU;
            switch (prefixed_opcode & 0x07U) {
            case 0U: value = (uint8_t)(machine->cpu.bc >> 8U); break;
            case 1U: value = (uint8_t)machine->cpu.bc; break;
            case 2U: value = (uint8_t)(machine->cpu.de >> 8U); break;
            case 3U: value = (uint8_t)machine->cpu.de; break;
            case 4U: value = (uint8_t)(machine->cpu.ix >> 8U); break;
            case 5U: value = (uint8_t)machine->cpu.ix; break;
            default: value = (uint8_t)(machine->cpu.af >> 8U); break;
            }
            if (ce_machine_write_memory(machine,
                                        (uint32_t)((int32_t)machine->cpu.ix +
                                                   (int8_t)displacement) & 0xFFFFFFU,
                                        value) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            result->cycles += 2U;
            machine->cpu.cycles += 2U;
        } else if (prefixed_opcode == 0x21U) {
            if (ce_machine_read_instruction_value(machine, &target) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
            machine->cpu.ix = target;
            result->cycles += width;
            machine->cpu.cycles += width;
        } else if (prefixed_opcode == 0x39U) {
            machine->cpu.ix = (machine->cpu.ix + machine->cpu.sp) &
                              (width == 3U ? 0xFFFFFFU : 0xFFFFU);
        } else if (prefixed_opcode == 0x09U) {
            machine->cpu.ix = (machine->cpu.ix + machine->cpu.bc) &
                              (width == 3U ? 0xFFFFFFU : 0xFFFFU);
        } else if (prefixed_opcode == 0x27U) {
            ce_cpu_daa(machine);
        } else if (prefixed_opcode == 0x2FU) {
            uint8_t value = (uint8_t)(machine->cpu.af >> 8U);
            machine->cpu.af = ((uint32_t)(uint8_t)~value << 8U) |
                              ((uint8_t)machine->cpu.af | 0x12U);
        } else if (prefixed_opcode == 0x07U) {
            uint8_t value = (uint8_t)(machine->cpu.af >> 8U);
            uint8_t rotated = (uint8_t)((value << 1U) | (value >> 7U));
            uint8_t flags = (uint8_t)machine->cpu.af & 0xC4U;
            flags |= (uint8_t)(value >> 7U);
            machine->cpu.af = ((uint32_t)rotated << 8U) | flags;
        } else if (prefixed_opcode == 0xF9U) {
            machine->cpu.sp = machine->cpu.ix;
        } else if (prefixed_opcode == 0xE1U) {
            uint32_t value = 0U;
            for (unsigned int index = 0U; index < width; ++index) {
                uint8_t byte;
                if (ce_machine_read_stack(machine, machine->cpu.sp + index,
                                          &byte) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
                value |= (uint32_t)byte << (index * 8U);
            }
            machine->cpu.ix = value;
            machine->cpu.sp += width;
        } else {
            result->status = CE_STEP_UNIMPLEMENTED;
            return result->status;
        }
        result->cycles += width;
        machine->cpu.cycles += width;
        break;
    }
    case 0xCBU:
    {
        uint8_t cb_opcode;
        unsigned int operation;
        unsigned int index;
        uint8_t value;
        uint8_t flags;
        uint8_t carry = (uint8_t)(machine->cpu.af & 0x01U);
        int memory = 0;

        if (ce_machine_read_flash(machine, machine->cpu.pc, &cb_opcode) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.pc = (machine->cpu.pc + 1U) & 0xFFFFFFU;
        result->cycles++;
        machine->cpu.cycles++;
        operation = (cb_opcode >> 3U) & 0x07U;
        index = cb_opcode & 0x07U;
        if (index == 6U) {
            memory = 1;
            if (ce_machine_read_data(machine, machine->cpu.hl, &value) != 0) {
                result->status = CE_STEP_ERROR;
                return result->status;
            }
        } else {
            value = ce_cpu_cb_read_register(machine, index);
        }

        if ((cb_opcode & 0xC0U) == 0x00U) {
            uint8_t rotated;
            flags = ce_cpu_cb_rotate(operation, value, carry, &rotated);
            value = rotated;
            machine->cpu.af = (machine->cpu.af & 0xFF00U) | flags;
            if (memory) {
                if (ce_machine_write_data(machine, machine->cpu.hl, value) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
            } else {
                ce_cpu_cb_write_register(machine, index, value);
            }
        } else if ((cb_opcode & 0xC0U) == 0x40U) {
            uint8_t mask = (uint8_t)(1U << operation);
            flags = (uint8_t)((machine->cpu.af & 0x01U) | 0x10U);
            flags |= (uint8_t)(value & 0x28U);
            if ((value & mask) == 0U) flags |= 0x44U;
            if ((operation == 7U) && ((value & 0x80U) != 0U)) flags |= 0x80U;
            machine->cpu.af = (machine->cpu.af & 0xFF00U) | flags;
        } else {
            uint8_t mask = (uint8_t)(1U << operation);
            value = (cb_opcode & 0xC0U) == 0x80U ?
                    (uint8_t)(value & (uint8_t)~mask) :
                    (uint8_t)(value | mask);
            if (memory) {
                if (ce_machine_write_data(machine, machine->cpu.hl, value) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
            } else {
                ce_cpu_cb_write_register(machine, index, value);
            }
        }
        if ((cb_opcode == 0x57U) && ((machine->io_ports[0x06U] & 0x04U) != 0U)) {
            machine->flash_unlocked = 1U;
        }
        if (memory) {
            result->cycles += 2U;
            machine->cpu.cycles += 2U;
        }
        break;
    }
    case 0xC3U: /* JP nn in reset Z80 mode */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        machine->cpu.pc = target;
        result->cycles += ce_machine_control_width(machine);
        machine->cpu.cycles += ce_machine_control_width(machine);
        break;
    case 0xD2U: /* JP NC,nn */
        if (ce_machine_read_instruction_value(machine, &target) != 0) {
            result->status = CE_STEP_ERROR;
            return result->status;
        }
        if ((machine->cpu.af & 0x01U) == 0U) machine->cpu.pc = target;
        result->cycles += ce_machine_control_width(machine);
        machine->cpu.cycles += ce_machine_control_width(machine);
        break;
    default:
        if ((fetch.opcode & 0xC7U) == 0xC7U) { /* RST n */
            uint32_t new_sp = machine->cpu.sp -
                              ce_machine_control_width(machine);
            uint32_t return_pc = machine->cpu.pc;
            unsigned width = ce_machine_control_width(machine);
            for (unsigned index = 0U; index < width; ++index) {
                if (ce_machine_write_stack(machine, new_sp + index,
                                            (uint8_t)(return_pc >>
                                                      (index * 8U))) != 0) {
                    result->status = CE_STEP_ERROR;
                    return result->status;
                }
            }
            machine->cpu.sp = new_sp;
            machine->cpu.pc = fetch.opcode & 0x38U;
            result->cycles++;
            machine->cpu.cycles++;
            break;
        }
        result->status = CE_STEP_UNIMPLEMENTED;
        return result->status;
    }

    result->status = CE_STEP_OK;
    return result->status;
}