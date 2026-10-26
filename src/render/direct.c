/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Shape CPU.
 *
 * Ghoti.io Shape CPU is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Shape CPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * The direct 8-bit compositor of the fast stroke coverage, and the batch that spreads it over threads.
 *
 * The compositor takes the runs of coverage of one stroke and blends a solid premultiplied source into the target's bytes,
 * source-over in integers, each step rounded to 8 bits as the rasterizers it is compared with round it.
 *
 * The batch holds the strokes of a render until it is full or the render ends, each as a copy of the points of its outline, and
 * then draws them over stripes of the target's rows, a stripe to a task at a time: every stripe draws the strokes that reach it
 * in the order they were queued, and the sums an edge leaves in a row depend on the row alone (lean.c), so the picture is the
 * same bytes for any number of tasks.  A stripe is written by one task; the tasks share the queue and nothing else.  The tasks
 * are run by the caller's executor (render.h): this file makes no thread.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/cutil/atomic.h>

#include <stdlib.h>
#include <string.h>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__x86_64__) && defined(__GNUC__)
#include <immintrin.h>
#define D8_AVX2 1
#endif

/* --- the compositor -------------------------------------------------------------------------------- */

#define D8_LOW 0x00ff00ff00ff00ffull
#define D8_ROUND 0x0080008000800080ull

/** `x / 255` rounded, in each of four 16-bit lanes holding at most 255 * 255. */
static inline uint64_t d8_div255(uint64_t x) {
  const uint64_t t = x + D8_ROUND;
  return ((t + ((t >> 8) & D8_LOW)) >> 8) & D8_LOW;
}

void gshcpu_direct8_color(GSHCPU_Direct8 * d, const uint8_t c[4]) {
  memcpy(d->bytes, c, 4);
  d->lanes = (uint64_t)c[0] | ((uint64_t)c[1] << 16) | ((uint64_t)c[2] << 32) | ((uint64_t)c[3] << 48);
  d->opaque = c[3] == 255;
}

#if defined(__SSE2__)
/** `x / 255` rounded, in each of eight 16-bit lanes holding at most 255 * 255: the scalar step's arithmetic. */
static inline __m128i d8_div255_v(__m128i x) {
  const __m128i t = _mm_add_epi16(x, _mm_set1_epi16(0x80));
  return _mm_srli_epi16(_mm_add_epi16(t, _mm_srli_epi16(t, 8)), 8);
}

/** Four pixels of @p row composed under the coverages @p m (four bytes): the scalar step, a pixel to each half-register. */
static inline void d8_four(unsigned char * row, const uint8_t * m, __m128i src) {
  const __m128i zero = _mm_setzero_si128();
  uint32_t m4;
  memcpy(&m4, m, sizeof m4);
  const __m128i mw = _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)m4), zero); /* m0 m1 m2 m3 in 16 bits */
  const __m128i mm = _mm_unpacklo_epi16(mw, mw);
  const __m128i m_lo = _mm_unpacklo_epi32(mm, mm); /* m0 four times, m1 four times */
  const __m128i m_hi = _mm_unpackhi_epi32(mm, mm);
  const __m128i px = _mm_loadu_si128((const __m128i *)(const void *)row);
  const __m128i e_lo = _mm_unpacklo_epi8(px, zero);
  const __m128i e_hi = _mm_unpackhi_epi8(px, zero);
  const __m128i w_lo = d8_div255_v(_mm_mullo_epi16(src, m_lo)); /* the source weighed by the coverage */
  const __m128i w_hi = d8_div255_v(_mm_mullo_epi16(src, m_hi));
  const __m128i k255 = _mm_set1_epi16(255);
  /* the target's share: one minus the weighed alpha (lane 3 of each pixel, to all four lanes of it) */
  const __m128i keep_lo = _mm_sub_epi16(k255, _mm_shufflehi_epi16(_mm_shufflelo_epi16(w_lo, 0xff), 0xff));
  const __m128i keep_hi = _mm_sub_epi16(k255, _mm_shufflehi_epi16(_mm_shufflelo_epi16(w_hi, 0xff), 0xff));
  __m128i r_lo = _mm_add_epi16(d8_div255_v(_mm_mullo_epi16(e_lo, keep_lo)), w_lo);
  __m128i r_hi = _mm_add_epi16(d8_div255_v(_mm_mullo_epi16(e_hi, keep_hi)), w_hi);
  r_lo = _mm_min_epi16(r_lo, k255); /* a lane past 255: the two roundings added up */
  r_hi = _mm_min_epi16(r_hi, k255);
  _mm_storeu_si128((__m128i *)(void *)row, _mm_packus_epi16(r_lo, r_hi));
}
#endif


#if defined(D8_AVX2)
/** Whether the vector kernels may be used: a test turns them off to compare them with their scalar twins. */
static int d8_vector_on = 1;

/** Whether this processor has AVX2 and the vector kernels are on, asked once (a race on it only repeats the question). */
static int d8_have_avx2(void) {
  return __builtin_cpu_supports("avx2") && d8_vector_on; /* the answer is made before main: a read, no race */
}

/**
 * Eight pixels at a time: the same arithmetic as d8_four, a pixel to each half of a half-register in each of the two lanes.  The
 * unpacks and the pack work inside a lane, so lane 0 holds pixels 0 to 3 and lane 1 pixels 4 to 7, in the order they are stored.
 * @return The pixels done, a multiple of eight.
 */
