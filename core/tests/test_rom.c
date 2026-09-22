#include "ce/rom.h"
#include "ce/machine.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_revision_is_unknown_without_reliable_metadata(void)
{
    ce_rom rom = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);
    ce_revision_identification identification;

    assert(bytes != NULL);
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);

    identification = ce_rom_identify_revision(&rom);
    assert(identification.revision == CE_REVISION_UNKNOWN);
    assert(identification.source == CE_REVISION_SOURCE_NONE);
    assert(identification.confidence_percent == 0U);

    ce_rom_destroy(&rom);
    assert(rom.bytes == NULL);
    assert(rom.size == 0U);
}

static void test_rom_loader_rejects_wrong_size(void)
{
    const char *path = "ce-test-short-rom.bin";
    FILE *file = fopen(path, "wb");
    ce_rom rom = {0};

    assert(file != NULL);
    assert(fputc(0, file) != EOF);
    assert(fclose(file) == 0);
    assert(ce_rom_load_file(path, &rom) != 0);
    assert(rom.bytes == NULL);
    assert(rom.size == 0U);
    assert(remove(path) == 0);
}

static void test_revision_names(void)
{
    assert(strcmp(ce_revision_name(CE_REVISION_UNKNOWN), "unknown") == 0);
    assert(strcmp(ce_revision_name(CE_REVISION_PRE_A), "pre-A") == 0);
    assert(strcmp(ce_revision_name(CE_REVISION_I), "I") == 0);
    assert(strcmp(ce_revision_name(CE_REVISION_M), "M") == 0);
}

static void test_machine_requires_explicit_revision_and_owns_rom(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_UNKNOWN};
    ce_rom rom = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) != 0);
    assert(rom.bytes == bytes);

    config.revision = CE_REVISION_I;
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    assert(rom.bytes == NULL);
    assert(ce_machine_cpu_state(&machine)->pc == 0U);
    assert(ce_machine_cpu_state(&machine)->adl_mode == 0U);
    assert(ce_machine_framebuffer(&machine) != NULL);

    ce_machine_pause(&machine);
    assert(ce_machine_is_paused(&machine));
    ce_machine_resume(&machine);
    assert(!ce_machine_is_paused(&machine));
    ce_machine_destroy(&machine);
}

static void test_machine_fetches_from_reset_address(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_fetch_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xEDU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.data_adl_mode = 1U;
    assert(ce_machine_fetch_opcode(&machine, &result) == 0);
    assert(result.address == 0U);
    assert(result.opcode == 0xEDU);
    assert(result.cycles == 1U);
    assert(ce_machine_cpu_state(&machine)->pc == 1U);
    assert(ce_machine_cpu_state(&machine)->cycles == 1U);
    ce_machine_destroy(&machine);
}

static void test_machine_executes_reset_sequence(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xF3U;
    bytes[1] = 0xEDU;
    bytes[2] = 0x7EU;
    bytes[3] = 0x5BU;
    bytes[4] = 0xC3U;
    bytes[5] = 0x4FU;
    bytes[6] = 0x0EU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);

    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(result.opcode == 0xF3U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(result.opcode == 0xEDU);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(result.opcode == 0x5BU);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(result.opcode == 0xC3U);
    assert(ce_machine_cpu_state(&machine)->pc == 0x0E4FU);
    ce_machine_destroy(&machine);
}

static void test_xor_a_sets_zero_and_parity(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xAFU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.af == 0x0044U);
    ce_machine_destroy(&machine);
}

static void test_out0_is_observable(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x3EU;
    bytes[1] = 0xA5U;
    bytes[2] = 0xEDU;
    bytes[3] = 0x39U;
    bytes[4] = 0x12U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.last_io_port == 0x12U);
    assert(machine.last_io_value == 0xA5U);
    assert(machine.io_write_count == 1U);
    ce_machine_destroy(&machine);
}

