/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "smk37_flash_map.h"

static uint8_t device[SMK37_FLASH_SIZE];
static uint32_t image_off = 0x400u;
static uint32_t image_size = SMK37_APP_HI;
static uint32_t package_size = 0x400u + SMK37_APP_HI;
static unsigned device_reads, erases, programs;

static uint32_t ota_rd16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t ota_rd32(const uint8_t *p) { return ota_rd16(p) | ota_rd16(p + 2) << 16; }

/* Mock the parsed UFW header and the transport. Rejected geometry must
 * return before even reading the device's key or asking for image bytes. */
static int ota_ufw(uint8_t *hdr, uint32_t *off, uint32_t *size,
                   uint32_t *ota_off, uint32_t *ota_len, uint32_t *crc)
{
    for (unsigned i = 0; i < 4; i++)
        hdr[4 + i] = (uint8_t)(package_size >> (8 * i));
    *off = image_off;
    *size = image_size;
    *ota_off = *ota_len = *crc = 0;
    return 0;
}

static int ldr_fread(uint32_t off, void *p, uint32_t n)
{
    (void)off;
    (void)p;
    (void)n;
    device_reads++;
    return -1;
}

static int ota_read(uint32_t off, uint8_t *p, uint32_t n)
{
    (void)off;
    (void)p;
    (void)n;
    assert(0 && "unexpected package read");
    return -1;
}

static int ldr_erase(uint32_t off)
{
    assert(SMK37_LOADER_WRITE_OK(off, SMK37_FLASH_SECTOR));
    memset(device + off, 0xff, SMK37_FLASH_SECTOR);
    erases++;
    return 0;
}

static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    assert(SMK37_LOADER_WRITE_OK(off, n));
    memcpy(device + off, p, n);
    programs++;
    return 0;
}

static void ota_jl_enc(uint8_t *p, uint32_t n) { (void)p; (void)n; }
static uint32_t ota_crc16(const uint8_t *p, uint32_t n, uint32_t crc)
{
    (void)p;
    (void)n;
    return crc;
}
static int ota_memeq(const uint8_t *p, const uint8_t *q, uint32_t n) { return memcmp(p, q, n) == 0; }
static int ldr_flash_known(void) { return 1; }
static void ldr_record_clear(void) { assert(0 && "unexpected update-record cleanup"); }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }

#include "../firmware/loader/ldr_core.c"

int main(void)
{
    /* Check every sector against the hardware boundaries, including the
     * boot head, the rest of VM, BTIF, USRTRIM, USRFLASH and key_mac. */
    for (uint32_t off = 0; off < SMK37_FLASH_SIZE; off += SMK37_FLASH_SECTOR) {
        int writable = (off >= 0x04000u && off < 0x9D000u) || (off >= 0xBB000u && off < 0xC0000u);
        assert(!!SMK37_LOADER_WRITE_OK(off, SMK37_FLASH_SECTOR) == writable);
        int app_writable = (off >= 0x9D000u && off < 0xAB000u) ||
            (off >= 0xAC000u && off < 0xAF000u) || (off >= 0xB0000u && off < 0xC0000u);
        assert(!!SMK37_APP_WRITE_OK(off, SMK37_FLASH_SECTOR) == app_writable);
    }
    uint32_t previous = 0;
    for (uint32_t off = SMK37_PROJECT_LO; off < SMK37_PROJECT_HI; off += SMK37_FLASH_SECTOR) {
        uint32_t physical = smk37_project_physical(off);
        assert(SMK37_APP_WRITE_OK(physical, SMK37_FLASH_SECTOR));
        assert(physical != 0xAB000u && physical != 0xAF000u);
        assert(!previous || physical > previous);
        previous = physical;
    }
    assert(previous == 0xB8000u);
    assert(!SMK37_LOADER_WRITE_OK(0x9CFFFu, 2u));
    assert(!SMK37_LOADER_WRITE_OK(0xBFFFFu, 2u));
    assert(!SMK37_LOADER_WRITE_OK(0x04000u, 0u));
    assert(!SMK37_LOADER_WRITE_OK(0x04000u, UINT32_MAX));
    assert(!SMK37_LOADER_WRITE_OK(UINT32_MAX, 2u));

    image_size = 0x93000u;
    assert(ldr_session() == -3);
    image_size = SMK37_APP_HI + 1u;
    package_size++;
    assert(ldr_session() == -3);
    image_size = SMK37_APP_HI;
    package_size -= 2u;
    assert(ldr_session() == -3);
    image_off = UINT32_MAX;
    assert(ldr_session() == -3);
    image_off = 0x3ffu;
    assert(ldr_session() == -3);
    assert(device_reads == 0 && erases == 0 && programs == 0);

    image_off = 0x400u;
    package_size = image_off + image_size;
    assert(ldr_session() == -41 && device_reads == 1);
    assert(erases == 0 && programs == 0);
    puts("PASS: SMK-37 loader write windows and package-size rejection");
}
