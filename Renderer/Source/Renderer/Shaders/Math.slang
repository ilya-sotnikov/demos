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


// https://github.com/imneme/pcg-c-basic/blob/master/pcg_basic.c
// https://stackoverflow.com/questions/4200224/random-noise-functions-for-glsl
struct Pcg32
{
    uint64_t mState; // RNG state.  All values are possible.
    uint64_t mInc; // Controls which RNG sequence (stream) is selected. Must *always* be odd.

    [mutating]
    void Init(uint64_t seed, uint64_t seq)
    {
        mState = 0U;
        mInc = (seq << 1U) | 1U;
        Next();
        mState += seed;
        Next();
    }

    [mutating]
    uint32_t Next()
    {
        const uint64_t oldState = mState;
        mState = oldState * 6364136223846793005ULL + mInc;
        const uint32_t xorShifted = uint32_t(((oldState >> 18U) ^ oldState) >> 27U);
        const uint32_t rot = uint32_t(oldState >> 59U);
        return (xorShifted >> rot) | (xorShifted << ((-rot) & 31U));
    }

    [mutating]
    float NextFloat()
    {
        // 23 bit mantissa => 32 - 23 = 9.
        const uint32_t raw = 0x3f800000U | (Next() >> 9U); // Float range [1, 2].
        return asfloat(raw) - 1.0; // Float range [0, 1].
    }

    [mutating]
    float2 NextFloat2()
    {
        return float2(NextFloat(), NextFloat());
    }

    [mutating]
    float3 NextFloat3()
    {
        return float3(NextFloat(), NextFloat(), NextFloat());
    }
};

// [0, 1] -> [-x, x]
T IntervalUnitToSymmetric<T>(T val, float x) where T : IFloat
{
    return ((val - T(0.5)) * T(2.0)) * T(x);
}

// https://www.elopezr.com/the-art-of-packing-data/
uint8_t PackFloatToUnorm(float val)
{
    return uint8_t(val * 255.0 + 0.5);
}

float UnpackUnormToFloat(uint8_t packed)
{
    return float(packed) / 255.0;
}

uint32_t PackFloat4ToRGBA8Unorm(float4 val)
{
    const uint4 uval = uint4(val * 255.0 + 0.5);
    return pack_u8(uval);
}

float4 UnpackRGBA8UnormToFloat4(uint32_t packed)
{
    return float4(unpack_u8u32(packed)) / 255.0;
}

// SRGB functions are approximations.
float4 FromSrgb(float4 color)
{
    return float4(pow(color.rgb, float3(2.2)), color.a);
}

float4 ToSrgb(float4 color)
{
    return float4(pow(color.rgb, float3(1.0 / 2.2)), color.a);
}

float3 FromSrgb(float3 color)
{
    return pow(color, float3(2.2));
}

float3 ToSrgb(float3 color)
{
    return pow(color, float3(1.0 / 2.2));
}

// A Fast and Robust Method for Avoiding Self-Intersection, Carsten Wächter, Nikolaus Binder.
// https://link.springer.com/content/pdf/10.1007/978-1-4842-4427-2_6
float3 OffsetRay(float3 position, float3 normal)
{
    const float origin = 1.0 / 32.0;
    const float floatScale = 1.0 / 65536.0;
    const float intScale = 256.0;

    const int3 offsetInt = int3(
        int(intScale * normal.x),
        int(intScale * normal.y),
        int(intScale * normal.z)
    );

    const float3 posInt = float3(
        asfloat(asint(position.x) + (position.x < 0.0 ? -offsetInt.x : offsetInt.x)),
        asfloat(asint(position.y) + (position.y < 0.0 ? -offsetInt.y : offsetInt.y)),
        asfloat(asint(position.z) + (position.z < 0.0 ? -offsetInt.z : offsetInt.z)),
    );

    return float3(
        abs(position.x) < origin ? position.x + floatScale * normal.x : posInt.x,
        abs(position.y) < origin ? position.y + floatScale * normal.y : posInt.y,
        abs(position.z) < origin ? position.z + floatScale * normal.z : posInt.z,
    );
}

// Building an Orthonormal Basis, Revisited, Tom Duff, James Burgess,
// Per Christensen, Christophe Hery, Andrew Kensler, Max Liani, and Ryusuke Villemin:
// https://www.jcgt.org/published/0006/01/01/
void ComputeBasis(float3 normal, out float3 tangent1, out float3 tangent2)
{
    const float nzSign = sign(normal.z);
    const float a = -1.0 / (nzSign + normal.z);
    const float b = normal.x * normal.y * a;
    tangent1 = float3(1.0 + nzSign * normal.x * normal.x * a, nzSign * b, -nzSign * normal.x);
    tangent2 = float3(b, nzSign + normal.y * normal.y * a, -normal.y);
}

// void ComputeBasis(float3 normal, out float3 tangent1, out float3 tangent2)
// {
//     // Suppose vector a has all equal components and is a unit vector:
//     // a = (s, s, s)
//     // Then 3*s*s = 1, s = sqrt(1/3) = 0.57735. This means that at
//     // least one component of a unit vector must be greater or equal
//     // to 0.57735.
//     if (abs(normal.x) >= 0.57735)
//     {
//         tangent1 = float3(normal.y, -normal.x, 0.0);
//     }
//     else
//     {
//         tangent1 = float3(0.0, normal.z, -normal.y);
//     }
//
//     tangent1 = normalize(tangent1);
//     tangent2 = cross(normal, tangent1);
// }

float3 ChangeBasis(float3 vec, float3 direction)
{
    float3 t1;
    float3 t2;
    ComputeBasis(direction, t1, t2);

    return float3(t1 * vec.x + t2 * vec.y + direction * vec.z);
}

// Doesn't need additional linear -> SRGB.
// http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 TonemapHejlBurgessDawson(float3 color, float exposure = 1.0)
{
   color *= exposure;
   const float3 x = max(0.0, color - 0.004);
   return (x * (6.2 * x + 0.5)) /
          (x * (6.2 * x + 1.7) + 0.06);
}
