#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static volatile uint32_t test_con, test_cnt, test_prd;
#define FM1_T4_CON test_con
#define FM1_T4_CNT test_cnt
#define FM1_T4_PRD test_prd
#include "../firmware/hal/fm1_time.h"

int main(void)
{
    static const uint32_t periods[] = {0u, 1u, 0xfffffffeu, 0xffffffffu};
    static const uint32_t upper[] = {0u, 0x4000u, 0x8000u, 0x10000u};
    uint32_t low, p, high, snapshot[3];

    /* Exercise the real initialization against every run/source/prescaler
     * byte and both matching and nonmatching periods. */
    for (high = 0u; high < sizeof upper / sizeof upper[0]; ++high) {
        for (p = 0u; p < sizeof periods / sizeof periods[0]; ++p) {
            for (low = 0u; low <= 0xffu; ++low) {
                const uint32_t before_con = upper[high] | low;
                test_con = before_con;
                test_cnt = 0x12345678u;
                test_prd = periods[p];
                fm1_time_init();
                if (low == 9u && periods[p] == 0xffffffffu) {
                    assert(test_con == before_con);
                    assert(test_cnt == 0x12345678u);
                    assert(test_prd == 0xffffffffu);
                } else {
                    assert(test_con == 0x4009u);
                    assert(test_cnt == 0u);
                    assert(test_prd == 0xffffffffu);
                }
            }
        }
    }

    test_cnt = 2400000u;
    assert(fm1_micros() == 100000u);
    fm1_timer4_read_snapshot(snapshot);
    assert(snapshot[0] == test_con && snapshot[1] == test_cnt &&
           snapshot[2] == test_prd);
    puts("TIMER4 startup configuration and preservation: passed");
    return 0;
}
