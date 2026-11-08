/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PLAITS_WAVETABLE_PACK_H_
#define PLAITS_WAVETABLE_PACK_H_

#include <stdint.h>

/* One selector shared by the resources translation unit and platform C. */
#ifndef FM1_PLAITS_PACK_WAVES
#define FM1_PLAITS_PACK_WAVES 1
#endif

/* Decode the long sine lookup table at boot from its exact quarter-wave form. */
#ifndef FM1_PLAITS_PACK_SINE
#define FM1_PLAITS_PACK_SINE 1
#endif

/* Reconstruct FM SysEx banks in their original direct-access symbols at boot. */
#ifndef FM1_PLAITS_PACK_SYX
#define FM1_PLAITS_PACK_SYX 1
#endif

/* Place only the reconstructed SysEx backing arrays in zeroed .bss. */
#ifndef FM1_PLAITS_SYX_DECODE_TO_BSS
#define FM1_PLAITS_SYX_DECODE_TO_BSS 1
#endif

/* Keep decoder scratch off .pool when this boot-only stack option is enabled. */
#ifndef FM1_PLAITS_WAVES_STACK_SCRATCH
#define FM1_PLAITS_WAVES_STACK_SCRATCH 1
#endif

/* Optional placement for the boot-only delta decoder scratch. When enabled,
 * the scratch is zero-initialized in .bss rather than consuming the runtime
 * audio .pool. The waveform initializer overwrites each decoded block before
 * reading it and completes before CPU1 starts. */
#ifndef FM1_PLAITS_WAVES_BOOT_SCRATCH_BSS
#define FM1_PLAITS_WAVES_BOOT_SCRATCH_BSS 0
#endif

/* Keep boot-only resource reconstruction small without changing DSP codegen. */
#ifndef FM1_PLAITS_RESOURCE_INIT_MINSIZE
#define FM1_PLAITS_RESOURCE_INIT_MINSIZE 1
#endif

#if FM1_PLAITS_RESOURCE_INIT_MINSIZE
#if defined(__clang__)
#define FM1_PLAITS_RESOURCE_INIT_ATTR __attribute__((minsize, noinline))
#elif defined(__GNUC__)
#define FM1_PLAITS_RESOURCE_INIT_ATTR __attribute__((optimize("Os"), noinline))
#else
#define FM1_PLAITS_RESOURCE_INIT_ATTR
#endif
#else
#define FM1_PLAITS_RESOURCE_INIT_ATTR
#endif

#if FM1_PLAITS_PACK_SINE && !FM1_PLAITS_PACK_WAVES
#error "FM1_PLAITS_PACK_SINE requires the existing pre-CPU1 wave init"
#endif
#if FM1_PLAITS_PACK_SYX && !FM1_PLAITS_PACK_WAVES
#error "FM1_PLAITS_PACK_SYX requires the existing pre-CPU1 wave init"
#endif
#if FM1_PLAITS_SYX_DECODE_TO_BSS != 0 && FM1_PLAITS_SYX_DECODE_TO_BSS != 1
#error "FM1_PLAITS_SYX_DECODE_TO_BSS must be zero or one"
#endif
#if FM1_PLAITS_SYX_DECODE_TO_BSS && !FM1_PLAITS_PACK_SYX
#error "FM1_PLAITS_SYX_DECODE_TO_BSS requires packed SysEx banks"
#endif
#if FM1_PLAITS_WAVES_STACK_SCRATCH && !FM1_PLAITS_PACK_WAVES
#error "FM1_PLAITS_WAVES_STACK_SCRATCH requires packed waveform decoding"
#endif
#if FM1_PLAITS_WAVES_BOOT_SCRATCH_BSS != 0 && \
    FM1_PLAITS_WAVES_BOOT_SCRATCH_BSS != 1
#error "FM1_PLAITS_WAVES_BOOT_SCRATCH_BSS must be zero or one"
#endif
#if FM1_PLAITS_WAVES_BOOT_SCRATCH_BSS && \
    (!FM1_PLAITS_PACK_WAVES || FM1_PLAITS_WAVES_STACK_SCRATCH)
#error "BSS waveform scratch requires packed waves and stack scratch disabled"
#endif

#define FM1_PLAITS_WAVE_SAMPLE_COUNT 25344u
#define FM1_PLAITS_WAVE_BLOCK_SAMPLES 4096u
#define FM1_PLAITS_WAVE_BLOCK_COUNT 7u