static void test_ld_bc_hl_respects_data_mode_width(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xEDU;
    bytes[1] = 0x07U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);

    machine.cpu.control_adl_mode = 0U;
    machine.cpu.data_adl_mode = 0U;
    machine.cpu.mbase = 0xD0U;
    machine.ram[0x0000U] = 0xEDU;
    machine.ram[0x0001U] = 0x07U;
    machine.cpu.hl = 0x0100U;
    machine.ram[0x0100U] = 0xAAU;
    machine.ram[0x0101U] = 0xBBU;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.bc == 0xBBAAU);
    assert(machine.cpu.pc == 2U);
    assert(machine.cpu.cycles == 4U);

    ce_machine_reset(&machine);
    machine.cpu.control_adl_mode = 0U;
    machine.cpu.data_adl_mode = 1U;
    machine.cpu.hl = 0xD40010U;
    machine.vram[0x10U] = 0x11U;
    machine.vram[0x11U] = 0x22U;
    machine.vram[0x12U] = 0x33U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.bc == 0x332211U);
    assert(machine.cpu.pc == 2U);
    assert(machine.cpu.cycles == 5U);
    ce_machine_destroy(&machine);
}

static void test_otimr_repeats_memory_to_port(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xEDU;
    bytes[1] = 0x93U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);

    machine.cpu.control_adl_mode = 0U;
    machine.cpu.data_adl_mode = 0U;
    machine.cpu.mbase = 0xD0U;
    machine.ram[0x0000U] = 0xEDU;
    machine.ram[0x0001U] = 0x93U;
    machine.cpu.hl = 0x0200U;
    machine.cpu.bc = 0x0305U;
    machine.ram[0x0200U] = 0x10U;
    machine.ram[0x0201U] = 0x20U;
    machine.ram[0x0202U] = 0x80U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.hl == 0x0203U);
    assert(machine.cpu.bc == 0x0008U);
            assert(machine.last_io_port == 0x07U);
    assert(machine.last_io_value == 0x80U);
    assert(machine.io_write_count == 3U);
    assert((machine.cpu.af & 0x40U) != 0U);
    assert((machine.cpu.af & 0x02U) != 0U);
    assert(machine.cpu.pc == 2U);
    assert(machine.cpu.cycles == 11U);
    ce_machine_destroy(&machine);
}

static void test_inc_hl_respects_data_mode(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x23U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.control_adl_mode = 0U;
    machine.cpu.data_adl_mode = 0U;
    machine.cpu.hl = 0xFFFFU;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.hl == 0U);
    ce_machine_reset(&machine);
    machine.cpu.control_adl_mode = 0U;
    machine.cpu.data_adl_mode = 1U;
    machine.cpu.hl = 0xFFFFFFU;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.hl == 0U);
    ce_machine_destroy(&machine);
}

static void test_ld_a_memory_and_cp_registers(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x3AU;
    bytes[1] = 0xB0U;
    bytes[2] = 0x00U;
    bytes[3] = 0xB8U;
    bytes[4] = 0xB9U;
    bytes[0xB0U] = 0x42U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.bc = 0x4242U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af >> 8U) == 0x42U);
    assert(machine.cpu.pc == 3U);
    assert(machine.cpu.cycles == 3U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af & 0x40U) != 0U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af & 0x40U) != 0U);
    ce_machine_destroy(&machine);
}

static void test_ld_memory_hl_writes_data_width(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x22U;
    bytes[1] = 0x00U;
    bytes[2] = 0x02U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.mbase = 0xD0U;
    machine.ram[0x0000U] = 0x22U;
    machine.ram[0x0001U] = 0x00U;
    machine.ram[0x0002U] = 0x02U;
    machine.cpu.hl = 0xBBAAU;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.ram[0x0200U] == 0xAAU);
    assert(machine.ram[0x0201U] == 0xBBU);
    assert(machine.cpu.pc == 3U);
    ce_machine_destroy(&machine);
}