__attribute__((target("avx2"))) static size_t d8_avx2_blocks(unsigned char * row, const uint8_t * m8, size_t n, uint64_t lanes) {
  const __m256i zero = _mm256_setzero_si256();
  const __m256i k255 = _mm256_set1_epi16(255);
  const __m256i src = _mm256_set1_epi64x((long long)lanes);
  size_t i = 0;
  for (; i + 8u <= n; i += 8, row += 32) {
    uint64_t m;
    memcpy(&m, m8 + i, sizeof m);
    const __m128i w = _mm_unpacklo_epi8(_mm_cvtsi64_si128((long long)m), _mm_setzero_si128()); /* the eight coverages, 16 bits each */
    const __m256i x = _mm256_inserti128_si256(_mm256_castsi128_si256(w), _mm_srli_si128(w, 8), 1); /* lane 1 starts at the fifth */
    const __m256i mm = _mm256_unpacklo_epi16(x, x);
    const __m256i m_lo = _mm256_unpacklo_epi32(mm, mm);
    const __m256i m_hi = _mm256_unpackhi_epi32(mm, mm);
    const __m256i px = _mm256_loadu_si256((const __m256i *)(const void *)row);
    const __m256i e_lo = _mm256_unpacklo_epi8(px, zero);
    const __m256i e_hi = _mm256_unpackhi_epi8(px, zero);
    __m256i t = _mm256_add_epi16(_mm256_mullo_epi16(src, m_lo), _mm256_set1_epi16(0x80));
    const __m256i w_lo = _mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8);
    t = _mm256_add_epi16(_mm256_mullo_epi16(src, m_hi), _mm256_set1_epi16(0x80));
    const __m256i w_hi = _mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8);
    const __m256i keep_lo = _mm256_sub_epi16(k255, _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(w_lo, 0xff), 0xff));
    const __m256i keep_hi = _mm256_sub_epi16(k255, _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(w_hi, 0xff), 0xff));
    t = _mm256_add_epi16(_mm256_mullo_epi16(e_lo, keep_lo), _mm256_set1_epi16(0x80));
    __m256i r_lo = _mm256_add_epi16(_mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8), w_lo);
    t = _mm256_add_epi16(_mm256_mullo_epi16(e_hi, keep_hi), _mm256_set1_epi16(0x80));
    __m256i r_hi = _mm256_add_epi16(_mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8), w_hi);
    r_lo = _mm256_min_epi16(r_lo, k255);
    r_hi = _mm256_min_epi16(r_hi, k255);
    _mm256_storeu_si256((__m256i *)(void *)row, _mm256_packus_epi16(r_lo, r_hi));
  }
  return i;
}
#endif

void gshcpu_direct8_vector(int enable) {
#if defined(D8_AVX2)
  d8_vector_on = enable;
#else
  (void)enable;
#endif
}

void gshcpu_direct8_span(void * ctx, int64_t y, int64_t x, size_t n, const uint8_t * m8) {
  const GSHCPU_Direct8 * d = ctx;
  unsigned char * row = d->pixels + (size_t)(y + d->dy) * d->stride + 4u * (size_t)(x + d->dx);
  /* the next row of the stroke is near this one's columns, and the target is far larger than the cache: ask for it now */
  __builtin_prefetch(row + d->stride, 1, 3);
  __builtin_prefetch(row + d->stride + 4u * (n - 1u), 1, 3);
  size_t i = 0;
#if defined(__SSE2__)
  const __m128i src = _mm_set_epi64x((long long)d->lanes, (long long)d->lanes);
#if defined(D8_AVX2)
  if (n >= 8u && d8_have_avx2()) {
    i = d8_avx2_blocks(row, m8, n, d->lanes);
    row += 4u * i;
  }
#endif
  for (; i + 4u <= n; i += 4, row += 16) {
    d8_four(row, m8 + i, src);
  }
  if (i + 2u <= n) {
    /* two or three pixels left: two in a half-register (a pixel of no coverage is left as it is), and one more below */
    const __m128i zero = _mm_setzero_si128();
    uint16_t m2;
    memcpy(&m2, m8 + i, sizeof m2);
    const __m128i mw = _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)m2), zero);
    const __m128i mm = _mm_unpacklo_epi16(mw, mw);
    const __m128i m_lo = _mm_unpacklo_epi32(mm, mm);
    const __m128i px = _mm_loadl_epi64((const __m128i *)(const void *)row);
    const __m128i e_lo = _mm_unpacklo_epi8(px, zero);
    const __m128i w_lo = d8_div255_v(_mm_mullo_epi16(src, m_lo));
    const __m128i keep_lo = _mm_sub_epi16(_mm_set1_epi16(255), _mm_shufflehi_epi16(_mm_shufflelo_epi16(w_lo, 0xff), 0xff));
    __m128i r_lo = _mm_add_epi16(d8_div255_v(_mm_mullo_epi16(e_lo, keep_lo)), w_lo);
    r_lo = _mm_min_epi16(r_lo, _mm_set1_epi16(255));
    _mm_storel_epi64((__m128i *)(void *)row, _mm_packus_epi16(r_lo, r_lo));
    i += 2;
    row += 8;
  }
#endif
  for (; i < n; i++, row += 4) {
    const uint32_t m = m8[i];
    if (m == 0) {
      continue;
    }
    if (m == 255u && d->opaque) {
      row[0] = d->bytes[0];
      row[1] = d->bytes[1];
      row[2] = d->bytes[2];
      row[3] = d->bytes[3];
      continue;
    }
    const uint64_t e = (uint64_t)row[0] | ((uint64_t)row[1] << 16) | ((uint64_t)row[2] << 32) | ((uint64_t)row[3] << 48);
    const uint64_t w = m == 255u ? d->lanes : d8_div255(d->lanes * m); /* the source weighed by the coverage */
    const uint64_t keep = 255u - (w >> 48); /* the target's share: one minus the weighed alpha */
    uint64_t r = d8_div255(e * keep) + w;
    const uint64_t over = (r >> 8) & 0x0001000100010001ull; /* a lane past 255: the two roundings added up */
    r = (r | (over * 255u)) & D8_LOW;
    row[0] = (uint8_t)r;
    row[1] = (uint8_t)(r >> 16);
    row[2] = (uint8_t)(r >> 32);
    row[3] = (uint8_t)(r >> 48);
  }
}

/* --- sources that differ from pixel to pixel ---------------------------------------------------------- */

#if defined(__SSE2__)
/** Four pixels of @p row composed with the four premultiplied pixels @p src under the coverages @p m (four bytes, or all @p m_const). */
static inline void d8_four_src(unsigned char * row, const uint32_t * src, const uint8_t * m, uint32_t m_const) {
  const __m128i zero = _mm_setzero_si128();
  __m128i m_lo;
  __m128i m_hi;
  if (m) {
    uint32_t m4;
    memcpy(&m4, m, sizeof m4);
    const __m128i mw = _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)m4), zero);
    const __m128i mm = _mm_unpacklo_epi16(mw, mw);
    m_lo = _mm_unpacklo_epi32(mm, mm);
    m_hi = _mm_unpackhi_epi32(mm, mm);
  } else {
    m_lo = m_hi = _mm_set1_epi16((short)m_const);
  }
  const __m128i px = _mm_loadu_si128((const __m128i *)(const void *)row);
  const __m128i sp = _mm_loadu_si128((const __m128i *)(const void *)src);
  const __m128i e_lo = _mm_unpacklo_epi8(px, zero);
  const __m128i e_hi = _mm_unpackhi_epi8(px, zero);
  const __m128i w_lo = d8_div255_v(_mm_mullo_epi16(_mm_unpacklo_epi8(sp, zero), m_lo));
  const __m128i w_hi = d8_div255_v(_mm_mullo_epi16(_mm_unpackhi_epi8(sp, zero), m_hi));
  const __m128i k255 = _mm_set1_epi16(255);
  const __m128i keep_lo = _mm_sub_epi16(k255, _mm_shufflehi_epi16(_mm_shufflelo_epi16(w_lo, 0xff), 0xff));
  const __m128i keep_hi = _mm_sub_epi16(k255, _mm_shufflehi_epi16(_mm_shufflelo_epi16(w_hi, 0xff), 0xff));
  __m128i r_lo = _mm_add_epi16(d8_div255_v(_mm_mullo_epi16(e_lo, keep_lo)), w_lo);
  __m128i r_hi = _mm_add_epi16(d8_div255_v(_mm_mullo_epi16(e_hi, keep_hi)), w_hi);
  r_lo = _mm_min_epi16(r_lo, k255);
  r_hi = _mm_min_epi16(r_hi, k255);
  _mm_storeu_si128((__m128i *)(void *)row, _mm_packus_epi16(r_lo, r_hi));
}
#endif

