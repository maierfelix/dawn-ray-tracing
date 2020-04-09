// Copyright 2017 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "common/Math.h"

#include "common/Assert.h"
#include "common/Platform.h"

#include <algorithm>
#include <cmath>

#if defined(DAWN_COMPILER_MSVC)
#    include <intrin.h>
#endif

uint32_t ScanForward(uint32_t bits) {
    ASSERT(bits != 0);
#if defined(DAWN_COMPILER_MSVC)
    unsigned long firstBitIndex = 0ul;
    unsigned char ret = _BitScanForward(&firstBitIndex, bits);
    ASSERT(ret != 0);
    return firstBitIndex;
#else
    return static_cast<uint32_t>(__builtin_ctz(bits));
#endif
}

uint32_t Log2(uint32_t value) {
    ASSERT(value != 0);
#if defined(DAWN_COMPILER_MSVC)
    unsigned long firstBitIndex = 0ul;
    unsigned char ret = _BitScanReverse(&firstBitIndex, value);
    ASSERT(ret != 0);
    return firstBitIndex;
#else
    return 31 - static_cast<uint32_t>(__builtin_clz(value));
#endif
}

uint32_t Log2(uint64_t value) {
    ASSERT(value != 0);
#if defined(DAWN_COMPILER_MSVC)
#    if defined(DAWN_PLATFORM_64_BIT)
    unsigned long firstBitIndex = 0ul;
    unsigned char ret = _BitScanReverse64(&firstBitIndex, value);
    ASSERT(ret != 0);
    return firstBitIndex;
#    else   // defined(DAWN_PLATFORM_64_BIT)
    unsigned long firstBitIndex = 0ul;
    if (_BitScanReverse(&firstBitIndex, value >> 32)) {
        return firstBitIndex + 32;
    }
    unsigned char ret = _BitScanReverse(&firstBitIndex, value & 0xFFFFFFFF);
    ASSERT(ret != 0);
    return firstBitIndex;
#    endif  // defined(DAWN_PLATFORM_64_BIT)
#else       // defined(DAWN_COMPILER_MSVC)
    return 63 - static_cast<uint32_t>(__builtin_clzll(value));
#endif      // defined(DAWN_COMPILER_MSVC)
}

uint64_t NextPowerOfTwo(uint64_t n) {
    if (n <= 1) {
        return 1;
    }

    return 1ull << (Log2(n - 1) + 1);
}

bool IsPowerOfTwo(uint64_t n) {
    ASSERT(n != 0);
    return (n & (n - 1)) == 0;
}

bool IsPtrAligned(const void* ptr, size_t alignment) {
    ASSERT(IsPowerOfTwo(alignment));
    ASSERT(alignment != 0);
    return (reinterpret_cast<size_t>(ptr) & (alignment - 1)) == 0;
}

bool IsAligned(uint32_t value, size_t alignment) {
    ASSERT(alignment <= UINT32_MAX);
    ASSERT(IsPowerOfTwo(alignment));
    ASSERT(alignment != 0);
    uint32_t alignment32 = static_cast<uint32_t>(alignment);
    return (value & (alignment32 - 1)) == 0;
}

uint32_t Align(uint32_t value, size_t alignment) {
    ASSERT(alignment <= UINT32_MAX);
    ASSERT(IsPowerOfTwo(alignment));
    ASSERT(alignment != 0);
    uint32_t alignment32 = static_cast<uint32_t>(alignment);
    return (value + (alignment32 - 1)) & ~(alignment32 - 1);
}

uint16_t Float32ToFloat16(float fp32) {
    uint32_t fp32i = BitCast<uint32_t>(fp32);
    uint32_t sign16 = (fp32i & 0x80000000) >> 16;
    uint32_t mantissaAndExponent = fp32i & 0x7FFFFFFF;

    if (mantissaAndExponent > 0x7F800000) {  // NaN
        return 0x7FFF;
    } else if (mantissaAndExponent > 0x47FFEFFF) {  // Infinity
        return static_cast<uint16_t>(sign16 | 0x7C00);
    } else if (mantissaAndExponent < 0x38800000) {  // Denormal
        uint32_t mantissa = (mantissaAndExponent & 0x007FFFFF) | 0x00800000;
        int32_t exponent = 113 - (mantissaAndExponent >> 23);

        if (exponent < 24) {
            mantissaAndExponent = mantissa >> exponent;
        } else {
            mantissaAndExponent = 0;
        }

        return static_cast<uint16_t>(
            sign16 | (mantissaAndExponent + 0x00000FFF + ((mantissaAndExponent >> 13) & 1)) >> 13);
    } else {
        return static_cast<uint16_t>(sign16 | (mantissaAndExponent + 0xC8000000 + 0x00000FFF +
                                               ((mantissaAndExponent >> 13) & 1)) >>
                                                  13);
    }
}

