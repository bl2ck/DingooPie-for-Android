#ifndef DINGOO_PIE_CC_CC_MATH_COMPAT_H
#define DINGOO_PIE_CC_CC_MATH_COMPAT_H

#include <stdint.h>

static inline uint32_t cc1800Unsigned64TopBit(uint64_t value)
{
    for (int32_t bit = 62; bit >= 0; --bit)
    {
        if ((value >> bit) & 1u) return (uint32_t)bit;
    }
    return 0u;
}

static inline void ccDivideUnsigned64LikeCc1800(uint64_t numerator,
    uint64_t denominator, uint64_t* quotient, uint64_t* remainder)
{
    if (!quotient || !remainder) return;
    *quotient = 0;
    *remainder = numerator;
    if (!denominator || !numerator) return;

    uint32_t denominatorTop = cc1800Unsigned64TopBit(denominator);
    uint32_t numeratorTop = cc1800Unsigned64TopBit(numerator);
    if (numeratorTop < denominatorTop) return;

    uint32_t shift = numeratorTop - denominatorTop;
    uint64_t shiftedDenominator = denominator << shift;
    for (;;)
    {
        if (*remainder >= shiftedDenominator)
        {
            *remainder -= shiftedDenominator;
            *quotient |= 1ull << shift;
        }
        if (!shift) break;
        shiftedDenominator >>= 1;
        --shift;
    }
}

#endif
