/* SPDX-License-Identifier: GPL-3.0-only */
#include "fm1_cpu1_smoke.h"

static int valid_ops(const fm1_cpu1_smoke_ops_t *o)
{
    return o && o->read_control && o->read_entry && o->read_debug_enable &&
           o->read_write_enable && o->write_control &&
           o->write_entry && o->barrier && o->clear_mailbox && o->read_ready &&
           o->read_heartbeat && o->read_cpu_id && o->ticks && o->irq_save && o->irq_restore;
}

uint32_t fm1_cpu1_smoke_run(const fm1_cpu1_smoke_ops_t *o,
                            uint32_t start_entry,
                            uint32_t timeout_ticks,
                            fm1_cpu1_smoke_result_t *r)
{
    uint32_t irq, control, entry, dbg_en, write_en, began, ready = 0u, heartbeat = 0u, cpu_id = 0xffffffffu;
    uint32_t status, current;
    if (!r) return FM1_CPU1_SMOKE_BAD_LIMIT;
    r->status = FM1_CPU1_SMOKE_BAD_LIMIT;
    r->heartbeat = 0u;
    r->cpu_id = 0xffffffffu;
    r->restored = 0u;
    r->saved_control = 0u;
    r->saved_entry = 0u;
    r->saved_debug_enable = 0u;
    r->saved_write_enable = 0u;
    if (!valid_ops(o) || !start_entry || !timeout_ticks ||
        timeout_ticks > FM1_CPU1_SMOKE_LIMIT)
        return r->status;

    irq = o->irq_save(o->context);
    control = o->read_control(o->context);
    entry = o->read_entry(o->context);
    dbg_en = o->read_debug_enable(o->context);
    write_en = o->read_write_enable(o->context);
    r->saved_control = control;
    r->saved_entry = entry;
    r->saved_debug_enable = dbg_en;
    r->saved_write_enable = write_en;
    /* The bootstrap runs before application guard setup. Refuse before any
     * C1 write if either shared guard facility is already active. */
    if (dbg_en != 0u || write_en != 0u) {
        o->irq_restore(o->context, irq);
        r->restored = 1u;
        r->status = FM1_CPU1_SMOKE_BAD_LIMIT;
        return r->status;
    }
    if (!(control & FM1_CPU1_RESET_BIT)) {
        o->irq_restore(o->context, irq);
        /* Nothing was changed; the caller may safely restore normal output. */
        r->restored = 1u;
        r->status = FM1_CPU1_SMOKE_BUSY;
        return r->status;
    }

    o->clear_mailbox(o->context);
    began = o->ticks(o->context);
    if (!o->write_entry(o->context, start_entry) ||
        o->read_entry(o->context) != start_entry) {
        /* A failed guard transaction is not enough proof to unmute, even if
         * the core registers appear unchanged. */
        r->restored = 0u;
        r->status = FM1_CPU1_SMOKE_GUARD_REFUSED;
        o->irq_restore(o->context, irq);
        return r->status;
    }
    o->barrier(o->context);
    current = control | FM1_CPU1_START_BIT;
    o->write_control(o->context, current);
    o->write_control(o->context, current & ~FM1_CPU1_RESET_BIT);
    o->irq_restore(o->context, irq);

    /* CPU0 leaves interrupts enabled during this bounded main-context wait. */
    do {
        ready = o->read_ready(o->context);
        heartbeat = o->read_heartbeat(o->context);
        cpu_id = o->read_cpu_id(o->context);
        if (ready && heartbeat >= 2u) break;
    } while ((uint32_t)(o->ticks(o->context) - began) < timeout_ticks);
    status = (ready && heartbeat >= 2u) ?
        (cpu_id == 1u ? FM1_CPU1_SMOKE_OK : FM1_CPU1_SMOKE_BAD_CPU) : FM1_CPU1_SMOKE_TIMEOUT;

    /* Vendor exception recovery clears START then asserts RESET. Restore the
     * saved boot entry and original reset control word after the core is held. */
    irq = o->irq_save(o->context);
    current = o->read_control(o->context);
    o->write_control(o->context, (current & ~FM1_CPU1_START_BIT) | FM1_CPU1_RESET_BIT);
    o->barrier(o->context);
    current = o->read_control(o->context);
    if (!(current & FM1_CPU1_RESET_BIT)) {
        /* Do not change the entry/control image underneath a core we failed
         * to prove held in reset. Caller keeps audio muted on this result. */
        o->irq_restore(o->context, irq);
        r->heartbeat = heartbeat;
        r->cpu_id = cpu_id;
        r->status = FM1_CPU1_SMOKE_RESTORE_FAILED;
        return r->status;
    }
    if (!o->write_entry(o->context, entry) || o->read_entry(o->context) != entry) {
        o->irq_restore(o->context, irq);
        r->heartbeat = heartbeat;
        r->cpu_id = cpu_id;
        r->status = FM1_CPU1_SMOKE_RESTORE_FAILED;
        return r->status;
    }
    o->write_control(o->context, control);
    o->barrier(o->context);
    o->irq_restore(o->context, irq);

    irq = o->irq_save(o->context);
    r->restored = (o->read_control(o->context) == control &&
                   o->read_entry(o->context) == entry) ? 1u : 0u;
    r->heartbeat = heartbeat;
    r->cpu_id = cpu_id;
    o->irq_restore(o->context, irq);
    if (!r->restored) status = FM1_CPU1_SMOKE_RESTORE_FAILED;
    r->status = status;
    return status;
}
