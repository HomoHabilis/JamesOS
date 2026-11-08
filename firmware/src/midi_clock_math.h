#ifndef FM1_MIDI_CLOCK_MATH_H
#define FM1_MIDI_CLOCK_MATH_H
#include <stdint.h>
/* Bounded, allocation-free unsigned division, with no compiler 64-bit
 * division helpers. A zero divisor returns UINT64_MAX. At most 64 shifts and
 * 64 subtract/compare steps; target audio cost still requires measurement. */
static uint64_t fm1_clock_div64(uint64_t numerator, uint64_t denominator, uint64_t *remainder)
{
    uint64_t divisor = denominator, bit = 1u, quotient = 0u;
    if (!denominator) { if (remainder) *remainder = numerator; return UINT64_MAX; }
    while (divisor <= (numerator >> 1) && divisor <= (UINT64_MAX >> 1)) {
        divisor <<= 1; bit <<= 1;
    }
    while (bit) {
        if (numerator >= divisor) { numerator -= divisor; quotient |= bit; }
        divisor >>= 1; bit >>= 1;
    }
    if (remainder) *remainder = numerator;
    return quotient;
}
#endif