static void test_flash_write_protection_distinguishes_privilege(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x22U;
    bytes[1] = 0x7CU;
    bytes[2] = 0x88U;
    bytes[0x887CU] = 0x5AU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.hl = 0x1234U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.nmi_pending == 0U);
    assert(machine.rom.bytes[0x887CU] == 0x5AU);

    ce_machine_reset(&machine);
    machine.privileged_end = 0U;
    machine.cpu.hl = 0x1234U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_ERROR);
    assert(machine.nmi_pending != 0U);
    assert(machine.rom.bytes[0x887CU] == 0x5AU);
    ce_machine_destroy(&machine);
}

static void test_ld_memory_a_ignores_locked_flash(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x32U;
    bytes[1] = 0xB7U;
    bytes[2] = 0x77U;
    bytes[0x77B7U] = 0x5AU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.af = 0xA500U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.rom.bytes[0x77B7U] == 0x5AU);
    assert(machine.cpu.pc == 3U);
    ce_machine_destroy(&machine);
}

static void test_ind_and_in0_are_observable(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xEDU;
    bytes[1] = 0xEAU;
    bytes[2] = 0xEDU;
    bytes[3] = 0x38U;
    bytes[4] = 0x0DU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.data_adl_mode = 1U;
    machine.cpu.hl = 0xD00100U;
    machine.cpu.bc = 0x0201U;
    machine.ram[0x0100U] = 0x80U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.hl == 0xD000FFU);
    assert((machine.cpu.bc >> 8U) == 1U);
    assert(machine.last_io_port == 0x0201U);
    assert(machine.last_io_value == 0x80U);
    machine.io_ports[0x0DU] = 0x36U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af >> 8U) == 0x36U);
    assert(machine.cpu.pc == 5U);
    ce_machine_destroy(&machine);
}

static void test_ld_immediate_registers(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x0EU;
    bytes[1] = 0x24U;
    bytes[2] = 0x06U;
    bytes[3] = 0xFFU;
    bytes[4] = 0x26U;
    bytes[5] = 0x81U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.bc & 0xFFU) == 0x24U);
    assert((machine.cpu.bc >> 8U) == 0xFFU);
    assert((machine.cpu.hl >> 8U) == 0x81U);
    ce_machine_destroy(&machine);
}

static void test_djnz_relative_branch(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x10U;
    bytes[1] = 0xFEU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.bc = 0x0200U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.bc >> 8U) == 1U);
    assert(machine.cpu.pc == 0U);
    assert(machine.cpu.cycles == 3U);
    ce_machine_destroy(&machine);
}

static void test_jr_unconditional_branch(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x18U;
    bytes[1] = 0xFEU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.pc == 0U);
    assert(machine.cpu.cycles == 2U);
    ce_machine_destroy(&machine);
}

static void test_im_1_is_recorded(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xEDU;
    bytes[1] = 0x56U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.interrupt_mode == 1U);
    assert(machine.cpu.pc == 2U);
    ce_machine_destroy(&machine);
}

static void test_ed_stack_absolute_load_store_in_long_mode(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xEDU; bytes[1] = 0x73U;
    bytes[2] = 0x10U; bytes[3] = 0x00U; bytes[4] = 0xD0U;
    bytes[5] = 0xEDU; bytes[6] = 0x7BU;
    bytes[7] = 0x10U; bytes[8] = 0x00U; bytes[9] = 0xD0U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.sp = 0xD1A872U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.ram[0x10U] == 0x72U);
    assert(machine.ram[0x11U] == 0xA8U);
    assert(machine.ram[0x12U] == 0xD1U);
    assert(machine.cpu.pc == 5U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.sp == 0xD1A872U);
    assert(machine.cpu.pc == 10U);
    ce_machine_destroy(&machine);
}

static void test_low_d177_fetches_from_flash(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_fetch_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0xD177U] = 0x8DU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.pc = 0xD177U;
    machine.ram[0x1177U] = 0x00U;
    assert(ce_machine_fetch_opcode(&machine, &result) == 0);
    assert(result.address == 0x00D177U);
    assert(result.opcode == 0x8DU);
    ce_machine_destroy(&machine);
}

