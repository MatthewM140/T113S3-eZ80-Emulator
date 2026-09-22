/* Project-owned headless glue for the GPLv3 CEmu core. */
#include "third_party/cemu/core/emu.h"
#include "third_party/cemu/core/asic.h"
#include "third_party/cemu/core/usb/usb.h"

#include <stdarg.h>

static asic_rev_t cemu_requested_revision = ASIC_REV_I;

void ce_cemu_set_revision(asic_rev_t revision)
{
    cemu_requested_revision = revision;
}

void gui_console_clear(void) {}
void gui_console_printf(const char *format, ...) { (void)format; }
void gui_console_err_printf(const char *format, ...) { (void)format; }

asic_rev_t gui_handle_reset(const boot_ver_t *boot_ver,
                            asic_rev_t loaded_revision,
                            asic_rev_t default_revision,
                            emu_device_t device,
                            bool *python)
{
    (void)boot_ver;
    (void)loaded_revision;
    (void)default_revision;
    (void)device;
    if (python != NULL) {
        *python = false;
    }
    return cemu_requested_revision;
}

/* USB is intentionally omitted from the first headless backend milestone. */
static uint8_t usb_headless_read(uint16_t address, bool peek)
{
    (void)address;
    (void)peek;
    return 0U;
}

static void usb_headless_write(uint16_t address, uint8_t value, bool poke)
{
    (void)address;
    (void)value;
    (void)poke;
}

eZ80portrange_t init_usb(void)
{
    eZ80portrange_t device = {
        usb_headless_read,
        usb_headless_write
    };
    return device;
}

void usb_reset(void) {}
bool usb_restore(FILE *image) { (void)image; return true; }
bool usb_save(FILE *image) { (void)image; return true; }
uint8_t usb_status(void) { return 0U; }
int usb_plug_device(int argc, const char *const *argv,
                    usb_progress_handler_t *progress_handler,
                    void *progress_context)
{
    (void)argc;
    (void)argv;
    (void)progress_handler;
    (void)progress_context;
    return -1;
}
