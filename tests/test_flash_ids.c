/* The storage/update gate must accept only the identified JEDEC IDs of the product:
 * FM-1 0x856014 and 0xB36014; SMK-37 Pro 0xEB6014 and 0x856014 (not 0xB36014). */
#include <assert.h>
#include <stdint.h>
#include "host_product.h"
#include "../firmware/hal/fm1_flash_ids.h"

int main(void)
{
#if BR_PRODUCT_SMK37
    assert(fl_jedec_known(0xEB6014u));
    assert(fl_jedec_known(0x856014u));
    assert(!fl_jedec_known(0xB36014u));
#else
    assert(fl_jedec_known(0x856014u));
    assert(fl_jedec_known(0xB36014u));
    assert(!fl_jedec_known(0xEB6014u));
#endif
    assert(!fl_jedec_known(0u));
    assert(!fl_jedec_known(0xFFFFFFu));
    assert(!fl_jedec_known(0xB36015u));
    assert(!fl_jedec_known(0x856013u));
    assert(!fl_jedec_known(0x01B36014u));
    assert(!fl_jedec_known(0x01EB6014u));
    return 0;
}