static void test_physical_ram_precedes_flash_decode(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 0U};
    ce_rom rom = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);
    uint8_t value = 0U;

    assert(bytes != NULL);
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.data_adl_mode = 1U;
    machine.ram[0x1177U] = 0x8DU;
    assert(ce_machine_effective_data_address(&machine, 0xD01177U) ==
           0xD01177U);
    assert(machine.ram[0x1177U] == 0x8DU);
    (void)value;
    ce_machine_destroy(&machine);
}

    static void test_effective_address_uses_mbase_and_adl(void)
    {
        ce_machine machine = {0};
        ce_machine_config config = {CE_REVISION_I};
        ce_rom rom = {0};
        uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

        assert(bytes != NULL);
        assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
        assert(ce_machine_init(&machine, &config, &rom) == 0);
        assert(ce_machine_effective_data_address(&machine, 0x887CU) ==
            0x00887CU);
        machine.cpu.mbase = 0xD0U;
        assert(ce_machine_effective_data_address(&machine, 0x887CU) ==
            0xD0887CU);
        machine.cpu.data_adl_mode = 1U;
        assert(ce_machine_effective_data_address(&machine, 0xD0887CU) ==
            0xD0887CU);
        machine.cpu.data_adl_mode = 0U;
        machine.cpu.control_adl_mode = 1U;
        assert(ce_machine_effective_data_address(&machine, 0x123456U) ==
                0xD03456U);
        ce_machine_destroy(&machine);
    }

    static void test_adc_a_l_sets_z80_flags(void)
    {
        ce_machine machine = {0};
        ce_machine_config config = {CE_REVISION_I};
        ce_rom rom = {0};
        ce_step_result result = {0};
        uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

        assert(bytes != NULL);
        bytes[0] = 0x8DU;
        assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
        assert(ce_machine_init(&machine, &config, &rom) == 0);
        machine.cpu.af = 0x7F01U;
        machine.cpu.hl = 0x00U;
        assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
        assert((machine.cpu.af >> 8U) == 0x80U);
        assert((machine.cpu.af & 0xFFU) == 0x94U);
        ce_machine_reset(&machine);
        machine.cpu.af = 0xFF01U;
        machine.cpu.hl = 0x00U;
        assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
        assert((machine.cpu.af >> 8U) == 0x00U);
        assert((machine.cpu.af & 0xFFU) == 0x51U);
        ce_machine_destroy(&machine);
    }

    static void test_ld_mb_a_requires_adl_mode(void)
    {
        ce_machine machine = {0};
        ce_machine_config config = {CE_REVISION_I};
        ce_rom rom = {0};
        ce_step_result result = {0};
        uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

        assert(bytes != NULL);
        bytes[0] = 0x3EU;
        bytes[1] = 0xD0U;
        bytes[2] = 0xEDU;
        bytes[3] = 0x6DU;
        assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
        assert(ce_machine_init(&machine, &config, &rom) == 0);
        assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
        assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
        assert(machine.cpu.mbase == 0U);
        ce_machine_reset(&machine);
        machine.cpu.adl_mode = 1U;
        machine.cpu.control_adl_mode = 1U;
        machine.cpu.data_adl_mode = 1U;
        assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
        assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
        assert(machine.cpu.mbase == 0xD0U);
        ce_machine_destroy(&machine);
    }

static void test_cb_register_rotate_flags_and_pc(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xCBU;
    bytes[1] = 0x07U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.af = 0x8101U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af >> 8U) == 0x03U);
    assert((machine.cpu.af & 0xFFU) == 0x05U);
    assert(machine.cpu.pc == 2U);
    assert(machine.cpu.cycles == 2U);
    ce_machine_destroy(&machine);
}

static void test_cb_memory_operations_and_bit_numbers(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xCBU; bytes[1] = 0x46U;
    bytes[2] = 0xCBU; bytes[3] = 0xBEU;
    bytes[4] = 0xCBU; bytes[5] = 0xF6U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.data_adl_mode = 1U;
    machine.cpu.hl = 0xD00100U;
    machine.ram[0x0100U] = 0x81U;
    machine.cpu.af = 0x0001U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af & 0xFFU) == 0x11U);
    assert(machine.cpu.pc == 2U && machine.cpu.cycles == 4U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.ram[0x0100U] == 0x01U);
    assert(machine.cpu.pc == 4U && machine.cpu.cycles == 8U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.ram[0x0100U] == 0x41U);
    assert(machine.cpu.pc == 6U && machine.cpu.cycles == 12U);
    ce_machine_destroy(&machine);
}

