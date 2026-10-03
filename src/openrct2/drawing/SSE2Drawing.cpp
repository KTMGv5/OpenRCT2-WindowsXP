/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../core/Guard.hpp"
#include "Drawing.Sprite.h"
#include "PaletteIndex.h"

using OpenRCT2::Drawing::PaletteIndex;

#if defined(__SSE2__) || defined(_M_IX86) || defined(_M_X64)

    #include <emmintrin.h>

void MaskSse2(
    int32_t width, int32_t height, const uint8_t* RESTRICT maskSrc, const uint8_t* RESTRICT colourSrc,
    PaletteIndex* RESTRICT dst, int32_t maskWrap, int32_t colourWrap, int32_t dstWrap)
{
    if (width == 32)
    {
        const __m128i zero128 = _mm_setzero_si128();
        for (int32_t yy = 0; yy < height; yy++)
        {
            int32_t colourStep = yy * (colourWrap + 32);
            int32_t maskStep = yy * (maskWrap + 32);
            int32_t dstStep = yy * (dstWrap + 32);

            // first half (16 bytes)
            const __m128i colour1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(colourSrc + colourStep));
            const __m128i mask1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(maskSrc + maskStep));
            const __m128i dest1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst + dstStep));
            const __m128i mc1 = _mm_and_si128(colour1, mask1);
            const __m128i saturate1 = _mm_cmpeq_epi8(mc1, zero128);
            // Blend: (dest & saturate) | (mc & ~saturate)
            const __m128i blended1 = _mm_or_si128(_mm_and_si128(dest1, saturate1), _mm_andnot_si128(saturate1, mc1));

            // second half (16 bytes)
            const __m128i colour2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(colourSrc + 16 + colourStep));
            const __m128i mask2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(maskSrc + 16 + maskStep));
            const __m128i dest2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst + 16 + dstStep));
            const __m128i mc2 = _mm_and_si128(colour2, mask2);
            const __m128i saturate2 = _mm_cmpeq_epi8(mc2, zero128);
            // Blend: (dest & saturate) | (mc & ~saturate)
            const __m128i blended2 = _mm_or_si128(_mm_and_si128(dest2, saturate2), _mm_andnot_si128(saturate2, mc2));

            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + dstStep), blended1);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 16 + dstStep), blended2);
        }
    }
    else
    {
        MaskScalar(width, height, maskSrc, colourSrc, dst, maskWrap, colourWrap, dstWrap);
    }
}

#else

void MaskSse2(
    int32_t width, int32_t height, const uint8_t* RESTRICT maskSrc, const uint8_t* RESTRICT colourSrc,
    PaletteIndex* RESTRICT dst, int32_t maskWrap, int32_t colourWrap, int32_t dstWrap)
{
    MaskScalar(width, height, maskSrc, colourSrc, dst, maskWrap, colourWrap, dstWrap);
}

#endif
