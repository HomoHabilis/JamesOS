/* SPDX-License-Identifier: GPL-3.0-only */
/* Include after firmware/src/dinghy.c. The SMK-37 Pro shows its two-second name intro on host
 * builds too (dinghy.c br_app_init sets br_intro_until for BR_PRODUCT_SMK37), so br_draw() draws
 * the intro until it expires. Page checks start after it, as on the device: host_after_intro()
 * runs the real expiry (br_draw at the deadline: black screen, full redraw). FM-1 host builds
 * have no intro, so it does nothing there. */
#ifndef BR_HOST_INTRO_H
#define BR_HOST_INTRO_H
static void host_after_intro(void)
{
    if (br_intro_until) { fm1_ms = br_intro_until; br_draw(); }
}
#endif
