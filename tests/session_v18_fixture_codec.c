/* Standalone codec, kept separate from the app's runtime table bindings. */
#include "../firmware/src/instrument_session_v17.h"
int test_migrate_v17_session(uint8_t *record, size_t n)
{
    static br_session_v17_t session;
    if (br_v17_decode(record, (uint32_t)n, &session, 0) != BR_V17_OK) return 0;
    return br_v17_encode(&session, record, (uint32_t)n, 0) == n;
}
