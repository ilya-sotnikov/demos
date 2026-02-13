#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;
using f64 = double;
using uchar = unsigned char;
using uint = unsigned int;
using ulong = unsigned long;
using ulonglong = unsigned long long;

// Stole from GNU libc since it's non-standard.
#ifndef M_Ef
#define M_Ef 2.7182818284590452354f // e
#endif

#ifndef M_LOG2Ef
#define M_LOG2Ef 1.4426950408889634074f // log_2
#endif

#ifndef M_LOG10Ef
#define M_LOG10Ef 0.43429448190325182765f // log_10 e
#endif

#ifndef M_LN2f
#define M_LN2f 0.69314718055994530942f // log_e 2
#endif

#ifndef M_LN10f
#define M_LN10f 2.30258509299404568402f // log_e 10
#endif

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f // pi
#endif

#ifndef M_PI_2f
#define M_PI_2f 1.57079632679489661923f // pi / 2
#endif

#ifndef M_PI_4f
#define M_PI_4f 0.78539816339744830962f // pi / 4
#endif

#ifndef M_1_PIf
#define M_1_PIf 0.31830988618379067154f // 1 / pi
#endif

#ifndef M_2_PIf
#define M_2_PIf 0.63661977236758134308f // 2 / pi
#endif

#ifndef M_2_SQRTPIf
#define M_2_SQRTPIf 1.12837916709551257390f // 2 / sqrt(pi)
#endif

#ifndef M_SQRT2f
#define M_SQRT2f 1.41421356237309504880f // sqrt(2)
#endif

#ifndef M_SQRT1_2f
#define M_SQRT1_2f 0.70710678118654752440f // 1 / sqrt(2)
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define ARRAY_SSIZE(arr) static_cast<ptrdiff_t>((sizeof(arr) / sizeof((arr)[0])))

template <typename T>
struct Slice
{
    T* mData;
    int mCount;

    int GetSizeBytes() const
    {
        return mCount * static_cast<int>(sizeof(T));
    }
};

struct MemorySlice
{
    void* mData;
    ptrdiff_t mCount;
};

template <typename Function>
struct ScopedDefer
{
    Function mFunction;
    explicit ScopedDefer(Function f) : mFunction(f) { }
    ~ScopedDefer()
    {
        mFunction();
    }
};

#define STR_CONCAT__(a, b) STR_CONCAT_2__(a, b)
#define STR_CONCAT_2__(a, b) a##b
#define DEFER(code) \
    const auto STR_CONCAT__(tmpDeferVarName__, __LINE__) = ScopedDefer([&]() { code; })