static void test_interrupt_controller_mask_latch_invert_and_ack(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.interrupt_enable = (1U << 1U) | (1U << 2U);
    machine.interrupt_latch = 1U << 1U;
    ce_machine_set_interrupt_source(&machine, 1U, 1U);
    ce_machine_set_interrupt_source(&machine, 1U, 0U);
    assert((ce_machine_interrupt_masked_status(&machine) & (1U << 1U)) != 0U);
    ce_machine_acknowledge_interrupt(&machine, 1U << 1U);
    assert((ce_machine_interrupt_masked_status(&machine) & (1U << 1U)) == 0U);
    machine.interrupt_invert = 1U << 2U;
    ce_machine_set_interrupt_source(&machine, 2U, 0U);
    assert((ce_machine_interrupt_masked_status(&machine) & (1U << 2U)) != 0U);
    ce_machine_destroy(&machine);
}

static void test_halt_interrupt_mask_and_stack_frame(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x00U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.halted = 1U;
    machine.cpu.iff1 = 1U;
    machine.cpu.interrupt_mode = 1U;
    machine.cpu.pc = 0x123456U;
    machine.cpu.sp = 0xD00100U;
    ce_machine_set_interrupt_source(&machine, 1U, 1U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_HALTED);
    machine.interrupt_enable = 1U << 1U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.halted == 0U);
    assert(machine.cpu.pc == 0x0038U);
    assert(machine.cpu.sp == 0xD000FDU);
    assert(machine.ram[0x00FDU] == 0x56U);
    assert(machine.ram[0x00FEU] == 0x34U);
    assert(machine.ram[0x00FFU] == 0x12U);
    assert(machine.cpu.iff1 == 0U && machine.cpu.iff2 == 0U);
    assert(machine.cpu.cycles == 7U);
    ce_machine_destroy(&machine);
}

static void test_ei_delay_and_di(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xFBU;
    bytes[1] = 0x00U;
    bytes[2] = 0xF3U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.interrupt_enable = 1U << 1U;
    ce_machine_set_interrupt_source(&machine, 1U, 1U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.iff1 == 0U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.iff1 != 0U);
    assert(machine.cpu.pc == 2U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.iff1 == 0U);
    ce_machine_destroy(&machine);
}

static void test_flash_unlock_status_sequence(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x3EU; bytes[1] = 0x04U;
    bytes[2] = 0xEDU; bytes[3] = 0x39U; bytes[4] = 0x06U;
    bytes[5] = 0xEDU; bytes[6] = 0x39U; bytes[7] = 0x28U;
    bytes[8] = 0xCBU; bytes[9] = 0x57U;
    bytes[10] = 0xEDU; bytes[11] = 0x38U; bytes[12] = 0x28U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af >> 8U) == 0x0CU);
    assert(machine.flash_unlocked != 0U);
    ce_machine_destroy(&machine);
}

static void test_ld_a_hl_uses_data_address_mode(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x7EU;
    bytes[0x2345U] = 0xA5U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.hl = 0x12345U;
    machine.cpu.mbase = 0x00U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af >> 8U) == 0xA5U);
    machine.cpu.data_adl_mode = 1U;
    machine.cpu.pc = 0U;
    machine.cpu.hl = 0xD02345U;
    machine.ram[0x2345U] = 0x5AU;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af >> 8U) == 0x5AU);
    ce_machine_destroy(&machine);
}

static void test_cp_d_sets_flags(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xBAU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.af = 0x4200U;
    machine.cpu.de = 0x4200U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af & 0xFFU) == 0x42U);
    ce_machine_destroy(&machine);
}

