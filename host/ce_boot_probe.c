#include "ce/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void print_coverage(const char *label, const unsigned int *counts)
{
    printf("%s", label);
    for (unsigned int index = 0U; index < 256U; ++index) {
        if (counts[index] != 0U) {
            printf(" %02X(%u)", index, counts[index]);
        }
    }
    putchar('\n');
}

int main(int argc, char **argv)
{
    ce_machine_config config = {CE_REVISION_UNKNOWN};
    ce_machine machine = {0};
    ce_step_result step;
    ce_rom rom = {0};
    unsigned int base_implemented[256] = {0U};
    unsigned int cb_implemented[256] = {0U};
    unsigned int ed_implemented[256] = {0U};
    unsigned int unsupported[256] = {0U};
    unsigned int count = 8U;

    config.initial_adl_mode = 1U;

    if ((argc < 5) || (strcmp(argv[1], "--rom") != 0) ||
        (strcmp(argv[3], "--revision") != 0) ||
        (parse_revision(argv[4], &config.revision) != 0)) {
        fprintf(stderr,
                "usage: %s --rom PATH --revision pre-a|i|m [--count N]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if ((argc == 7) && (strcmp(argv[5], "--count") == 0)) {
        char *end = NULL;
        unsigned long parsed = strtoul(argv[6], &end, 10);
        if ((*argv[6] == '\0') || (*end != '\0') || (parsed > 1000000UL)) {
            fprintf(stderr, "invalid count\n");
            return EXIT_FAILURE;
        }
        count = (unsigned int)parsed;
    } else if (argc != 5) {
        fprintf(stderr, "invalid arguments\n");
        return EXIT_FAILURE;
    }

    if (ce_rom_load_file(argv[2], &rom) != 0) {
        fprintf(stderr, "failed to load ROM: %s\n", argv[2]);
        return EXIT_FAILURE;
    }
    if (ce_machine_init(&machine, &config, &rom) != 0) {
        fprintf(stderr, "failed to initialize machine\n");
        ce_rom_destroy(&rom);
        return EXIT_FAILURE;
    }

    printf("revision=%s\n", ce_revision_name(config.revision));
    for (unsigned int index = 0U; index < count; ++index) {
        uint32_t pc_before = ce_machine_cpu_state(&machine)->pc;
        uint64_t cycles_before = ce_machine_cpu_state(&machine)->cycles;
        if (pc_before == 0x0006DDU) {
            uint32_t stack_address = machine.cpu.sp & 0xFFFFFFU;
            uint8_t stack_bytes[3] = {0U, 0U, 0U};
            for (unsigned int byte_index = 0U; byte_index < 3U; ++byte_index) {
                uint32_t address = stack_address + byte_index;
                if ((address >= 0xD00000U) &&
                    (address < 0xD00000U + CE_RAM_SIZE_BYTES)) {
                    stack_bytes[byte_index] = machine.ram[address - 0xD00000U];
                } else if (address < machine.rom.size) {
                    stack_bytes[byte_index] = machine.rom.bytes[address];
                }
            }
            printf("ret_stack=%06X bytes=%02X %02X %02X\n",
                   (unsigned int)stack_address,
                   (unsigned int)stack_bytes[0],
                   (unsigned int)stack_bytes[1],
                   (unsigned int)stack_bytes[2]);
        }
        ce_step_status status = ce_machine_step(&machine, &step);
        unsigned int *coverage = base_implemented;
        if (step.opcode == 0xCBU) {
            coverage = cb_implemented;
        } else if (step.opcode == 0xEDU) {
            coverage = ed_implemented;
        }
        if (status == CE_STEP_OK) {
            uint8_t encoded = step.opcode;
            if ((step.opcode == 0xCBU) || (step.opcode == 0xEDU)) {
                if (step.address + 1U < machine.rom.size) {
                    encoded = machine.rom.bytes[step.address + 1U];
                }
            }
            coverage[encoded]++;
        } else if (status == CE_STEP_UNIMPLEMENTED) {
            unsupported[step.opcode]++;
        }
        if (status != CE_STEP_OK) {
            fprintf(stderr,
                    "strict stop at step %u status=%d opcode=%02X "
                    "address=%06X last_trustworthy_pc=%06X "
                    "cycles=%llu mbase=%02X privileged=%u "
                    "flash_attempted=%u nmi=%u af=%06X bc=%06X "
                    "de=%06X hl=%06X sp=%06X\n",
                    index,
                    (int)status,
                    (unsigned int)step.opcode,
                    (unsigned int)step.address,
                    (unsigned int)pc_before,
                    (unsigned long long)cycles_before,
                    (unsigned int)machine.cpu.mbase,
                    (unsigned int)(pc_before <= machine.privileged_end),
                    (unsigned int)machine.flash_write_attempted,
                    (unsigned int)machine.nmi_pending,
                    (unsigned int)machine.cpu.af,
                    (unsigned int)machine.cpu.bc,
                    (unsigned int)machine.cpu.de,
                    (unsigned int)machine.cpu.hl,
                    (unsigned int)machine.cpu.sp);
            if ((status == CE_STEP_ERROR) ||
                (status == CE_STEP_FLASH_UNIMPLEMENTED)) {
                    fprintf(stderr, "bus_address=%06X bus_write=%u\n",
                        (unsigned int)machine.last_bus_address,
                        (unsigned int)machine.last_bus_write);
                    }
                    print_coverage("implemented_base:", base_implemented);
                    print_coverage("implemented_cb:", cb_implemented);
                    print_coverage("implemented_ed:", ed_implemented);
                    print_coverage("unsupported:", unsupported);
            ce_machine_destroy(&machine);
            return EXIT_FAILURE;
        }
         printf("step=%u address=%06X opcode=%02X status=%d pc=%06X "
             "mbase=%02X adl=%u data_adl=%u cycles=%llu\n",
               index,
               (unsigned int)step.address,
               (unsigned int)step.opcode,
               (int)step.status,
               (unsigned int)ce_machine_cpu_state(&machine)->pc,
               (unsigned int)ce_machine_cpu_state(&machine)->mbase,
               (unsigned int)ce_machine_cpu_state(&machine)->control_adl_mode,
               (unsigned int)ce_machine_cpu_state(&machine)->data_adl_mode,
               (unsigned long long)ce_machine_cpu_state(&machine)->cycles);
    }

    print_coverage("implemented_base:", base_implemented);
    print_coverage("implemented_cb:", cb_implemented);
    print_coverage("implemented_ed:", ed_implemented);
    print_coverage("unsupported:", unsupported);

    ce_machine_destroy(&machine);
    return EXIT_SUCCESS;
}