#if defined(D8_AVX2)
/**
 * Eight pixels at a time, as d8_four_src: the pixels done, a multiple of eight.  The sources are @p src, or the entries of the
 * byte table @p table (four bytes each) at @p index, a pixel whose index is all ones having none.  Eight pixels of full
 * coverage and an opaque source are stored as they are.
 */
__attribute__((target("avx2"))) static size_t d8_avx2_src_blocks(unsigned char * row, const uint32_t * src, const uint8_t * table,
    const uint32_t * index, const uint8_t * m8, uint32_t m_const, size_t n) {
  const __m256i zero = _mm256_setzero_si256();
  const __m256i k255 = _mm256_set1_epi16(255);
  const __m256i k80 = _mm256_set1_epi16(0x80);
  size_t i = 0;
  for (; i + 8u <= n; i += 8, row += 32) {
    __m256i sp;
    if (src) {
      sp = _mm256_loadu_si256((const __m256i *)(const void *)(src + i));
    } else {
      const __m256i idx = _mm256_loadu_si256((const __m256i *)(const void *)(index + i));
      const __m256i none = _mm256_cmpeq_epi32(idx, _mm256_set1_epi32(-1));
      sp = _mm256_andnot_si256(none, _mm256_i32gather_epi32((const int *)(const void *)table, _mm256_andnot_si256(none, idx), 4));
    }
    __m256i m_lo;
    __m256i m_hi;
    if (m8) {
      uint64_t m;
      memcpy(&m, m8 + i, sizeof m);
      if (m == 0xffffffffffffffffull &&
          _mm256_testc_si256(_mm256_cmpeq_epi32(_mm256_srli_epi32(sp, 24), _mm256_set1_epi32(255)), _mm256_set1_epi32(-1))) {
        _mm256_storeu_si256((__m256i *)(void *)row, sp); /* full coverage of opaque sources */
        continue;
      }
      const __m128i w = _mm_unpacklo_epi8(_mm_cvtsi64_si128((long long)m), _mm_setzero_si128());
      const __m256i x = _mm256_inserti128_si256(_mm256_castsi128_si256(w), _mm_srli_si128(w, 8), 1);
      const __m256i mm = _mm256_unpacklo_epi16(x, x);
      m_lo = _mm256_unpacklo_epi32(mm, mm);
      m_hi = _mm256_unpackhi_epi32(mm, mm);
    } else {
      m_lo = m_hi = _mm256_set1_epi16((short)m_const);
    }
    const __m256i px = _mm256_loadu_si256((const __m256i *)(const void *)row);
    const __m256i e_lo = _mm256_unpacklo_epi8(px, zero);
    const __m256i e_hi = _mm256_unpackhi_epi8(px, zero);
    __m256i t = _mm256_add_epi16(_mm256_mullo_epi16(_mm256_unpacklo_epi8(sp, zero), m_lo), k80);
    const __m256i w_lo = _mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8);
    t = _mm256_add_epi16(_mm256_mullo_epi16(_mm256_unpackhi_epi8(sp, zero), m_hi), k80);
    const __m256i w_hi = _mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8);
    const __m256i keep_lo = _mm256_sub_epi16(k255, _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(w_lo, 0xff), 0xff));
    const __m256i keep_hi = _mm256_sub_epi16(k255, _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(w_hi, 0xff), 0xff));
    t = _mm256_add_epi16(_mm256_mullo_epi16(e_lo, keep_lo), k80);
    __m256i r_lo = _mm256_add_epi16(_mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8), w_lo);
    t = _mm256_add_epi16(_mm256_mullo_epi16(e_hi, keep_hi), k80);
    __m256i r_hi = _mm256_add_epi16(_mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8), w_hi);
    r_lo = _mm256_min_epi16(r_lo, k255);
    r_hi = _mm256_min_epi16(r_hi, k255);
    _mm256_storeu_si256((__m256i *)(void *)row, _mm256_packus_epi16(r_lo, r_hi));
  }
  return i;
}
#endif

/** The four bytes at @p p as four 16-bit lanes (the first byte lowest). */
static inline uint64_t d8_lanes_of(const uint8_t * p) {
  return (uint64_t)p[0] | ((uint64_t)p[1] << 16) | ((uint64_t)p[2] << 32) | ((uint64_t)p[3] << 48);
}

void gshcpu_direct8_span_pixels(unsigned char * row, const uint32_t * src, const uint8_t * m8, uint32_t m_const, size_t n) {
  size_t i = 0;
#if defined(D8_AVX2)
  if (n >= 8u && d8_have_avx2()) {
    i = d8_avx2_src_blocks(row, src, NULL, NULL, m8, m_const, n);
    row += 4u * i;
  }
#endif
#if defined(__SSE2__)
  for (; i + 4u <= n; i += 4, row += 16) {
    d8_four_src(row, src + i, m8 ? m8 + i : NULL, m_const);
  }
#endif
  for (; i < n; i++, row += 4) {
    const uint32_t m = m8 ? m8[i] : m_const;
    const uint8_t * sb = (const uint8_t *)(const void *)(src + i);
    if (m == 0 || sb[3] == 0) {
      continue;
    }
    if (m == 255u && sb[3] == 255u) {
      memcpy(row, sb, 4);
      continue;
    }
    const uint64_t e = d8_lanes_of(row);
    const uint64_t sl = d8_lanes_of(sb);
    const uint64_t w = m == 255u ? sl : d8_div255(sl * m);
    const uint64_t keep = 255u - (w >> 48);
    uint64_t r = d8_div255(e * keep) + w;
    const uint64_t over = (r >> 8) & 0x0001000100010001ull;
    r = (r | (over * 255u)) & D8_LOW;
    row[0] = (uint8_t)r;
    row[1] = (uint8_t)(r >> 16);
    row[2] = (uint8_t)(r >> 32);
    row[3] = (uint8_t)(r >> 48);
  }
}

