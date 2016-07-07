/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>
#include <immintrin.h>

#include "./vpx_config.h"
#include "vpx_ports/mem.h"
#include "vpx/vpx_integer.h"

#include "vpx_dsp/x86/synonyms.h"

////////////////////////////////////////////////////////////////////////////////
// 8 bit
////////////////////////////////////////////////////////////////////////////////

static INLINE unsigned int obmc_sad_w4(const uint8_t *a, const int a_stride,
                                       const int32_t *b, const int32_t *m,
                                       const int height) {
  const int a_step = a_stride - 4;
  int n = 0;
  __m128i v_sad_d = _mm_setzero_si128();

  do {
    const __m128i v_a_b = xx_loadl_32(a + n);
    const __m128i v_m_d = xx_load_128(m + n);
    const __m128i v_b_d = xx_load_128(b + n);

    const __m128i v_a_d = _mm_cvtepu8_epi32(v_a_b);

    // Values in both a and m fit in 15 bits, and are packed at 32 bit
    // boundaries. We use pmaddwd, as it has lower latency on Haswell
    // than pmulld but produces the same result with these inputs.
    const __m128i v_am_d = _mm_madd_epi16(v_a_d, v_m_d);

    const __m128i v_diff_d = _mm_sub_epi32(v_b_d, v_am_d);
    const __m128i v_absdiff_d = _mm_abs_epi32(v_diff_d);

    // Rounded absolute difference
    const __m128i v_rad_d = xx_roundn_epu32(v_absdiff_d, 12);

    v_sad_d = _mm_add_epi32(v_sad_d, v_rad_d);

    n += 4;

    if (n % 4 == 0)
      a += a_step;
  } while (n < 4 * height);

  return xx_hsum_epi32_si32(v_sad_d);
}

static INLINE unsigned int obmc_sad_w8n(const uint8_t *a, const int a_stride,
                                        const int32_t *b, const int32_t *m,
                                        const int width, const int height) {
  const int a_step = a_stride - width;
  int n = 0;
  __m128i v_sad_d = _mm_setzero_si128();
  assert(width >= 8 && (width & (width - 1)) == 0);

  do {
    const __m128i v_a1_b = xx_loadl_32(a + n + 4);
    const __m128i v_m1_d = xx_load_128(m + n + 4);
    const __m128i v_b1_d = xx_load_128(b + n + 4);
    const __m128i v_a0_b = xx_loadl_32(a + n);
    const __m128i v_m0_d = xx_load_128(m + n);
    const __m128i v_b0_d = xx_load_128(b + n);

    const __m128i v_a0_d = _mm_cvtepu8_epi32(v_a0_b);
    const __m128i v_a1_d = _mm_cvtepu8_epi32(v_a1_b);

    // Values in both a and m fit in 15 bits, and are packed at 32 bit
    // boundaries. We use pmaddwd, as it has lower latency on Haswell
    // than pmulld but produces the same result with these inputs.
    const __m128i v_am0_d = _mm_madd_epi16(v_a0_d, v_m0_d);
    const __m128i v_am1_d = _mm_madd_epi16(v_a1_d, v_m1_d);

    const __m128i v_diff0_d = _mm_sub_epi32(v_b0_d, v_am0_d);
    const __m128i v_diff1_d = _mm_sub_epi32(v_b1_d, v_am1_d);
    const __m128i v_absdiff0_d = _mm_abs_epi32(v_diff0_d);
    const __m128i v_absdiff1_d = _mm_abs_epi32(v_diff1_d);

    // Rounded absolute difference
    const __m128i v_rad0_d = xx_roundn_epu32(v_absdiff0_d, 12);
    const __m128i v_rad1_d = xx_roundn_epu32(v_absdiff1_d, 12);

    v_sad_d = _mm_add_epi32(v_sad_d, v_rad0_d);
    v_sad_d = _mm_add_epi32(v_sad_d, v_rad1_d);

    n += 8;

    if (n % width == 0)
      a += a_step;
  } while (n < width * height);

  return xx_hsum_epi32_si32(v_sad_d);
}

#define OBMCSADWXH(w, h)                                                      \
unsigned int vpx_obmc_sad##w##x##h##_sse4_1(const uint8_t *ref,               \
                                            int ref_stride,                   \
                                            const int32_t *wsrc,              \
                                            const int32_t *msk) {             \
  if (w == 4)                                                                 \
    return obmc_sad_w4(ref, ref_stride, wsrc, msk, h);                        \
  else                                                                        \
    return obmc_sad_w8n(ref, ref_stride, wsrc, msk, w, h);                    \
}

#if CONFIG_EXT_PARTITION
OBMCSADWXH(128, 128)
OBMCSADWXH(128, 64)
OBMCSADWXH(64, 128)
#endif  // CONFIG_EXT_PARTITION
OBMCSADWXH(64, 64)
OBMCSADWXH(64, 32)
OBMCSADWXH(32, 64)
OBMCSADWXH(32, 32)
OBMCSADWXH(32, 16)
OBMCSADWXH(16, 32)
OBMCSADWXH(16, 16)
OBMCSADWXH(16, 8)
OBMCSADWXH(8, 16)
OBMCSADWXH(8, 8)
OBMCSADWXH(8, 4)
OBMCSADWXH(4, 8)
OBMCSADWXH(4, 4)

////////////////////////////////////////////////////////////////////////////////
// High bit-depth
////////////////////////////////////////////////////////////////////////////////

