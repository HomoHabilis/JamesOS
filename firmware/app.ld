/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C06000 holds strict .ram_text (flash driver) followed by
 * .ram_audio (CPU0 DSP callbacks); 0x01C06000..0x01C08000 stays for RAM-run debug.
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .ram_audio : { _ra_start = .; KEEP(*(.ram_audio*)) . = ALIGN(4); _ra_end = .; } > RAMTEXT AT > XIP
    _ra_load = LOADADDR(.ram_audio);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; KEEP(*(.cpu1_worker_bss.*)) *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(16); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    PROVIDE(_fm1_cpu1_worker_ustack_top = fm1_cpu1_worker_ustack + 0x2000);
    PROVIDE(_fm1_cpu1_worker_sstack_top = fm1_cpu1_worker_sstack + 0x800);
    .noinit (NOLOAD) : { *(.noinit) } > NOINIT
    /* Keep legacy recovery records stable; diagnostics follow the full old block. */
    .noinit.cpu1_trace (NOLOAD) : { KEEP(*(.noinit.cpu1_trace)) } > NOINIT
    .noinit.cpu1_diag (NOLOAD) : { KEEP(*(.noinit.cpu1_diag)) } > NOINIT
    .noinit.cpu1_fault_bundle (NOLOAD) : { KEEP(*(.noinit.cpu1_fault_bundle)) } > NOINIT
    .noinit.fx_progress (NOLOAD) : { KEEP(*(.noinit.fx_progress)) } > NOINIT
    .noinit.watchdog (NOLOAD) : { KEEP(*(.noinit.watchdog)) } > NOINIT
    /* Append new retained diagnostics after every legacy record. */
    .noinit.end (NOLOAD) : { KEEP(*(.noinit.end)) } > NOINIT
    /* Optional records remain after the original 80-byte .noinit.end object. */
    .noinit.end.c1_timeout (NOLOAD) : { KEEP(*(.noinit.end.c1_timeout)) } > NOINIT
    .noinit.end.c0_stack_window (NOLOAD) : { KEEP(*(.noinit.end.c0_stack_window)) } > NOINIT
    .noinit.end.ram_code (NOLOAD) : { KEEP(*(.noinit.end.ram_code)) } > NOINIT
    /* CPU1 fault context is strictly append-only after the prior RAM-code record. */
    .noinit.end.c1_context (NOLOAD) : { KEEP(*(.noinit.end.c1_context)) } > NOINIT
    .noinit.end.dual_code (NOLOAD) : { KEEP(*(.noinit.end.dual_code)) } > NOINIT
    .noinit.end.power_read (NOLOAD) : { KEEP(*(.noinit.end.power_read)) } > NOINIT
    .noinit.end.vendor360 (NOLOAD) : { KEEP(*(.noinit.end.vendor360)) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
