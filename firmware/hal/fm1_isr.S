/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* IRQ entry wrappers: save what a C call may clobber, call the C body,
 * restore, csync, rti. clang does not emit the csync needed before rti,
 * so ISRs are not written in C directly. */
	.macro FM1_ISR name, body
	.section .text.\name,"ax",@progbits
	.globl \name
\name:
	[--sp] = {psr, rets, reti}
	[--sp] = {r3-r0}
	call \body
	{r3-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
	.endm

	/* Controlled diagnostic ablation: preserve every GPR around Timer5. */
	.macro FM1_ISR_FULL_GPRS name, body
	.section .text.\name,"ax",@progbits
	.globl \name
\name:
	[--sp] = {psr, rets, reti}
	[--sp] = {r15-r0}
	call \body
	{r15-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
	.endm

#ifndef FM1_TIMER5_MINIMAL_GPR_SAVE
#define FM1_TIMER5_MINIMAL_GPR_SAVE 1
#endif
#if FM1_TIMER5_MINIMAL_GPR_SAVE
	FM1_ISR isr_timer5, fm1_timer5_irq
#else
	FM1_ISR_FULL_GPRS isr_timer5, fm1_timer5_irq
#endif
	FM1_ISR isr_alnk0, fm1_alnk0_irq
