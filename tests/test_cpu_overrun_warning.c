/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include "../firmware/src/cpu_overrun_warning.h"
int main(void) {
    br_cpu_warning_t s = {0};
    assert(!br_cpu_warning_step(&s, 100u, 0u, 120u));
    assert(!br_cpu_warning_step(&s, 200u, 1u, 120u));
    assert(br_cpu_warning_step(&s, 8200u, 2u, 120u)); /* four bars at 120 BPM */
    assert(!br_cpu_warning_step(&s, 8300u, 10u, 120u)); /* only once */
    assert(!br_cpu_warning_take_notice(&s, 1u)); /* preserve a visible safety message */
    assert(br_cpu_warning_take_notice(&s, 0u)); /* show once after it clears */
    assert(!br_cpu_warning_take_notice(&s, 0u));
    s = (br_cpu_warning_t){0};
    assert(!br_cpu_warning_step(&s, 0u, 0u, 120u));
    assert(!br_cpu_warning_step(&s, 10u, 1u, 120u));
    assert(!br_cpu_warning_step(&s, 8011u, 2u, 120u)); /* first expired */
    assert(br_cpu_warning_step(&s, 8100u, 3u, 120u));
    s = (br_cpu_warning_t){0};
    assert(!br_cpu_warning_step(&s, UINT32_MAX-10u, 5u, 240u));
    assert(br_cpu_warning_step(&s, 20u, 7u, 240u)); /* two in one poll, clock wrap */
    return 0;
}