void gshcpu_direct8_span_table(unsigned char * row, const uint8_t * table, const uint32_t * index, const uint8_t * m8, size_t n) {
  size_t i = 0;
#if defined(D8_AVX2)
  if (n >= 8u && d8_have_avx2()) {
    i = d8_avx2_src_blocks(row, NULL, table, index, m8, 0, n);
    row += 4u * i;
  }
#endif
  /* The pixels left (fewer than eight with AVX2): their colors gathered, a color of alpha 0 where nothing is drawn, then composed together. */
  uint32_t src[8];
  while (i < n) {
    const size_t rest = n - i < 8u ? n - i : 8u;
    for (size_t j = 0; j < rest; j++) {
      if (index[i + j] == GSHCPU_RENDER_GRADIENT_NONE || m8[i + j] == 0) {
        src[j] = 0;
      } else {
        memcpy(&src[j], table + 4u * (size_t)index[i + j], sizeof src[j]);
      }
    }
    gshcpu_direct8_span_pixels(row, src, m8 + i, 0, rest);
    i += rest;
    row += 4u * rest;
  }
}

#if defined(D8_AVX2)
__attribute__((target("avx2"))) static size_t d8_avx2_weigh(uint8_t * m, const uint8_t * c, size_t n) {
  const __m256i zero = _mm256_setzero_si256();
  const __m256i k80 = _mm256_set1_epi16(0x80);
  size_t i = 0;
  for (; i + 32u <= n; i += 32) {
    const __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(m + i));
    const __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(c + i));
    __m256i t = _mm256_add_epi16(_mm256_mullo_epi16(_mm256_unpacklo_epi8(a, zero), _mm256_unpacklo_epi8(b, zero)), k80);
    const __m256i lo = _mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8);
    t = _mm256_add_epi16(_mm256_mullo_epi16(_mm256_unpackhi_epi8(a, zero), _mm256_unpackhi_epi8(b, zero)), k80);
    const __m256i hi = _mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8);
    _mm256_storeu_si256((__m256i *)(void *)(m + i), _mm256_packus_epi16(lo, hi));
  }
  return i;
}
#endif

void gshcpu_direct8_weigh(uint8_t * m, const uint8_t * c, size_t n) {
  size_t i = 0;
#if defined(D8_AVX2)
  if (n >= 32u && d8_have_avx2()) {
    i = d8_avx2_weigh(m, c, n);
  }
#endif
  for (; i < n; i++) {
    m[i] = (uint8_t)(((uint32_t)m[i] * c[i] + 127u) / 255u);
  }
}

/* --- straight-alpha targets ----------------------------------------------------------------------------- */

#if defined(D8_AVX2)
/** Eight pixels at a time of the premultiplication: the pixels done, a multiple of eight. */
__attribute__((target("avx2"))) static size_t d8_avx2_premultiply(unsigned char * dst, const unsigned char * src, size_t n) {
  const __m256i zero = _mm256_setzero_si256();
  const __m256i k80 = _mm256_set1_epi16(0x80);
  size_t i = 0;
  for (; i + 8u <= n; i += 8) {
    const __m256i px = _mm256_loadu_si256((const __m256i *)(const void *)(src + 4u * i));
    const __m256i lo = _mm256_unpacklo_epi8(px, zero);
    const __m256i hi = _mm256_unpackhi_epi8(px, zero);
    const __m256i a_lo = _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(lo, 0xff), 0xff);
    const __m256i a_hi = _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(hi, 0xff), 0xff);
    __m256i t = _mm256_add_epi16(_mm256_mullo_epi16(lo, a_lo), k80);
    __m256i p_lo = _mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8);
    t = _mm256_add_epi16(_mm256_mullo_epi16(hi, a_hi), k80);
    __m256i p_hi = _mm256_srli_epi16(_mm256_add_epi16(t, _mm256_srli_epi16(t, 8)), 8);
    p_lo = _mm256_blend_epi16(p_lo, lo, 0x88); /* the alpha lane is the alpha */
    p_hi = _mm256_blend_epi16(p_hi, hi, 0x88);
    _mm256_storeu_si256((__m256i *)(void *)(dst + 4u * i), _mm256_packus_epi16(p_lo, p_hi));
  }
  return i;
}
#endif

/** The premultiplied channel of straight @p c under alpha @p a: `(c a + 127) / 255`, the weight of D8. */
static inline uint8_t d8_premul(uint32_t c, uint32_t a) {
  return (uint8_t)((c * a + 127u) / 255u);
}

void gshcpu_direct8_premultiply_row(unsigned char * dst, const unsigned char * src, size_t n) {
  size_t i = 0;
#if defined(D8_AVX2)
  if (n >= 8u && d8_have_avx2()) {
    i = d8_avx2_premultiply(dst, src, n);
  }
#endif
  for (; i < n; i++) {
    const uint32_t a = src[4u * i + 3u];
    if (a == 255u) {
      memcpy(dst + 4u * i, src + 4u * i, 4);
    } else {
      dst[4u * i + 0u] = d8_premul(src[4u * i + 0u], a);
      dst[4u * i + 1u] = d8_premul(src[4u * i + 1u], a);
      dst[4u * i + 2u] = d8_premul(src[4u * i + 2u], a);
      dst[4u * i + 3u] = (uint8_t)a;
    }
  }
}

void gshcpu_direct8_store_straight_row(unsigned char * target, const unsigned char * premultiplied, size_t n) {
  for (size_t i = 0; i < n; i++, target += 4, premultiplied += 4) {
    const uint32_t ao = target[3];
    /* A pixel the drawing left as it loaded is left as it was, whatever the rounding of the round trip would give. */
    if (ao == 255u) {
      if (memcmp(target, premultiplied, 4) == 0) {
        continue;
      }
    } else if (ao == 0u) {
      if (premultiplied[3] == 0 && premultiplied[0] == 0 && premultiplied[1] == 0 && premultiplied[2] == 0) {
        continue;
      }
    } else if (premultiplied[3] == ao && premultiplied[0] == d8_premul(target[0], ao) && premultiplied[1] == d8_premul(target[1], ao) &&
        premultiplied[2] == d8_premul(target[2], ao)) {
      continue;
    }
    const uint32_t a = premultiplied[3];
    if (a == 0u) {
      memset(target, 0, 4);
    } else if (a == 255u) {
      memcpy(target, premultiplied, 4);
    } else {
      for (int k = 0; k < 3; k++) {
        const uint32_t c = ((uint32_t)premultiplied[k] * 255u + a / 2u) / a;
        target[k] = (uint8_t)(c > 255u ? 255u : c);
      }
      target[3] = (uint8_t)a;
    }
  }
}