#if FM1_PLAITS_PACK_WAVES
/* Call after .pool has been cleared and before CPU1 is started. */
#ifdef __cplusplus
extern "C" {
#endif
int fm1_plaits_waves_init(uint32_t *crc32_out);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace fm1_plaits_wavepack {

/* Bounded decoder for a single raw LZ4 block (no frame header). */
static inline FM1_PLAITS_RESOURCE_INIT_ATTR bool lz4_decompress_block(
    const uint8_t *src, uint32_t src_size,
    uint8_t *dst, uint32_t dst_capacity, uint32_t *dst_size_out) {
  uint32_t ip = 0;
  uint32_t op = 0;
  if (!src || !dst || !dst_size_out) return false;
  while (ip < src_size) {
    const uint8_t token = src[ip++];
    uint32_t literals = token >> 4;
    if (literals == 15u) {
      uint8_t extension;
      do {
        if (ip >= src_size) return false;
        extension = src[ip++];
        if (literals > UINT32_MAX - extension) return false;
        literals += extension;
      } while (extension == 255u);
    }
    if (literals > src_size - ip || literals > dst_capacity - op) return false;
    for (uint32_t i = 0; i < literals; ++i) dst[op + i] = src[ip + i];
    ip += literals;
    op += literals;
    if (ip == src_size) {
      *dst_size_out = op;
      return true;
    }
    if (src_size - ip < 2u) return false;
    const uint32_t offset = (uint32_t)src[ip] | ((uint32_t)src[ip + 1] << 8);
    ip += 2u;
    if (offset == 0u || offset > op) return false;
    uint32_t match = token & 15u;
    if (match == 15u) {
      uint8_t extension;
      do {
        if (ip >= src_size) return false;
        extension = src[ip++];
        if (match > UINT32_MAX - extension) return false;
        match += extension;
      } while (extension == 255u);
    }
    if (match > UINT32_MAX - 4u) return false;
    match += 4u;
    if (match > dst_capacity - op) return false;
    for (uint32_t i = 0; i < match; ++i) dst[op + i] = dst[op + i - offset];
    op += match;
  }
  return false;
}

static inline int16_t bits_to_i16(uint16_t bits) {
  return bits < 0x8000u ? (int16_t)bits : (int16_t)((int32_t)bits - 65536);
}

/* Decode one block of zigzag-varint deltas into signed 16-bit samples. */
static inline FM1_PLAITS_RESOURCE_INIT_ATTR bool decode_delta_block(
    const uint8_t *src, uint32_t src_size,
    int16_t *dst, uint32_t sample_count) {
  uint32_t ip = 0;
  uint16_t previous = 0;
  if (!src || !dst || sample_count == 0u) return false;
  for (uint32_t n = 0; n < sample_count; ++n) {
    uint32_t value = 0;
    uint32_t shift = 0;
    bool terminated = false;
    for (uint32_t byte_index = 0; byte_index < 3u; ++byte_index) {
      if (ip >= src_size) return false;
      const uint8_t byte = src[ip++];
      value |= (uint32_t)(byte & 0x7fu) << shift;
      if ((byte & 0x80u) == 0u) {
        terminated = true;
        break;
      }
      shift += 7u;
    }
    if (!terminated || value > 65535u) return false;
    const int32_t signed_value = (value & 1u)
        ? -(int32_t)((value + 1u) >> 1)
        : (int32_t)(value >> 1);
    if (n == 0u) {
      if (signed_value < -32768 || signed_value > 32767) return false;
      previous = (uint16_t)signed_value;
    } else {
      previous = (uint16_t)(previous + (uint16_t)signed_value);
    }
    dst[n] = bits_to_i16(previous);
  }
  return ip == src_size;
}

static inline FM1_PLAITS_RESOURCE_INIT_ATTR uint32_t crc32_sample(
    uint32_t crc, int16_t sample) {
  const uint16_t bits = (uint16_t)sample;
  const uint8_t bytes[2] = {(uint8_t)bits, (uint8_t)(bits >> 8)};
  for (uint32_t b = 0; b < 2u; ++b) {
    crc ^= bytes[b];
    for (uint32_t bit = 0; bit < 8u; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  return crc;
}

}  // namespace fm1_plaits_wavepack
#endif  // __cplusplus
#endif  // FM1_PLAITS_PACK_WAVES

#endif  // PLAITS_WAVETABLE_PACK_H_
