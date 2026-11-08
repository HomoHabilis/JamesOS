/* SPDX-License-Identifier: GPL-3.0-only
 * Pure early-start policy shared by the platform and host tests.
 */
#pragma once
#include <stdint.h>
#include "fm1_cpu1_early_cookie.h"

enum {
    FM1_CPU1_AUTOSTART_RESTORE_FAILED = 0u,
    FM1_CPU1_AUTOSTART_RESTORE_RETURN = 1u,
    FM1_CPU1_AUTOSTART_RESTORE_CONTINUE = 2u
};

static inline uint32_t fm1_cpu1_reboot_mode_valid(uint32_t mode)
{
    return mode <= 1u;
}

static inline uint32_t fm1_cpu1_reboot_cookie_kind(uint32_t mode)
{
    return mode == 0u ? FM1_CPU1_EARLY_COOKIE_BYPASS :
           mode == 1u ? FM1_CPU1_EARLY_COOKIE_NONE : 0xFFFFFFFFu;
}

/* Shared outer gates for both explicit cold-reboot modes. The platform still
 * verifies park/reset itself immediately before scheduling the reboot. */
static inline uint32_t fm1_cpu1_reboot_request_allowed(uint32_t mode,
                                                        uint32_t pending,
                                                        uint32_t playing,
                                                        uint32_t armed,
                                                        uint32_t fault,
                                                        uint32_t reset_held,
                                                        uint32_t muted,
                                                        uint32_t engine_quiet,
                                                        uint32_t fx_quiet)
{
    if (!fm1_cpu1_reboot_mode_valid(mode) || pending || playing || armed)
        return 0u;
    if (fault) return reset_held != 0u;
    return !muted && engine_quiet && fx_quiet;
}

static inline uint32_t fm1_cpu1_autostart_after_restore(uint32_t cookie_kind,
                                                         uint32_t recovery_or_rom,
                                                         uint32_t restore_verified)
{
    if (!restore_verified) return FM1_CPU1_AUTOSTART_RESTORE_FAILED;
    if (recovery_or_rom || cookie_kind == FM1_CPU1_EARLY_COOKIE_BYPASS)
        return FM1_CPU1_AUTOSTART_RESTORE_RETURN;
    return FM1_CPU1_AUTOSTART_RESTORE_CONTINUE;
}

static inline uint32_t fm1_cpu1_autostart_idle_journal_valid(uint32_t magic,
                                                              uint32_t state,
                                                              uint32_t integrity,
                                                              uint32_t expected_magic,
                                                              uint32_t expected_state,
                                                              uint32_t expected_integrity)
{
    return magic == expected_magic && state == expected_state &&
           integrity == expected_integrity;
}

static inline uint32_t fm1_cpu1_autostart_should_launch(uint32_t cookie_kind,
                                                        uint32_t recovery_or_rom,
                                                        uint32_t retry_suppressed)
{
    if (recovery_or_rom || cookie_kind == FM1_CPU1_EARLY_COOKIE_BYPASS ||
        retry_suppressed ||
        (cookie_kind != FM1_CPU1_EARLY_COOKIE_NONE &&
         cookie_kind != FM1_CPU1_EARLY_COOKIE_START))
        return 0u;
    return 1u;
}

static inline uint32_t fm1_cpu1_autostart_preflight_ok(uint32_t flags)
{
    return flags == 255u;
}

static inline uint32_t fm1_cpu1_autostart_retry_suppressed(uint32_t cookie_kind,
                                                            uint32_t status,
                                                            uint32_t restore_verified,
                                                            uint32_t failure_status)
{
    /* An explicit one-shot START request overrides one automatic retry delay. */
    return cookie_kind != FM1_CPU1_EARLY_COOKIE_START &&
           status == failure_status && restore_verified;
}