#if CONFIG_VP9_HIGHBITDEPTH
static INLINE unsigned int hbd_obmc_sad_w4(const uint8_t *a8,
                                           const int a_stride,
                                           const int32_t *b, const int32_t *m,
                                           const int height) {
  const uint16_t *a = CONVERT_TO_SHORTPTR(a8);
  const int a_step = a_stride - 4;
  int n = 0;
  __m128i v_sad_d = _mm_setzero_si128();

  do {
    const __m128i v_a_w = xx_loadl_64(a + n);
    const __m128i v_m_d = xx_load_128(m + n);
    const __m128i v_b_d = xx_load_128(b + n);

    const __m128i v_a_d = _mm_cvtepu16_epi32(v_a_w);

    // Values in both a and m fit in 15 bits, and are packed at 32 bit
    // boundaries. We use pmaddwd, as it has lower latency on Haswell
    // than pmulld but produces the same result with these inputs.
    const __m128i v_am_d = _mm_madd_epi16(v_a_d, v_m_d);

    const __m128i v_diff_d = _mm_sub_epi32(v_b_d, v_am_d);
    const __m128i v_absdiff_d = _mm_abs_epi32(v_diff_d);

    // Rounded absolute difference
    const __m128i v_rad_d = xx_roundn_epu32(v_absdiff_d, 12);

    v_sad_d = _mm_add_epi32(v_sad_d, v_rad_d);

    n += 4;

    if (n % 4 == 0)
      a += a_step;
  } while (n < 4 * height);

  return xx_hsum_epi32_si32(v_sad_d);
}

static INLINE unsigned int hbd_obmc_sad_w8n(const uint8_t *a8,
                                            const int a_stride,
                                            const int32_t *b, const int32_t *m,
                                            const int width, const int height) {
  const uint16_t *a = CONVERT_TO_SHORTPTR(a8);
  const int a_step = a_stride - width;
  int n = 0;
  __m128i v_sad_d = _mm_setzero_si128();
  assert(width >= 8 && (width & (width - 1)) == 0);

  do {
    const __m128i v_a1_w = xx_loadl_64(a + n + 4);
    const __m128i v_m1_d = xx_load_128(m + n + 4);
    const __m128i v_b1_d = xx_load_128(b + n + 4);
    const __m128i v_a0_w = xx_loadl_64(a + n);
    const __m128i v_m0_d = xx_load_128(m + n);
    const __m128i v_b0_d = xx_load_128(b + n);

    const __m128i v_a0_d = _mm_cvtepu16_epi32(v_a0_w);
    const __m128i v_a1_d = _mm_cvtepu16_epi32(v_a1_w);

    // Values in both a and m fit in 15 bits, and are packed at 32 bit
    // boundaries. We use pmaddwd, as it has lower latency on Haswell
    // than pmulld but produces the same result with these inputs.
    const __m128i v_am0_d = _mm_madd_epi16(v_a0_d, v_m0_d);
    const __m128i v_am1_d = _mm_madd_epi16(v_a1_d, v_m1_d);

    const __m128i v_diff0_d = _mm_sub_epi32(v_b0_d, v_am0_d);
    const __m128i v_diff1_d = _mm_sub_epi32(v_b1_d, v_am1_d);
    const __m128i v_absdiff0_d = _mm_abs_epi32(v_diff0_d);
    const __m128i v_absdiff1_d = _mm_abs_epi32(v_diff1_d);

    // Rounded absolute difference
    const __m128i v_rad0_d = xx_roundn_epu32(v_absdiff0_d, 12);
    const __m128i v_rad1_d = xx_roundn_epu32(v_absdiff1_d, 12);

    v_sad_d = _mm_add_epi32(v_sad_d, v_rad0_d);
    v_sad_d = _mm_add_epi32(v_sad_d, v_rad1_d);

    n += 8;

    if (n % width == 0)
      a += a_step;
  } while (n < width * height);

  return xx_hsum_epi32_si32(v_sad_d);
}

#define HBD_OBMCSADWXH(w, h)                                                  \
unsigned int vpx_highbd_obmc_sad##w##x##h##_sse4_1(const uint8_t *ref,        \
                                                   int ref_stride,            \
                                                   const int32_t *wsrc,       \
                                                   const int32_t *msk) {      \
  if (w == 4)                                                                 \
    return hbd_obmc_sad_w4(ref, ref_stride, wsrc, msk, h);                    \
  else                                                                        \
    return hbd_obmc_sad_w8n(ref, ref_stride, wsrc, msk, w, h);                \
}

#if CONFIG_EXT_PARTITION
HBD_OBMCSADWXH(128, 128)
HBD_OBMCSADWXH(128, 64)
HBD_OBMCSADWXH(64, 128)
#endif  // CONFIG_EXT_PARTITION
HBD_OBMCSADWXH(64, 64)
HBD_OBMCSADWXH(64, 32)
HBD_OBMCSADWXH(32, 64)
HBD_OBMCSADWXH(32, 32)
HBD_OBMCSADWXH(32, 16)
HBD_OBMCSADWXH(16, 32)
HBD_OBMCSADWXH(16, 16)
HBD_OBMCSADWXH(16, 8)
HBD_OBMCSADWXH(8, 16)
HBD_OBMCSADWXH(8, 8)
HBD_OBMCSADWXH(8, 4)
HBD_OBMCSADWXH(4, 8)
HBD_OBMCSADWXH(4, 4)
#endif  // CONFIG_VP9_HIGHBITDEPTH