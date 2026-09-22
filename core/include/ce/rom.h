#ifndef CE_ROM_H
#define CE_ROM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CE_FLASH_SIZE_BYTES (4U * 1024U * 1024U)

typedef enum ce_revision {
    CE_REVISION_UNKNOWN = 0,
    CE_REVISION_PRE_A,
    CE_REVISION_I,
    CE_REVISION_M
} ce_revision;

typedef enum ce_revision_source {
    CE_REVISION_SOURCE_NONE = 0,
    CE_REVISION_SOURCE_EXPLICIT,
    CE_REVISION_SOURCE_ROM_METADATA
} ce_revision_source;

typedef struct ce_rom {
    uint8_t *bytes;
    size_t size;
} ce_rom;

typedef struct ce_revision_identification {
    ce_revision revision;
    ce_revision_source source;
    uint8_t confidence_percent;
} ce_revision_identification;

/* Loads a raw CE flash image. The caller owns the returned storage. */
int ce_rom_load_file(const char *path, ce_rom *rom);

/* Takes ownership of a caller-provided buffer on success. */
int ce_rom_init(ce_rom *rom, uint8_t *bytes, size_t size);

void ce_rom_destroy(ce_rom *rom);

/* Metadata parsing is intentionally conservative: unknown means unknown. */
ce_revision_identification ce_rom_identify_revision(const ce_rom *rom);

const char *ce_revision_name(ce_revision revision);

#ifdef __cplusplus
}
#endif

#endif