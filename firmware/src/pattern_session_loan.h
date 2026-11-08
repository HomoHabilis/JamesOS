/* Session scratch loan model. Host-only until target ownership is proven. */
#ifndef FM1_PATTERN_SESSION_LOAN_H
#define FM1_PATTERN_SESSION_LOAN_H
#include <stddef.h>
#include <stdint.h>
#define FM1_SESSION_LOAN_LOADED 1u
#define FM1_SESSION_LOAN_PENDING 2u
#define FM1_SESSION_LOAN_UNDO 4u
/* Foreground only. The exclusive predicate must certify stopped/quiescent
 * audio AND block editor, diagnostics, project UI, settings callbacks and all
 * other users of these buffers until release. Borrowing undo is rejected
 * while a valid undo image exists; legacy/invalid save lock and pending async save
 * reject every loan. Caller never owns these buffers past release. */
int br_pattern_session_loan_acquire(unsigned mask,int (*exclusive)(void *),void *context,void **loaded,void **pending,void **undo,size_t *bytes);
void br_pattern_session_loan_release(void);
#endif
