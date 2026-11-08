/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_MATH_POLICY_H
#define FM1_MATH_POLICY_H
/* Opt-in DSP-only reassociation/contraction experiment. HAL, app/control and
 * loader compilation retain strict FP. Infinity/NaN assumptions and reciprocal
 * approximation are deliberately not enabled by this policy. */
#ifndef FM1_DSP_RELAXED_FP
#define FM1_DSP_RELAXED_FP 1
#endif
/* Default-off experiment: compile only the exact cold-engine TU allowlist at
 * -Os while keeping all DSP floating-point flags unchanged. */
#ifndef FM1_PLAITS_COLD_TU_OS
#define FM1_PLAITS_COLD_TU_OS 1
#endif
/* Experimental compiler ablation for the percussion-state PC-limit fault.
 * Equations, buffer geometry and optimization level are unchanged. */
#ifndef FM1_BRAIDS_PERC_NO_JUMP_TABLES
#define FM1_BRAIDS_PERC_NO_JUMP_TABLES 0
#endif
#endif