/* --- blend modes on bytes ------------------------------------------------------------------------------ */

/** The part of a separable mode's result that depends on both pixels, `As Ad B(Cb, Cs)` (B2), from the premultiplied channels. */
static inline __attribute__((always_inline)) float d8_both(uint32_t op, float s, float d, float sa, float da) {
  switch (op) {
    case GSHSCENE_COMPOSITE_MULTIPLY:
      return s * d;
    case GSHSCENE_COMPOSITE_SCREEN:
      return sa * d + da * s - s * d;
    case GSHSCENE_COMPOSITE_OVERLAY:
      /* hard-light with the pixels exchanged */
      return 2.0f * d <= da ? 2.0f * s * d : da * s + sa * (2.0f * d - da) - s * (2.0f * d - da);
    case GSHSCENE_COMPOSITE_HARD_LIGHT:
      return 2.0f * s <= sa ? 2.0f * s * d : sa * d + da * (2.0f * s - sa) - d * (2.0f * s - sa);
    case GSHSCENE_COMPOSITE_DARKEN:
      return sa * d < da * s ? sa * d : da * s;
    case GSHSCENE_COMPOSITE_LIGHTEN:
      return sa * d > da * s ? sa * d : da * s;
    case GSHSCENE_COMPOSITE_DIFFERENCE: {
      const float t = sa * d - da * s;
      return t < 0.0f ? -t : t;
    }
    default: /* GSHSCENE_COMPOSITE_EXCLUSION */
      return sa * d + da * s - 2.0f * s * d;
  }
}

int gshcpu_direct8_blends(uint32_t op) {
  switch (op) {
    case GSHSCENE_COMPOSITE_MULTIPLY:
    case GSHSCENE_COMPOSITE_SCREEN:
    case GSHSCENE_COMPOSITE_OVERLAY:
    case GSHSCENE_COMPOSITE_HARD_LIGHT:
    case GSHSCENE_COMPOSITE_DARKEN:
    case GSHSCENE_COMPOSITE_LIGHTEN:
    case GSHSCENE_COMPOSITE_DIFFERENCE:
    case GSHSCENE_COMPOSITE_EXCLUSION:
      return 1;
    default:
      return 0;
  }
}

/** One byte of `v` (a float in 0..1): rounded, held to 0..@p top. */
static inline uint8_t d8_byte(float v, uint8_t top) {
  const int32_t n = (int32_t)(v * 255.0f + 0.5f);
  return n <= 0 ? 0 : n >= (int32_t)top ? top : (uint8_t)n;
}

#define D8_BLEND_LOOP(OP) \
  for (size_t i = 0; i < n; i++, dst += 4, src += 4) { \
    if (src[3] == 0) { \
      continue; \
    } \
    const float sa = (float)src[3] * sk; \
    const float da = (float)dst[3] * (1.0f / 255.0f); \
    float c[4]; \
    for (int k = 0; k < 3; k++) { \
      const float s = (float)src[k] * sk; \
      const float d = (float)dst[k] * (1.0f / 255.0f); \
      c[k] = s * (1.0f - da) + d * (1.0f - sa) + d8_both(OP, s, d, sa, da); \
    } \
    c[3] = sa + da - sa * da; \
    const uint8_t a = d8_byte(c[3], 255); \
    dst[3] = a; \
    for (int k = 0; k < 3; k++) { \
      dst[k] = d8_byte(c[k], a); \
    } \
  }

#if defined(D8_AVX2)
/** The result of a separable mode that depends on both pixels (d8_both), eight pixels at a time. */
__attribute__((target("avx2"))) static inline __attribute__((always_inline)) __m256 d8_both_v(uint32_t op, __m256 s, __m256 d, __m256 sa,
    __m256 da) {
  const __m256 two = _mm256_set1_ps(2.0f);
  const __m256 sd = _mm256_mul_ps(s, d);
  switch (op) {
    case GSHSCENE_COMPOSITE_MULTIPLY:
      return sd;
    case GSHSCENE_COMPOSITE_SCREEN:
      return _mm256_sub_ps(_mm256_add_ps(_mm256_mul_ps(sa, d), _mm256_mul_ps(da, s)), sd);
    case GSHSCENE_COMPOSITE_OVERLAY: {
      const __m256 d2 = _mm256_mul_ps(two, d);
      const __m256 t = _mm256_sub_ps(d2, da);
      const __m256 hi = _mm256_sub_ps(_mm256_add_ps(_mm256_mul_ps(da, s), _mm256_mul_ps(sa, t)), _mm256_mul_ps(s, t));
      const __m256 lo = _mm256_mul_ps(two, sd);
      return _mm256_blendv_ps(hi, lo, _mm256_cmp_ps(d2, da, _CMP_LE_OQ));
    }
    case GSHSCENE_COMPOSITE_HARD_LIGHT: {
      const __m256 s2 = _mm256_mul_ps(two, s);
      const __m256 t = _mm256_sub_ps(s2, sa);
      const __m256 hi = _mm256_sub_ps(_mm256_add_ps(_mm256_mul_ps(sa, d), _mm256_mul_ps(da, t)), _mm256_mul_ps(d, t));
      const __m256 lo = _mm256_mul_ps(two, sd);
      return _mm256_blendv_ps(hi, lo, _mm256_cmp_ps(s2, sa, _CMP_LE_OQ));
    }
    case GSHSCENE_COMPOSITE_DARKEN:
      return _mm256_min_ps(_mm256_mul_ps(sa, d), _mm256_mul_ps(da, s));
    case GSHSCENE_COMPOSITE_LIGHTEN:
      return _mm256_max_ps(_mm256_mul_ps(sa, d), _mm256_mul_ps(da, s));
    case GSHSCENE_COMPOSITE_DIFFERENCE:
      return _mm256_andnot_ps(_mm256_set1_ps(-0.0f), _mm256_sub_ps(_mm256_mul_ps(sa, d), _mm256_mul_ps(da, s)));
    default: /* GSHSCENE_COMPOSITE_EXCLUSION */
      return _mm256_sub_ps(_mm256_add_ps(_mm256_mul_ps(sa, d), _mm256_mul_ps(da, s)), _mm256_mul_ps(two, sd));
  }
}

