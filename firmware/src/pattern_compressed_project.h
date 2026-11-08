/* Offline prototype. Callers must keep source and every output/work buffer
 * disjoint and immutable where required. All output is disposable on error;
 * never pass the live project. Adopt staged data only after success. */
#ifndef FM1_COMPRESSED_PROJECT_H
#define FM1_COMPRESSED_PROJECT_H
#include "pattern_project.h"
int fm1_compressed_project_decode(const uint8_t *,size_t,fm1_project_session_valid,void *,fm1_project_work *,uint8_t *,br_pattern_bank *,uint8_t *,size_t,br_pattern_snapshot *,fm1_pattern_song *);
#endif
