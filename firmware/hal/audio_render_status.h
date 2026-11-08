/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/* Audio callback outcome. A dropped half must not be written again after the
 * callback: DMA may already have switched to it. An uncontained fault means
 * a worker may still own audio memory and must remain a separate outcome. */
enum {
    FM1_AUDIO_RENDER_UNCONTAINED = 0u,
    FM1_AUDIO_RENDER_OK = 1u,
    FM1_AUDIO_RENDER_DROP_LATE = 2u,
    /* Fault is latched and the worker is contained, but this half may have
     * expired. Do not write it again; resynchronize before rendering silence. */
    FM1_AUDIO_RENDER_CONTAINED_FAULT = 3u
};