/** The channel @p k (0 to 3) of eight packed pixels as eight floats of 0..255. */
__attribute__((target("avx2"))) static inline __attribute__((always_inline)) __m256 d8_channel(__m256i px, int k) {
  return _mm256_cvtepi32_ps(_mm256_and_si256(_mm256_srli_epi32(px, 8 * k), _mm256_set1_epi32(255)));
}

/** A result channel of 0..1 as eight ints of 0..@p top (rounded). */
__attribute__((target("avx2"))) static inline __attribute__((always_inline)) __m256i d8_round_v(__m256 v, __m256i top) {
  const __m256i n = _mm256_cvttps_epi32(_mm256_add_ps(_mm256_mul_ps(v, _mm256_set1_ps(255.0f)), _mm256_set1_ps(0.5f)));
  return _mm256_min_epi32(_mm256_max_epi32(n, _mm256_setzero_si256()), top);
}

#define D8_AVX2_BLEND(NAME, OP) \
  __attribute__((target("avx2"))) static size_t NAME(unsigned char * dst, const unsigned char * src, size_t n, float opacity) { \
    const __m256 inv = _mm256_set1_ps(1.0f / 255.0f); \
    const __m256 op = _mm256_set1_ps(opacity); \
    const __m256 sk = _mm256_mul_ps(inv, op); \
    const __m256 one = _mm256_set1_ps(1.0f); \
    size_t i = 0; \
    for (; i + 8u <= n; i += 8) { \
      const __m256i sp = _mm256_loadu_si256((const __m256i *)(const void *)(src + 4u * i)); \
      const __m256i dp = _mm256_loadu_si256((const __m256i *)(const void *)(dst + 4u * i)); \
      const __m256 sa = _mm256_mul_ps(d8_channel(sp, 3), sk); \
      const __m256 da = _mm256_mul_ps(d8_channel(dp, 3), inv); \
      const __m256 fa = _mm256_sub_ps(one, da); \
      const __m256 fb = _mm256_sub_ps(one, sa); \
      const __m256 oa = _mm256_sub_ps(_mm256_add_ps(sa, da), _mm256_mul_ps(sa, da)); \
      const __m256i a = d8_round_v(oa, _mm256_set1_epi32(255)); \
      __m256i out = _mm256_slli_epi32(a, 24); \
      for (int k = 0; k < 3; k++) { \
        const __m256 s = _mm256_mul_ps(d8_channel(sp, k), sk); \
        const __m256 d = _mm256_mul_ps(d8_channel(dp, k), inv); \
        const __m256 c = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(s, fa), _mm256_mul_ps(d, fb)), d8_both_v(OP, s, d, sa, da)); \
        out = _mm256_or_si256(out, _mm256_slli_epi32(d8_round_v(c, a), 8 * k)); \
      } \
      /* a pixel with no source alpha is left alone */ \
      const __m256i keep = _mm256_cmpeq_epi32(_mm256_srli_epi32(sp, 24), _mm256_setzero_si256()); \
      _mm256_storeu_si256((__m256i *)(void *)(dst + 4u * i), _mm256_blendv_epi8(out, dp, keep)); \
    } \
    return i; \
  }

D8_AVX2_BLEND(d8_avx2_multiply, GSHSCENE_COMPOSITE_MULTIPLY)
D8_AVX2_BLEND(d8_avx2_screen, GSHSCENE_COMPOSITE_SCREEN)
D8_AVX2_BLEND(d8_avx2_overlay, GSHSCENE_COMPOSITE_OVERLAY)
D8_AVX2_BLEND(d8_avx2_hard_light, GSHSCENE_COMPOSITE_HARD_LIGHT)
D8_AVX2_BLEND(d8_avx2_darken, GSHSCENE_COMPOSITE_DARKEN)
D8_AVX2_BLEND(d8_avx2_lighten, GSHSCENE_COMPOSITE_LIGHTEN)
D8_AVX2_BLEND(d8_avx2_difference, GSHSCENE_COMPOSITE_DIFFERENCE)
D8_AVX2_BLEND(d8_avx2_exclusion, GSHSCENE_COMPOSITE_EXCLUSION)

/** The pixels of the row a vector kernel does, a multiple of eight; 0 where the processor has no AVX2. */
static size_t d8_blend_avx2(uint32_t op, unsigned char * dst, const unsigned char * src, size_t n, float opacity) {
  if (n < 8u || !d8_have_avx2()) {
    return 0;
  }
  switch (op) {
    case GSHSCENE_COMPOSITE_MULTIPLY:
      return d8_avx2_multiply(dst, src, n, opacity);
    case GSHSCENE_COMPOSITE_SCREEN:
      return d8_avx2_screen(dst, src, n, opacity);
    case GSHSCENE_COMPOSITE_OVERLAY:
      return d8_avx2_overlay(dst, src, n, opacity);
    case GSHSCENE_COMPOSITE_HARD_LIGHT:
      return d8_avx2_hard_light(dst, src, n, opacity);
    case GSHSCENE_COMPOSITE_DARKEN:
      return d8_avx2_darken(dst, src, n, opacity);
    case GSHSCENE_COMPOSITE_LIGHTEN:
      return d8_avx2_lighten(dst, src, n, opacity);
    case GSHSCENE_COMPOSITE_DIFFERENCE:
      return d8_avx2_difference(dst, src, n, opacity);
    default:
      return d8_avx2_exclusion(dst, src, n, opacity);
  }
}
#endif