static void test_cp_e_sets_flags(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xBBU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.af = 0x4200U;
    machine.cpu.de = 0x0042U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af & 0xFFU) == 0x42U);
    ce_machine_destroy(&machine);
}

static void test_and_immediate_sets_flags(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xE6U;
    bytes[1] = 0x0FU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.af = 0xF301U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af >> 8U) == 0x03U);
    assert((machine.cpu.af & 0xFFU) == 0x14U);
    assert(machine.cpu.pc == 2U && machine.cpu.cycles == 2U);
    ce_machine_destroy(&machine);
}

static void test_ld_b_hl_reads_memory(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x46U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.hl = 0x0200U;
    machine.ram[0x0200U] = 0xA5U;
    machine.cpu.data_adl_mode = 1U;
    machine.cpu.hl = 0xD00200U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.bc >> 8U) == 0xA5U);
    ce_machine_destroy(&machine);
}

static void test_ld_c_hl_reads_memory(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x4EU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.hl = 0xD00200U;
    machine.cpu.data_adl_mode = 1U;
    machine.ram[0x0200U] = 0x5AU;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.bc & 0xFFU) == 0x5AU);
    ce_machine_destroy(&machine);
}

static void test_ret_c_condition(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xD8U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.sp = 0xD00100U;
    machine.ram[0x0100U] = 0x34U;
    machine.ram[0x0101U] = 0x12U;
    machine.ram[0x0102U] = 0x00U;
    machine.cpu.af = 0x0001U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.pc == 0x1234U);
    assert(machine.cpu.sp == 0xD00103U);
    ce_machine_destroy(&machine);
}

static void test_add_hl_bc_sets_carry(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x09U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.hl = 0x9000U;
    machine.cpu.bc = 0x9000U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.hl == 0x2000U);
    assert((machine.cpu.af & 0x01U) != 0U);
    ce_machine_destroy(&machine);
}

static void test_dec_sp_respects_control_mode(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x3BU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.sp = 0xD00000U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.sp == 0xCFFFFFU);
    ce_machine_destroy(&machine);
}

static void test_dd_cb_indexed_res_set(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xDDU; bytes[1] = 0xCBU; bytes[2] = 0xEDU; bytes[3] = 0x86U;
    bytes[4] = 0xDDU; bytes[5] = 0xCBU; bytes[6] = 0xEDU; bytes[7] = 0xC6U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.ix = 0xD00200U;
    machine.ram[0x01EDU] = 0x01U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.ram[0x01EDU] == 0x00U);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.ram[0x01EDU] == 0x01U);
    ce_machine_destroy(&machine);
}

static void test_dd_daa_and_add_ix_bc(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xDDU; bytes[1] = 0x27U;
    bytes[2] = 0xDDU; bytes[3] = 0x09U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.af = 0x0A00U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af >> 8U) == 0x10U);
    machine.cpu.ix = 0x100000U;
    machine.cpu.bc = 0x000002U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.ix == 0x100002U);
    ce_machine_destroy(&machine);
}

static void test_ld_c_a(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x4FU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.af = 0xA500U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.bc & 0xFFU) == 0xA5U);
    ce_machine_destroy(&machine);
}

static void test_ld_bc_immediate_is_16_bit(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x01U;
    bytes[1] = 0x40U;
    bytes[2] = 0x01U;
    bytes[3] = 0x05U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.bc == 0x0140U);
    assert(machine.cpu.pc == 3U);
    ce_machine_destroy(&machine);
}

static void test_scf_sets_carry(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0x37U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.af = 0x1200U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert((machine.cpu.af & 0xFFU) == 0x01U);
    ce_machine_destroy(&machine);
}

static void test_ret_nz_condition(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xC0U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.sp = 0xD00100U;
    machine.ram[0x0100U] = 0x34U;
    machine.ram[0x0101U] = 0x12U;
    machine.ram[0x0102U] = 0x00U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.pc == 0x1234U);
    ce_machine_destroy(&machine);
}

