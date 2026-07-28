#include "cc/cc_graphics_compat.h"

#include <stdio.h>
#include <string.h>

int main()
{
    static const uint16_t transparent = 0xffffu;
    static const uint16_t source[] = {
        1, transparent, 3, 4, 90, 91,
        5, 6, transparent, 8, 92, 93,
        9, 10, 11, transparent, 94, 95,
    };
    uint16_t destination[21];
    for (size_t i = 0; i < sizeof(destination) / sizeof(destination[0]); ++i)
    {
        destination[i] = 100u + (uint16_t)i;
    }

    ccBlitTransparent16(destination, 7u, source, 6u, 4u, 3u, transparent);

    static const uint16_t expected[] = {
        1, 101, 3, 4, 104, 105, 106,
        5, 6, 109, 8, 111, 112, 113,
        9, 10, 11, 117, 118, 119, 120,
    };
    if (memcmp(destination, expected, sizeof(expected)) != 0)
    {
        fprintf(stderr, "transparent blit row progression failed\n");
        return 1;
    }

    static const uint16_t scaledSource[] = {
        10, 11, 12, 13,
        20, 21, 22, 23,
        30, 31, 32, 33,
    };
    uint16_t scaledDestination[8] = {};
    ccBlitScaled16(scaledDestination, 4u, scaledSource, 4u,
        0u, 0u, 0x10000u, 0x10000u, 4u, 2u);
    static const uint16_t scaledExpected[] = {
        10, 11, 12, 13,
        20, 21, 22, 23,
    };
    if (memcmp(scaledDestination, scaledExpected, sizeof(scaledExpected)) != 0)
    {
        fprintf(stderr, "scaled 16-bit blit sampling failed\n");
        return 1;
    }

    static const uint8_t indexedSource[] = {
        1, 2, 3, 4,
        4, 3, 2, 1,
    };
    static const uint16_t indexedPalette[] = {
        0, 101, 202, 303, 404,
    };
    uint16_t indexedDestination[8] = {};
    ccBlitScaledIndexed16(indexedDestination, 4u, indexedSource, 4u,
        indexedPalette, 0u, 0u, 0x10000u, 0x10000u, 4u, 2u);
    static const uint16_t indexedExpected[] = {
        101, 202, 303, 404,
        404, 303, 202, 101,
    };
    if (memcmp(indexedDestination, indexedExpected,
            sizeof(indexedExpected)) != 0)
    {
        fprintf(stderr, "scaled indexed blit palette lookup failed\n");
        return 1;
    }
    if (ccNormalizeScaledStep(0u, 200u, 40u) != 0x10000u ||
        ccNormalizeScaledStep(380u, 320u, 320u) != 0x10000u ||
        ccNormalizeScaledStep(0x8000u, 160u, 320u) != 0x8000u)
    {
        fprintf(stderr, "scaled step normalization failed\n");
        return 1;
    }
    if (ccBlendRgb565(0x2104u, 0x4208u, 1u) != 0x630cu ||
        ccBlendRgb565(0x630cu, 0x4208u, 2u) != 0x2104u ||
        ccBlendRgb565(0xffffu, 0x0000u, 5u) != 0x7bcfu)
    {
        fprintf(stderr, "RGB565 blend modes failed\n");
        return 1;
    }
    static const int32_t vectorSource[] = {
        3 * 65536, 4 * 65536, 0,
    };
    int32_t vectorDestination[3] = {};
    if (!ccNormalizeVector3Fixed(vectorDestination, vectorSource) ||
        vectorDestination[0] != 39321 || vectorDestination[1] != 52428 ||
        vectorDestination[2] != 0)
    {
        fprintf(stderr, "fixed vector normalization failed\n");
        return 1;
    }
    printf("CC transparent 16-bit blit regression passed.\n");
    return 0;
}