void gshcpu_direct8_blend_row(uint32_t op, unsigned char * dst, const unsigned char * src, size_t n, float opacity) {
#if defined(D8_AVX2)
  {
    const size_t done = d8_blend_avx2(op, dst, src, n, opacity);
    dst += 4u * done;
    src += 4u * done;
    n -= done;
  }
#endif
  const float sk = (1.0f / 255.0f) * opacity; /* as the vector kernel scales: the product of the two first */
  switch (op) {
    case GSHSCENE_COMPOSITE_MULTIPLY:
      D8_BLEND_LOOP(GSHSCENE_COMPOSITE_MULTIPLY)
      break;
    case GSHSCENE_COMPOSITE_SCREEN:
      D8_BLEND_LOOP(GSHSCENE_COMPOSITE_SCREEN)
      break;
    case GSHSCENE_COMPOSITE_OVERLAY:
      D8_BLEND_LOOP(GSHSCENE_COMPOSITE_OVERLAY)
      break;
    case GSHSCENE_COMPOSITE_HARD_LIGHT:
      D8_BLEND_LOOP(GSHSCENE_COMPOSITE_HARD_LIGHT)
      break;
    case GSHSCENE_COMPOSITE_DARKEN:
      D8_BLEND_LOOP(GSHSCENE_COMPOSITE_DARKEN)
      break;
    case GSHSCENE_COMPOSITE_LIGHTEN:
      D8_BLEND_LOOP(GSHSCENE_COMPOSITE_LIGHTEN)
      break;
    case GSHSCENE_COMPOSITE_DIFFERENCE:
      D8_BLEND_LOOP(GSHSCENE_COMPOSITE_DIFFERENCE)
      break;
    default:
      D8_BLEND_LOOP(GSHSCENE_COMPOSITE_EXCLUSION)
      break;
  }
}

/* --- the batch ------------------------------------------------------------------------------------- */

#define BATCH_ENTRIES 1024u
#define BATCH_POINTS 524288u /* 4 MiB of points */
#define BATCH_SUBPATHS 32768u
#define STRIPE_ROWS 32
/** The fewest queued strokes worth the threads' start: below it a flush is drawn by the caller alone. */
#define BATCH_MIN_ENTRIES 4u

typedef struct Entry {
  size_t point0;             /* its points, from the fill's array */
  size_t sub0, subs;         /* and its subpaths */
  int64_t shift_x, shift_y;
  GSHCPU_Rect rect;          /* the destination's window */
  GSHCPU_Direct8 d8;
} Entry;

/** What a batch holds between two flushes.  There are two: the executor draws one while the caller fills the other. */
typedef struct Fill {
  Entry * entry;
  size_t entries;
  GSHGEOM_Device_Point * point;
  size_t points;
  GSHGEOM_Flat_Subpath * sub;
  size_t subs;
  int64_t y0;                /* the first row of stripe 0 */
  int64_t stripes;
} Fill;

typedef struct Worker {
  struct GSHCPU_Batch * batch;
  void * scratch;
  uint8_t * line;
} Worker;

struct GSHCPU_Batch {
  GSHGEOM_Allocator alloc;
  GSHCPU_Executor executor;
  unsigned threads;
  size_t width;              /* the widest window */
  unsigned char * pixels;
  size_t stride;
  Fill fill[2];
  unsigned filling;          /* the one being queued into; the other is being drawn when `drawing` is set */
  Fill * drawing;            /* the fill the tasks draw */
  int started;               /* the executor was started on `drawing` and has not been waited for */
  Worker * worker;           /* one for each thread, the caller's first, made at the first flush that needs them */
  size_t scratch_bytes;
  /* the drawing's */
  GCU_Atomic_Int next;       /* the next stripe to take */
  GCU_Atomic_Int failed;     /* the first result that was not OK, as an int */
};

static void fill_free(const GSHGEOM_Allocator * a, Fill * f) {
  a->free_fn(a->ctx, f->entry);
  a->free_fn(a->ctx, f->point);
  a->free_fn(a->ctx, f->sub);
}

static int fill_make(const GSHGEOM_Allocator * a, Fill * f) {
  f->entry = a->malloc_fn(a->ctx, BATCH_ENTRIES * sizeof *f->entry);
  f->point = a->malloc_fn(a->ctx, BATCH_POINTS * sizeof *f->point);
  f->sub = a->malloc_fn(a->ctx, BATCH_SUBPATHS * sizeof *f->sub);
  return f->entry && f->point && f->sub;
}

GSHCPU_Batch * gshcpu_batch_create(const GSHGEOM_Allocator * alloc, const GSHCPU_Executor * executor, size_t width,
    unsigned char * pixels, size_t stride) {
  if (!executor || !executor->start || !executor->wait || executor->threads < 2u ||
      executor->threads > GSHCPU_EXECUTOR_MAX_THREADS || !width || width > 65000u) {
    return NULL;
  }
  GSHCPU_Batch * b = alloc->calloc_fn(alloc->ctx, 1, sizeof *b);
  if (!b) {
    return NULL;
  }
  b->alloc = *alloc;
  b->executor = *executor;
  b->threads = (unsigned)executor->threads;
  b->width = width;
  b->pixels = pixels;
  b->stride = stride;
  b->scratch_bytes = gshcpu_lean_bytes(width, STRIPE_ROWS);
  gcu_atomic_int_init(&b->next, 0);
  gcu_atomic_int_init(&b->failed, 0);
  if (!fill_make(alloc, &b->fill[0]) || !fill_make(alloc, &b->fill[1])) {
    gshcpu_batch_destroy(b);
    return NULL;
  }
  return b;
}

/** The stripe @p s of the drawing, drawn by @p w: each queued stroke that reaches it, in the order queued. */
static GSHCPU_Result batch_stripe(GSHCPU_Batch * b, Worker * w, int64_t s) {
  const Fill * f = b->drawing;
  const int64_t Y0 = f->y0 + s * STRIPE_ROWS;
  const int64_t Y1 = Y0 + STRIPE_ROWS;
  for (size_t i = 0; i < f->entries; i++) {
    const Entry * e = &f->entry[i];
    GSHCPU_Rect r = e->rect;
    if (r.y1 <= Y0 || r.y0 >= Y1) {
      continue;
    }
    r.y0 = r.y0 > Y0 ? r.y0 : Y0;
    r.y1 = r.y1 < Y1 ? r.y1 : Y1;
    GSHCPU_Result x = gshcpu_render_lean_cover_points(f->point + e->point0, f->sub + e->sub0, e->subs, e->shift_x, e->shift_y, r,
        w->scratch, b->scratch_bytes, w->line, gshcpu_direct8_span, (void *)&e->d8);
    if (x != GSHCPU_OK) {
      return x;
    }
  }
  return GSHCPU_OK;
}

/** Take stripes until there are none, or one fails. */
static void batch_work(Worker * w) {
  GSHCPU_Batch * b = w->batch;
  for (;;) {
    if (gcu_atomic_int_load(&b->failed) != 0) {
      return;
    }
    const int64_t s = gcu_atomic_int_fetch_add(&b->next, 1);
    if (s >= b->drawing->stripes) {
      return;
    }
    GSHCPU_Result x = batch_stripe(b, w, s);
    if (x != GSHCPU_OK) {
      int32_t none = 0;
      (void)gcu_atomic_int_compare_exchange(&b->failed, &none, (int32_t)x);
      return;
    }
  }
}

