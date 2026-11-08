/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_PATTERN_LOAN_TOKEN_H
#define FM1_PATTERN_LOAN_TOKEN_H
#include <stdint.h>
/* Single foreground owner only. Sequence exhaustion fails closed rather than
 * recycling an old token; caller must zero-initialize exactly once. */
typedef struct { uint32_t sequence, active; } br_pattern_loan_token;
static inline uint32_t br_pattern_loan_claim(br_pattern_loan_token *t) {
    if(!t || t->active || t->sequence==UINT32_MAX) return 0;
    t->active=++t->sequence; return t->active;
}
static inline int br_pattern_loan_matches(const br_pattern_loan_token *t,uint32_t value) {
    return t && value && t->active==value;
}
static inline int br_pattern_loan_release(br_pattern_loan_token *t,uint32_t value) {
    if(!br_pattern_loan_matches(t,value)) return 0;
    t->active=0; return 1;
}
#endif