static void test_ed_ld_hl_hl(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xEDU;
    bytes[1] = 0x27U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.hl = 0x0200U;
    machine.rom.bytes[0x0200U] = 0x34U;
    machine.rom.bytes[0x0201U] = 0x12U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.hl == 0x1234U);
    ce_machine_destroy(&machine);
}

static void test_ed_ld_hl_bc(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xEDU;
    bytes[1] = 0x0FU;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.hl = 0xD00200U;
    machine.cpu.bc = 0x003456U;
    machine.cpu.data_adl_mode = 0U;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.ram[0x0200U] == 0x56U);
    assert(machine.ram[0x0201U] == 0x34U);
    assert(machine.ram[0x0202U] == 0x00U);
    ce_machine_destroy(&machine);
}

static void test_ed_sbc_hl_bc_uses_control_width(void)
{
    ce_machine machine = {0};
    ce_machine_config config = {CE_REVISION_I, 1U};
    ce_rom rom = {0};
    ce_step_result result = {0};
    uint8_t *bytes = calloc(CE_FLASH_SIZE_BYTES, 1U);

    assert(bytes != NULL);
    bytes[0] = 0xEDU;
    bytes[1] = 0x42U;
    assert(ce_rom_init(&rom, bytes, CE_FLASH_SIZE_BYTES) == 0);
    assert(ce_machine_init(&machine, &config, &rom) == 0);
    machine.cpu.data_adl_mode = 0U;
    machine.cpu.hl = 0xD005F8U;
    machine.cpu.bc = 0x00000CU;
    assert(ce_machine_step(&machine, &result) == CE_STEP_OK);
    assert(machine.cpu.hl == 0xD005ECU);
    assert((machine.cpu.af & 0x01U) == 0U);
    ce_machine_destroy(&machine);
}

int main(void)
{
    test_revision_is_unknown_without_reliable_metadata();
    test_rom_loader_rejects_wrong_size();
    test_revision_names();
    test_machine_requires_explicit_revision_and_owns_rom();
    test_machine_fetches_from_reset_address();
    test_machine_executes_reset_sequence();
    test_xor_a_sets_zero_and_parity();
    test_out0_is_observable();
    test_ld_bc_hl_respects_data_mode_width();
    test_otimr_repeats_memory_to_port();
    test_inc_hl_respects_data_mode();
    test_ld_a_memory_and_cp_registers();
    test_ld_memory_hl_writes_data_width();
    test_flash_write_protection_distinguishes_privilege();
    test_ld_memory_a_ignores_locked_flash();
    test_ind_and_in0_are_observable();
    test_ld_immediate_registers();
    test_djnz_relative_branch();
    test_jr_unconditional_branch();
    test_im_1_is_recorded();
    test_ed_stack_absolute_load_store_in_long_mode();
    test_low_d177_fetches_from_flash();
    test_physical_ram_precedes_flash_decode();
    test_effective_address_uses_mbase_and_adl();
    test_adc_a_l_sets_z80_flags();
    test_ld_mb_a_requires_adl_mode();
    test_cb_register_rotate_flags_and_pc();
    test_cb_memory_operations_and_bit_numbers();
    test_interrupt_controller_mask_latch_invert_and_ack();
    test_halt_interrupt_mask_and_stack_frame();
    test_ei_delay_and_di();
    test_flash_unlock_status_sequence();
    test_ld_a_hl_uses_data_address_mode();
    test_cp_d_sets_flags();
    test_cp_e_sets_flags();
    test_and_immediate_sets_flags();
    test_ld_b_hl_reads_memory();
    test_ld_c_hl_reads_memory();
    test_ret_c_condition();
    test_add_hl_bc_sets_carry();
    test_dec_sp_respects_control_mode();
    test_dd_cb_indexed_res_set();
    test_dd_daa_and_add_ix_bc();
    test_ld_c_a();
    test_ld_bc_immediate_is_16_bit();
    test_scf_sets_carry();
    test_ret_nz_condition();
    test_ed_ld_hl_hl();
    test_ed_ld_hl_bc();
    test_ed_sbc_hl_bc_uses_control_width();
    return EXIT_SUCCESS;
}