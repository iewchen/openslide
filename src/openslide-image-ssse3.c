
#include <config.h>
#include "openslide-image.h"

#ifdef USE_SSSE3
#include <x86intrin.h>

void _openslide_bgr24_to_argb32_ssse3(uint8_t *src, size_t src_len,
                                      uint32_t *dst) {
  /* four 24-bits pixels a time */
  const int mm_step = 12;
  /* Decrease mm_len by 1 so that the last read is still 16 bytes inside
   * source buffer.
   */
  size_t mm_len = src_len / mm_step - 1;
  char cff = (char)0xFF;  // specify 0xFF as char to silence sanitizer
  /* Since the alpha will be changed to 255 later, its shuffle control mask can
   * be any value. 0xFF sets alpha to 0.
   */
  __m128i shuffle = _mm_setr_epi8(
      0, 1, 2, cff, 3, 4, 5, cff, 6, 7, 8, cff, 9, 10, 11, cff);
  __m128i opaque = _mm_setr_epi8(
      0, 0, 0, cff, 0, 0, 0, cff, 0, 0, 0, cff, 0, 0, 0, cff);
  __m128i bgr, argb, out;
  for (size_t n = 0; n < mm_len; n++) {
    bgr = _mm_lddqu_si128((__m128i const *)src);
    argb = _mm_shuffle_epi8(bgr, shuffle);  // SSSE3. No alternative in SSE2
    out = _mm_or_si128(argb, opaque);
    _mm_storeu_si128((__m128i *)dst, out);

    src += mm_step;
    dst += 4;
  }

  for (size_t i = mm_len * mm_step; i < src_len; i += 3, src += 3) {
    *dst++ = BGR24TOARGB32(src);
  }
}

void _openslide_restore_czi_zstd1_sse3(uint8_t *src, size_t src_len,
                                       uint8_t *dst) {
  size_t half_len = src_len / 2;
  uint8_t *p = dst;
  uint8_t *slo = src;
  uint8_t *shi = src + half_len;
  const int mm_step = 16;
  __m128i vhi, vlo, tmp;
  size_t len_mm = half_len / mm_step;
  size_t len_remain = half_len % mm_step;
  for (size_t i = 0; i < len_mm; i++) {
    vlo = _mm_lddqu_si128((__m128i const *) slo);  // SSE3
    vhi = _mm_lddqu_si128((__m128i const *) shi);

    /* _mm_stream_si128 is slightly slower than _mm_storeu_si128
     * repeat on the same regions of a test fluorescence slide, cflags -O2
     *     AVX2 : 3.57 GB/s
     *     SSE2 : 3.60 GB/s (use _mm_storeu_si128)
     *     SSE2 : 3.56 GB/s (use _mm_stream_si128)
     *  non-SIMD: 1.85 GB/s
     *  around 20% of time of reading slide is spend on highlow restore
     */
    tmp = _mm_unpacklo_epi8(vlo, vhi);
    _mm_storeu_si128((__m128i *) p, tmp);
    p += mm_step;
    tmp = _mm_unpackhi_epi8(vlo, vhi);
    _mm_storeu_si128((__m128i *) p, tmp);
    p += mm_step;

    slo += mm_step;
    shi += mm_step;
  }

  for (size_t i = 0; i < len_remain; i++) {
    *p++ = *slo++;
    *p++ = *shi++;
  }
}

void _openslide_gray16_to_gray8_sse2(uint8_t *src, size_t src_len,
                                     int pixel_real_bits, uint8_t *dst) {
  /* eight 16-bits pixels a time */
  int nshift = pixel_real_bits - 8;
  const int mm_step = 16;
  /* Decrease mm_len by 1 so that the last write is still 16 bytes inside
   * dst buffer.
   */
  size_t mm_len = src_len / mm_step - 1;
  __m128i gray8, gray16, tmp1, tmp2;
  __m128i hi8 =
      _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1);
  __m128i lo8 =
      _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1);
  __m128i allzero = _mm_set_epi64x(0, 0);

  for (size_t n = 0; n < mm_len; n++) {
    gray16 = _mm_load_si128((__m128i const *)src);
    tmp2 = _mm_srli_epi16(gray16, nshift);
    gray8 = _mm_shuffle_epi8(tmp2, lo8);
    /* check after right shift, whether the high 8 bits are non-zero. Sometimes
     * 14 bits zeiss gray uses more than 14 bits.
     */
    tmp1 = _mm_shuffle_epi8(tmp2, hi8);
    /* 0xFF if high 8 bits is non-zero, 0 otherwise. The sign bit of high 8
     * bits is always zero since it has been shift right, therefor it is safe to
     * compare signed with 0.
     */
    tmp2 = _mm_cmpgt_epi8(tmp1, allzero);
    tmp1 = _mm_or_si128(tmp2, gray8);
    _mm_storeu_si128((__m128i *)dst, tmp1);

    src += mm_step;
    dst += 8;
  }

  size_t i = mm_len * mm_step;
  while (i < src_len) {
    *dst++ = gray16togray8(src, nshift);
    i += 2;
    src += 2;
  }
}

#endif
