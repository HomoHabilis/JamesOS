#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../firmware/hal/fm1_fault_diag.h"

static void test_v3_active_sp_record_and_selector4_read(void)
{
    volatile fm1_fault_c0_stack_window_record_t record;
    uint32_t frame[26] = {0};
    uint32_t words[FM1_FAULT_C1_TIMEOUT_C0_WORDS];
    uint8_t page[128];
    uint32_t i;
    const uint32_t user_low = 0x1c74100u;
    const uint32_t user_high = 0x1c7a000u;
    const uint32_t system_low = 0x1c7a100u;
    const uint32_t system_high = 0x1c7c000u;

    memset((void *)&record, 0, sizeof record);
    for (i = 0u; i < FM1_FAULT_C1_TIMEOUT_C0_WORDS; ++i)
        words[i] = 0xabc00000u + i;
    frame[16] = 0x01c022b4u;
    frame[22] = 0x1c79790u;
    frame[23] = system_high;
    frame[24] = 0x1c7ba50u;

    assert(sizeof record == 72u);
    assert(offsetof(fm1_fault_c0_stack_window_record_t, saved_usp) == 24u);
    assert(fm1_fault_c0_stack_window_capture_abi(
        &record, 0u, 22u, frame[16], frame[24], system_low, system_high,
        words, FM1_FAULT_C0_SYSTEM_STACK_VERSION));
    assert(record.magic == FM1_FAULT_C1_TIMEOUT_C0_STACK_MAGIC);
    assert(record.version == FM1_FAULT_C0_SYSTEM_STACK_VERSION);
    assert(record.byte_size == 72u && record.fault_count == 22u);
    assert(record.fault_pc == frame[16] && record.saved_usp == frame[24]);
    assert(record.sample_base == frame[24] + FM1_FAULT_C0_SYSTEM_STACK_OFFSET);
    for (i = 0u; i < FM1_FAULT_C1_TIMEOUT_C0_WORDS; ++i)
        assert(record.words[i] == words[i]);
    assert(fm1_fault_c0_stack_window_record_valid_abi(
        &record, FM1_FAULT_C0_SYSTEM_STACK_VERSION, system_low, system_high));
    assert(!fm1_fault_c0_stack_window_record_valid_abi(
        &record, FM1_FAULT_C1_TIMEOUT_C0_STACK_VERSION, user_low, user_high));

    assert(FM1_FAULT_C0_SYSTEM_STACK_READ_KIND == 4u);
    assert(fm1_fault_c0_stack_window_chunk_abi(
        &record, FM1_FAULT_C0_SYSTEM_STACK_VERSION,
        system_low, system_high, 0u, sizeof record, page) == sizeof record + 3u);
    assert(page[0] == 0u && page[1] == 0u && page[2] == sizeof record);
    assert(memcmp(page + 3u, (const void *)&record, sizeof record) == 0);

    /* The active-system stack window includes a local at SP+80 and ends at
     * SP+100, while keeping the same ten-word, 72-byte retained record. */
    assert(record.sample_base == frame[24] + 64u);
    assert(record.words[4] == words[4]);
    assert(FM1_FAULT_C0_SYSTEM_STACK_LAST_OFFSET == 100u);
    assert(FM1_FAULT_C0_SYSTEM_STACK_VERSION == 3u);
}

static void test_selector1_can_still_read_old_v1_record(void)
{
    volatile fm1_fault_c0_stack_window_record_t record;
    uint32_t words[FM1_FAULT_C1_TIMEOUT_C0_WORDS] = {0};
    uint8_t page[128];
    const uint32_t user_low = 0x1c74100u;
    const uint32_t user_high = 0x1c7a000u;
    const uint32_t old_usp = 0x1c79790u;

    memset((void *)&record, 0, sizeof record);
    assert(fm1_fault_c0_stack_window_capture_abi(
        &record, 0u, 21u, 0x01c022b4u, old_usp, user_low, user_high,
        words, FM1_FAULT_C1_TIMEOUT_C0_STACK_VERSION));
    assert(fm1_fault_c0_stack_window_record_valid_abi(
        &record, FM1_FAULT_C1_TIMEOUT_C0_STACK_VERSION, user_low, user_high));
    assert(fm1_fault_c0_stack_window_chunk_abi(
        &record, FM1_FAULT_C1_TIMEOUT_C0_STACK_VERSION,
        user_low, user_high, 0u, sizeof record, page) == sizeof record + 3u);
    assert(memcmp(page + 3u, (const void *)&record, sizeof record) == 0);
}

static void test_capture_and_reader_reject_invalid_cpu_version_and_bounds(void)
{
    volatile fm1_fault_c0_stack_window_record_t record;
    uint32_t words[FM1_FAULT_C1_TIMEOUT_C0_WORDS] = {0};
    uint8_t page[128];
    const uint32_t low = 0x1c7a100u;
    const uint32_t high = 0x1c7c000u;

    memset((void *)&record, 0, sizeof record);
    assert(!fm1_fault_c0_stack_window_capture_abi(
        &record, 1u, 22u, 0u, 0x1c7ba50u, low, high, words,
        FM1_FAULT_C0_SYSTEM_STACK_VERSION));
    assert(record.magic == 0u);
    assert(!fm1_fault_c0_stack_window_capture_abi(
        &record, 0u, 22u, 0u, 0x1c79790u, low, high, words,
        FM1_FAULT_C0_SYSTEM_STACK_VERSION));
    assert(!fm1_fault_c0_stack_window_capture_abi(
        &record, 0u, 22u, 0u, 0x1c7ba50u, low, high, words, 99u));
    assert(!fm1_fault_c0_stack_window_chunk_abi(
        &record, FM1_FAULT_C0_SYSTEM_STACK_VERSION, low, high,
        0u, sizeof record, page));

    /* A system record needs the complete SP+64..SP+100 window. The C1
     * timeout/USP record continues to require its original SP+104..SP+140. */
    assert(fm1_fault_c0_stack_window_capture_abi(
        &record, 0u, 23u, 0u, 0x1c7ba50u, low, 0x1c7bab8u, words,
        FM1_FAULT_C0_SYSTEM_STACK_VERSION));
    assert(record.sample_base == 0x1c7ba50u + 64u);
    assert(!fm1_fault_c0_stack_window_capture_abi(
        &record, 0u, 24u, 0u, 0x1c7ba50u, low, 0x1c7bab7u, words,
        FM1_FAULT_C0_SYSTEM_STACK_VERSION));
    assert(record.magic == 0u);
    assert(fm1_fault_c0_stack_window_capture_abi(
        &record, 0u, 25u, 0u, 0x1c79790u, 0x1c74100u, 0x1c79820u,
        words, FM1_FAULT_C1_TIMEOUT_C0_STACK_VERSION));
    assert(record.sample_base == 0x1c79790u + 104u);
    assert(!fm1_fault_c0_stack_window_capture_abi(
        &record, 0u, 26u, 0u, 0x1c79790u, 0x1c74100u, 0x1c7981fu,
        words, FM1_FAULT_C1_TIMEOUT_C0_STACK_VERSION));
    assert(record.magic == 0u);
}

int main(void)
{
    test_v3_active_sp_record_and_selector4_read();
    test_selector1_can_still_read_old_v1_record();
    test_capture_and_reader_reject_invalid_cpu_version_and_bounds();
    return 0;
}
