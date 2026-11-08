/* Included by platform.c after the flash/cache hardware helpers. */
#ifndef FM1_CPU1_CACHE_START_H
#define FM1_CPU1_CACHE_START_H
#if FM1_C1_ICACHE_XIP_TRAMPOLINE
/* SDK evidence: the AC79 SDK's global `icache_flush` at 0x1c0106c rounds its
 * pointer down and length up to 32-byte lines, waits for CACHE_CON bit 14,
 * then issues `csync; flushinv [addr]; csync` and waits again per line. The
 * target linker start is aligned, so rounding this absolute end covers the
 * same span. The matching CACHE_CON definition and idle predicate are in
 * fm1_flash.h. Bounds come from linker symbols, not a fixed memory-map guess. */
static __attribute__((noinline, used, noreturn))
void fm1_cpu1_worker_xip_cache_helper(void)
{
    uint32_t first = (uint32_t)(uintptr_t)_rt_start & ~31u;
    uint32_t end = (uint32_t)(uintptr_t)_ra_end;
    uint32_t limit;

    /* The linked executable span is below 0x80000000 and far from wrap. */
    if (end <= (uint32_t)(uintptr_t)_rt_start || end > UINT32_MAX - 31u) {
        for (;;) { }
    }
    limit = (end + 31u) & ~31u;
    fl_cache_idle();
    for (; first < limit; first += 32u) {
        fl_csync();
        __asm__ volatile("flushinv [%0]" :: "r"(first) : "memory");
        fl_csync();
        fl_cache_idle();
    }

    /* We are already in normal C stack/mode after the trampoline's RTI.
     * Transfer directly to the RAM worker entry; the original reset stub is
     * not a C callee and must only be used as the default hardware entry. */
    void (* volatile start_worker)(void) = fm1_cpu1_worker_main;
    start_worker();
    for (;;) { }
}

/* CPU1 is pointed directly at this XIP function for the experiment. It sets
 * known stacks, then enters the normal C helper through the existing RTI
 * mode transition. The helper invalidates copied RAM code and calls the
 * RAM worker entry through a typed volatile pointer. */
__attribute__((section(".text.c1_worker_xip_cache_trampoline"), noinline,
               used, naked, noreturn))
static void fm1_cpu1_worker_xip_cache_trampoline(void)
{
    __asm__ volatile("cli\n\t"
                     "usp = _fm1_cpu1_worker_ustack_top\n\t"
                     "sp = _fm1_cpu1_worker_sstack_top\n\t"
                     "reti = fm1_cpu1_worker_xip_cache_helper\n\t"
                     "rti\n\t");
}
#endif
#endif
