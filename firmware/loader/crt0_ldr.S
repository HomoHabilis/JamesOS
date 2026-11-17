/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca update loader entry. The SPL decompresses the loader to its load
 * address (0x01C0A800) and calls its first byte. Interrupts off,
 * our own stacks, clear .bss, then C. Never returns.
 * The product switch needs -DBR_PRODUCT on this file's command line (tools/build.py):
 * without it the FM-1 loader is assembled. */
#include "../product.h"
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	.ascii "FELUCCA-LOADER-1"	/* 16 bytes: Felucca recognises its loader in a package by this */
#if BR_PRODUCT_SMK37
	.ascii "JAMESOS-K-SMK37!"	/* SMK-37 Pro: ota.c stages only a loader with both 16-byte marks */
#endif
	.balign 2
1:	cli
	sp = _stack_top
	ssp = _sstack_top
	call ldr_cstart
	goto .
