#ifndef DINGOO_PIE_CC_CC_GRAPHICS_COMPAT_H
#define DINGOO_PIE_CC_CC_GRAPHICS_COMPAT_H

#include <stdint.h>
#include <limits.h>
#include <math.h>

static inline void ccBlitTransparent16(uint16_t* destination,
    uint32_t destinationStride, const uint16_t* source,
    uint32_t sourceStride, uint32_t width, uint32_t height,
    uint16_t transparent)
{
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint16_t* sourceRow = source + y * sourceStride;
        uint16_t* destinationRow = destination + y * destinationStride;
        for (uint32_t x = 0; x < width; ++x)
        {
            if (sourceRow[x] != transparent)
            {
                destinationRow[x] = sourceRow[x];
            }
        }
    }
}

static inline void ccBlitScaled16(uint16_t* destination,
    uint32_t destinationStride, const uint16_t* source,
    uint32_t sourceStride, uint32_t sourceLeft, uint32_t sourceTop,
    uint32_t sourceXStep, uint32_t sourceYStep, uint32_t width,
    uint32_t height)
{
    uint32_t sourceY = sourceTop << 16;
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint16_t* sourceRow = source +
            (sourceY >> 16) * sourceStride + sourceLeft;
        uint16_t* destinationRow = destination + y * destinationStride;
        uint32_t sourceX = 0;
        for (uint32_t x = 0; x < width; ++x)
        {
            destinationRow[x] = sourceRow[sourceX >> 16];
            sourceX += sourceXStep;
        }
        sourceY += sourceYStep;
    }
}

static inline void ccBlitScaledIndexed16(uint16_t* destination,
    uint32_t destinationStride, const uint8_t* source,
    uint32_t sourceStride, const uint16_t* palette, uint32_t sourceLeft,
    uint32_t sourceTop, uint32_t sourceXStep, uint32_t sourceYStep,
    uint32_t width, uint32_t height)
{
    uint32_t sourceY = sourceTop << 16;
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* sourceRow = source +
            (sourceY >> 16) * sourceStride + sourceLeft;
        uint16_t* destinationRow = destination + y * destinationStride;
        uint32_t sourceX = 0;
        for (uint32_t x = 0; x < width; ++x)
        {
            destinationRow[x] = palette[sourceRow[sourceX >> 16]];
            sourceX += sourceXStep;
        }
        sourceY += sourceYStep;
    }
}

static inline uint32_t ccNormalizeScaledStep(uint32_t step,
    uint32_t sourceExtent, uint32_t destinationExtent)
{
    return !step || sourceExtent == destinationExtent ? 0x10000u : step;
}

static inline uint16_t ccBlendRgb565(uint16_t destination, uint16_t source,
    uint32_t mode)
{
    if (mode == 0u) return source;
    if (mode == 1u)
    {
        uint32_t red = (destination & 0xf800u) + (source & 0xf800u);
        uint32_t green = (destination & 0x07e0u) + (source & 0x07e0u);
        uint32_t blue = (destination & 0x001fu) + (source & 0x001fu);
        if (red > 0xf800u) red = 0xf800u;
        if (green > 0x07e0u) green = 0x07e0u;
        if (blue > 0x001fu) blue = 0x001fu;
        return (uint16_t)(red | green | blue);
    }
    if (mode == 2u)
    {
        int32_t red = (destination & 0xf800u) - (source & 0xf800u);
        int32_t green = (destination & 0x07e0u) - (source & 0x07e0u);
        int32_t blue = (destination & 0x001fu) - (source & 0x001fu);
        return (uint16_t)((red > 0 ? red : 0) |
            (green > 0 ? green : 0) | (blue > 0 ? blue : 0));
    }
    if (mode == 3u)
    {
        return (uint16_t)(((destination >> 1) & 0x7bcfu) +
            ((destination >> 2) & 0x39c7u) +
            ((source >> 2) & 0x39c7u));
    }
    if (mode == 4u)
    {
        return (uint16_t)(((source >> 1) & 0x7bcfu) +
            ((destination >> 2) & 0x39c7u) +
            ((source >> 2) & 0x39c7u));
    }
    return (uint16_t)(((destination >> 1) & 0x7bcfu) +
        ((source >> 1) & 0x7bcfu));
}

static inline void ccBlitTransparentBlend16(uint16_t* destination,
    uint32_t destinationStride, const uint16_t* source,
    uint32_t sourceStride, uint32_t width, uint32_t height,
    uint16_t transparent, uint32_t blendMode)
{
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            uint16_t pixel = source[x];
            if (pixel != transparent)
            {
                destination[x] = ccBlendRgb565(destination[x], pixel,
                    blendMode);
            }
        }
        source += sourceStride;
        destination += destinationStride;
    }
}

static inline bool ccNormalizeVector3Fixed(int32_t* destination,
    const int32_t* source)
{
    if (!destination || !source) return false;
    long double x = source[0];
    long double y = source[1];
    long double z = source[2];
    long double length = sqrtl(x * x + y * y + z * z);
    if (length == 0.0L)
    {
        destination[0] = 0;
        destination[1] = 0;
        destination[2] = 0;
        return true;
    }
    for (uint32_t i = 0; i < 3u; ++i)
    {
        long double value = (long double)source[i] * 65536.0L / length;
        if (value > INT32_MAX) value = INT32_MAX;
        if (value < INT32_MIN) value = INT32_MIN;
        destination[i] = (int32_t)value;
    }
    return true;
}

#endif