bool IsFloat16NaN(uint16_t fp16) {
    return (fp16 & 0x7FFF) > 0x7C00;
}

// Based on the Khronos Data Format Specification 1.2 Section 13.3 sRGB transfer functions
float SRGBToLinear(float srgb) {
    // sRGB is always used in unsigned normalized formats so clamp to [0.0, 1.0]
    if (srgb <= 0.0f) {
        return 0.0f;
    } else if (srgb > 1.0f) {
        return 1.0f;
    }

    if (srgb < 0.04045f) {
        return srgb / 12.92f;
    } else {
        return std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }
}

// generates a 4x3 transform matrix in row-major order
void Fill4x3TransformMatrix(float* out,
                            float tx,
                            float ty,
                            float tz,
                            float rx,
                            float ry,
                            float rz,
                            float sx,
                            float sy,
                            float sz) {
    const float PI = 3.14159265358979f;

    // make identity
    out[0] = 1.0f;
    out[5] = 1.0f;
    out[10] = 1.0f;
    out[15] = 1.0f;
    // apply translation
    {
        float x = tx;
        float y = ty;
        float z = tz;
        out[12] = out[0] * x + out[4] * y + out[8] * z + out[12];
        out[13] = out[1] * x + out[5] * y + out[9] * z + out[13];
        out[14] = out[2] * x + out[6] * y + out[10] * z + out[14];
        out[15] = out[3] * x + out[7] * y + out[11] * z + out[15];
    }
    // apply rotation
    {
        // x rotation
        {
            rx = rx * (PI / 180.0f);
            float s = sinf(rx);
            float c = cosf(rx);
            float a10 = out[4];
            float a11 = out[5];
            float a12 = out[6];
            float a13 = out[7];
            float a20 = out[8];
            float a21 = out[9];
            float a22 = out[10];
            float a23 = out[11];
            out[4] = a10 * c + a20 * s;
            out[5] = a11 * c + a21 * s;
            out[6] = a12 * c + a22 * s;
            out[7] = a13 * c + a23 * s;
            out[8] = a20 * c - a10 * s;
            out[9] = a21 * c - a11 * s;
            out[10] = a22 * c - a12 * s;
            out[11] = a23 * c - a13 * s;
        }
        // y rotation
        {
            ry = ry * (PI / 180.0f);
            float s = sinf(ry);
            float c = cosf(ry);
            float a00 = out[0];
            float a01 = out[1];
            float a02 = out[2];
            float a03 = out[3];
            float a20 = out[8];
            float a21 = out[9];
            float a22 = out[10];
            float a23 = out[11];
            out[0] = a00 * c - a20 * s;
            out[1] = a01 * c - a21 * s;
            out[2] = a02 * c - a22 * s;
            out[3] = a03 * c - a23 * s;
            out[8] = a00 * s + a20 * c;
            out[9] = a01 * s + a21 * c;
            out[10] = a02 * s + a22 * c;
            out[11] = a03 * s + a23 * c;
        }
        // z rotation
        {
            rz = rz * (PI / 180.0f);
            float s = sinf(rz);
            float c = cosf(rz);
            float a00 = out[0];
            float a01 = out[1];
            float a02 = out[2];
            float a03 = out[3];
            float a10 = out[4];
            float a11 = out[5];
            float a12 = out[6];
            float a13 = out[7];
            out[0] = a00 * c + a10 * s;
            out[1] = a01 * c + a11 * s;
            out[2] = a02 * c + a12 * s;
            out[3] = a03 * c + a13 * s;
            out[4] = a10 * c - a00 * s;
            out[5] = a11 * c - a01 * s;
            out[6] = a12 * c - a02 * s;
            out[7] = a13 * c - a03 * s;
        }
    }
    // apply scale
    {
        out[0] = out[0] * sx;
        out[1] = out[1] * sx;
        out[2] = out[2] * sx;
        out[3] = out[3] * sx;
        out[4] = out[4] * sy;
        out[5] = out[5] * sy;
        out[6] = out[6] * sy;
        out[7] = out[7] * sy;
        out[8] = out[8] * sz;
        out[9] = out[9] * sz;
        out[10] = out[10] * sz;
        out[11] = out[11] * sz;
    }
    // turn into 4x3
    out[3] = out[12];
    out[7] = out[13];
    out[11] = out[14];
    // reset last row
    out[12] = 0.0f;
    out[13] = 0.0f;
    out[14] = 0.0f;
    out[15] = 0.0f;
}