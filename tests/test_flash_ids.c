/* The storage/update gate must accept only the two supported JEDEC IDs. */
#include <assert.h>
#include <stdint.h>
#include "../firmware/hal/fm1_flash_ids.h"

int main(void)
{
    assert(fl_jedec_known(0x856014u));
    assert(fl_jedec_known(0xB36014u));
    assert(!fl_jedec_known(0u));
    assert(!fl_jedec_known(0xFFFFFFu));
    assert(!fl_jedec_known(0xB36015u));
    assert(!fl_jedec_known(0x856013u));
    assert(!fl_jedec_known(0x01B36014u));
    return 0;
}
