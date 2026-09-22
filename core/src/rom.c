#include "ce/rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ce_rom_clear(ce_rom *rom)
{
    if (rom != NULL) {
        rom->bytes = NULL;
        rom->size = 0U;
    }
}

int ce_rom_init(ce_rom *rom, uint8_t *bytes, size_t size)
{
    if ((rom == NULL) || (bytes == NULL) || (size != CE_FLASH_SIZE_BYTES)) {
        free(bytes);
        return -1;
    }

    ce_rom_destroy(rom);
    rom->bytes = bytes;
    rom->size = size;
    return 0;
}

int ce_rom_load_file(const char *path, ce_rom *rom)
{
    FILE *file;
    uint8_t *bytes;
    size_t read_size;

    if ((path == NULL) || (rom == NULL)) {
        return -1;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    bytes = malloc(CE_FLASH_SIZE_BYTES);
    if (bytes == NULL) {
        fclose(file);
        return -1;
    }

    read_size = fread(bytes, 1U, CE_FLASH_SIZE_BYTES, file);
    if ((read_size != CE_FLASH_SIZE_BYTES) || (fgetc(file) != EOF)) {
        free(bytes);
        fclose(file);
        return -1;
    }

    fclose(file);
    return ce_rom_init(rom, bytes, read_size);
}

void ce_rom_destroy(ce_rom *rom)
{
    if (rom == NULL) {
        return;
    }

    free(rom->bytes);
    ce_rom_clear(rom);
}

ce_revision_identification ce_rom_identify_revision(const ce_rom *rom)
{
    ce_revision_identification identification = {
        CE_REVISION_UNKNOWN,
        CE_REVISION_SOURCE_NONE,
        0U
    };

    (void)rom;
    return identification;
}

const char *ce_revision_name(ce_revision revision)
{
    switch (revision) {
    case CE_REVISION_PRE_A:
        return "pre-A";
    case CE_REVISION_I:
        return "I";
    case CE_REVISION_M:
        return "M";
    case CE_REVISION_UNKNOWN:
    default:
        return "unknown";
    }
}