/** The executor's task: the stripes taken by the helper @p index, which has memory of its own (the caller's is the first). */
static void batch_task(void * ctx, size_t index) {
  GSHCPU_Batch * b = ctx;
  if (index + 1u < b->threads) {
    batch_work(&b->worker[index + 1u]);
  }
}

/** The threads' memory: a scratch and a row of coverages each.  @return 1 when they are there. */
static int batch_workers(GSHCPU_Batch * b) {
  if (b->worker) {
    return 1;
  }
  const GSHGEOM_Allocator * a = &b->alloc;
  b->worker = a->calloc_fn(a->ctx, b->threads, sizeof *b->worker);
  if (!b->worker) {
    return 0;
  }
  for (unsigned i = 0; i < b->threads; i++) {
    Worker * w = &b->worker[i];
    w->batch = b;
    w->scratch = a->calloc_fn(a->ctx, 1, b->scratch_bytes);
    w->line = a->malloc_fn(a->ctx, b->width);
    if (!w->scratch || !w->line) {
      return 0; /* the partial set is freed with the batch; the caller does not use it */
    }
  }
  return 1;
}

/** Wait for the drawing to be done: the caller takes the stripes left, then waits for the helpers.  Empties the drawn fill. */
static GSHCPU_Result batch_complete(GSHCPU_Batch * b) {
  if (!b->started) {
    return GSHCPU_OK;
  }
  batch_work(&b->worker[0]);
  b->executor.wait(b->executor.user);
  b->started = 0;
  b->drawing->entries = b->drawing->points = b->drawing->subs = 0;
  return (GSHCPU_Result)gcu_atomic_int_load(&b->failed);
}

/**
 * Hand what is queued to the threads and go on queueing into the other fill.  The one before it is waited for first, so the
 * strokes are drawn in the order queued.  A render that has few strokes or stripes is drawn by the caller here and then.
 */
static GSHCPU_Result batch_submit(GSHCPU_Batch * b) {
  Fill * f = &b->fill[b->filling];
  if (f->entries == 0) {
    return GSHCPU_OK;
  }
  GSHCPU_Result r = batch_complete(b);
  if (r != GSHCPU_OK || !batch_workers(b)) {
    f->entries = f->points = f->subs = 0;
    return r != GSHCPU_OK ? r : GSHCPU_ERR_OOM;
  }
  int64_t lo = f->entry[0].rect.y0;
  int64_t hi = f->entry[0].rect.y1;
  for (size_t i = 1; i < f->entries; i++) {
    lo = f->entry[i].rect.y0 < lo ? f->entry[i].rect.y0 : lo;
    hi = f->entry[i].rect.y1 > hi ? f->entry[i].rect.y1 : hi;
  }
  f->y0 = lo;
  f->stripes = (hi - lo + STRIPE_ROWS - 1) / STRIPE_ROWS;
  b->drawing = f;
  gcu_atomic_int_store(&b->next, 0);
  gcu_atomic_int_store(&b->failed, 0);
  if (f->entries >= BATCH_MIN_ENTRIES && f->stripes >= 2) {
    /* The executor starts the helpers and returns; a refusal, or a helper it did not start, leaves the stripes to the rest. */
    (void)b->executor.start(b->executor.user, b->threads - 1u, batch_task, b);
    b->started = 1;
    b->filling ^= 1u;
    return GSHCPU_OK;
  }
  batch_work(&b->worker[0]);
  f->entries = f->points = f->subs = 0;
  return (GSHCPU_Result)gcu_atomic_int_load(&b->failed);
}

GSHCPU_Result gshcpu_batch_flush(GSHCPU_Batch * b) {
  if (!b) {
    return GSHCPU_OK;
  }
  GSHCPU_Result r = batch_submit(b);
  GSHCPU_Result c = batch_complete(b);
  return r != GSHCPU_OK ? r : c;
}

void gshcpu_batch_destroy(GSHCPU_Batch * b) {
  if (!b) {
    return;
  }
  (void)batch_complete(b); /* nothing the executor started outlives the batch's memory */
  GSHGEOM_Allocator a = b->alloc;
  if (b->worker) {
    for (unsigned i = 0; i < b->threads; i++) {
      a.free_fn(a.ctx, b->worker[i].scratch);
      a.free_fn(a.ctx, b->worker[i].line);
    }
    a.free_fn(a.ctx, b->worker);
  }
  fill_free(&a, &b->fill[0]);
  fill_free(&a, &b->fill[1]);
  a.free_fn(a.ctx, b);
}

int gshcpu_batch_add(GSHCPU_Batch * b, const GSHGEOM_Flat_Path * path, int64_t shift_x, int64_t shift_y, GSHCPU_Rect rect,
    const uint8_t color[4], GSHCPU_Result * result) {
  *result = GSHCPU_OK;
  const size_t points = gshgeom_flat_path_point_count(path);
  const size_t subs = gshgeom_flat_path_subpath_count(path);
  if (points > BATCH_POINTS || subs > BATCH_SUBPATHS || rect.x1 - rect.x0 > (int64_t)b->width) {
    return 0; /* more than a batch holds, or wider than its threads' scratch: the caller draws it itself, after the flush */
  }
  Fill * f = &b->fill[b->filling];
  if (f->entries == BATCH_ENTRIES || f->points + points > BATCH_POINTS || f->subs + subs > BATCH_SUBPATHS) {
    *result = batch_submit(b); /* the threads draw it while the next is queued */
    if (*result != GSHCPU_OK) {
      return -1;
    }
    f = &b->fill[b->filling];
  }
  Entry * e = &f->entry[f->entries];
  e->point0 = f->points;
  e->sub0 = f->subs;
  e->subs = subs;
  e->shift_x = shift_x;
  e->shift_y = shift_y;
  e->rect = rect;
  e->d8.pixels = b->pixels;
  e->d8.stride = b->stride;
  e->d8.dx = shift_x;
  e->d8.dy = shift_y;
  gshcpu_direct8_color(&e->d8, color);
  if (points) {
    memcpy(f->point + f->points, gshgeom_flat_path_points(path), points * sizeof *f->point);
  }
  for (size_t i = 0; i < subs; i++) {
    if (gshgeom_flat_path_subpath(path, i, &f->sub[f->subs + i]) != GSHGEOM_OK) {
      *result = GSHCPU_ERR_INTERNAL;
      return -1;
    }
  }
  f->points += points;
  f->subs += subs;
  f->entries++;
  return 1;
}
