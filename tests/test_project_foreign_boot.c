#include <assert.h>
#include "pattern_boot_recovery.h"

int main(void) {
    br_pattern_boot_recovery s={0};
    assert(br_pattern_boot_should_try(&s,1,100));
    assert(br_pattern_boot_result(&s,100,FM1_PROJECT_FOREIGN_WORKING,0)==1);
    assert(s.done);
    s=(br_pattern_boot_recovery){0};
    assert(br_pattern_boot_result(&s,100,PFS_IO,0)==-2);
    assert(s.done);
    s=(br_pattern_boot_recovery){0};
    assert(br_pattern_boot_result(&s,100,FM1_PROJECT_BUSY,0)==0);
    assert(!s.done);
    assert(!br_pattern_boot_should_try(&s,1,200));
    assert(br_pattern_boot_should_try(&s,1,350));
    return 0;
}
