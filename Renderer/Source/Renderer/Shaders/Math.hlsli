#pragma once

#include "Common.hlsli"

// Stole from GNU libc.
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

#ifndef FLT_MAX
#define FLT_MAX 3.402823466e+38f
#endif

// https://github.com/imneme/pcg-c-basic/blob/master/pcg_basic.c
// https://stackoverflow.com/questions/4200224/random-noise-functions-for-glsl
struct Pcg32
{
    uint64_t mState; // RNG state.  All values are possible.
    uint64_t mInc; // Controls which RNG sequence (stream) is selected. Must *always* be odd.

    void Init(uint64_t seed, uint64_t seq)
    {
        mState = 0U;
        mInc = (seq << 1U) | 1U;
        Next();
        mState += seed;
        Next();
    }

    uint32_t Next()
    {
        const uint64_t oldState = mState;
        mState = oldState * 6364136223846793005ULL + mInc;
        const uint32_t xorShifted = uint32_t(((oldState >> 18U) ^ oldState) >> 27U);
        const uint32_t rot = uint32_t(oldState >> 59U);
        return (xorShifted >> rot) | (xorShifted << ((-rot) & 31U));
    }

    float NextFloat()
    {
        // 23 bit mantissa => 32 - 23 = 9.
        const uint32_t raw = 0x3f800000U | (Next() >> 9U); // Float range [1, 2].
        return asfloat(raw) - 1.0; // Float range [0, 1].
    }

    float2 NextFloat2()
    {
        return float2(NextFloat(), NextFloat());
    }

    float3 NextFloat3()
    {
        return float3(NextFloat(), NextFloat(), NextFloat());
    }
};

// https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
float InterleavedGradientNoise(int x, int y) {
    return fmod(52.9829189 * fmod(0.06711056 * float(x) + 0.00583715 * float(y), 1.0), 1.0);
}

float Min(float3 x)
{
    return min(x.x, min(x.y, x.z));
}

float Max(float3 x)
{
    return max(x.x, max(x.y, x.z));
}

// [0, 1] -> [-x, x]
template<typename Float>
Float IntervalUnitToSymmetric(Float val, float x)
{
    return ((val - 0.5) * 2.0) * x;
}

// https://www.elopezr.com/the-art-of-packing-data/
uint32_t PackFloat4ToRGBA8Unorm(float4 val)
{
    const uint4 uval = uint4(val * 255.0 + 0.5);
    return pack_u8(uval);
}

float4 UnpackRGBA8UnormToFloat4(uint32_t packed)
{
    return float4(unpack_u8u32(packed)) / 255.0;
}

float2 PackNormalOctahedral(float3 normal)
{
    normal /= (abs(normal.x) + abs(normal.y) + abs(normal.z));
    normal.xy = normal.z >= 0.0 ? normal.xy :
        (1.0 - abs(normal.yx)) * select(normal.xy >= 0.0, 1.0, -1.0);
    normal.xy = normal.xy * 0.5 + 0.5;
    return normal.xy;
}

float3 UnpackNormalOctahedral(float2 packed)
{
    packed = packed * 2.0 - 1.0;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(packed.xy, 1.0 - abs(packed.x) - abs(packed.y));
    const float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

uint16_t PackFloat2ToRG8Snorm(float2 value)
{
    const int2 ivalue = int2(round(value * 127.0));
    return uint16_t((ivalue.g << 8) | ivalue.r);
}

float2 UnpackRG8SnormToFloat2(uint16_t packed)
{
    const int ri = (int(packed) << 24) >> 24;
    const int gi = (int(packed) << 16) >> 24;
    return float2(ri, gi) / 127.0;
}

// These SRGB functions are approximations.
float3 SrgbToLinear(float3 color)
{
    return pow(color, 2.2);
}

float3 LinearToSrgb(float3 color)
{
    return pow(color, 1.0 / 2.2);
}

float Luminance(float3 linearColor)
{
    return dot(linearColor, float3(0.2127, 0.7152, 0.0722));
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
        asfloat(asint(position.z) + (position.z < 0.0 ? -offsetInt.z : offsetInt.z))
    );

    return float3(
        abs(position.x) < origin ? position.x + floatScale * normal.x : posInt.x,
        abs(position.y) < origin ? position.y + floatScale * normal.y : posInt.y,
        abs(position.z) < origin ? position.z + floatScale * normal.z : posInt.z
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
float3 TonemapHejlBurgessDawson(float3 color)
{
   const float3 x = max(0.0, color - 0.004);
   return (x * (6.2 * x + 0.5)) /
          (x * (6.2 * x + 1.7) + 0.06);
}

// https://mynameismjp.wordpress.com/2012/10/28/msaa-resolve-filters/
// https://en.wikipedia.org/wiki/Mitchell%E2%80%93Netravali_filters
float FilterMitchellNetravali(float x, float B = 1.0 / 3.0, float C = 1.0 / 3.0)
{
    float y = 0.0f;
    const float x2 = x * x;
    const float x3 = x * x * x;

    if (x < 1.0)
    {
        y = (12.0 - 9.0 * B - 6.0 * C) * x3 +
            (-18.0 + 12.0 * B + 6.0 * C) * x2 +
            (6.0 - 2.0 * B);
    }
    else if (x < 2)
    {
        y = (-B - 6.0 * C) * x3 +
            (6.0 * B + 30.0 * C) * x2 +
            (-12.0 * B - 48.0 * C) * x +
            (8.0 * B + 24.0 * C);
    }

    return y / 6.0f;
}

// https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
// See https://vec3.ca/posts/bicubic-filtering-in-fewer-taps for more details.
float4 SampleTextureCatmullRom(
    Texture2D<float4> tex,
    SamplerState linearSampler,
    float2 uv,
    float2 texSize
)
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate.
    // We'll do this by rounding down the sample location to get the exact center of our
    // "starting" texel. The starting texel will be at location [1, 1] in the grid,
    // where [0, 0] is the top left corner.
    const float2 samplePos = uv * texSize;
    const float2 texPos1 = floor(samplePos - 0.5) + 0.5;

    // Compute the fractional offset from our starting texel to our original sample
    // location, which we'll feed into the Catmull-Rom spline function to get our filter weights.
    const float2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    const float2 w3 = f * f * (-0.5 + 0.5 * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    const float2 w12 = w1 + w2;
    const float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture.
    float2 texPos0 = texPos1 - 1.0;
    float2 texPos3 = texPos1 + 2.0;
    float2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    float4 result = 0.0;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos0.y), 0.0) * w0.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y), 0.0) * w12.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos0.y), 0.0) * w3.x * w0.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos12.y), 0.0) * w0.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0.0) * w12.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos3.y), 0.0) * w0.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y), 0.0) * w12.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos3.y), 0.0) * w3.x * w3.y;

    return result;
}

// https://github.com/playdeadgames/temporal/blob/master/Assets/Shaders/TemporalReprojection.shader
float3 ClipAabbCenter(float3 aabbMin, float3 aabbMax, float3 p)
{
        // NOTE: only clips towards AABB center (but fast!).
        const float3 pClip = 0.5 * (aabbMax + aabbMin);
        const float3 eClip = 0.5 * (aabbMax - aabbMin) + 0.0001;

        const float3 vClip = p - pClip;
        const float3 vUnit = vClip / eClip;
        const float3 aUnit = abs(vUnit);
        const float maUnit = max(aUnit.x, max(aUnit.y, aUnit.z));

        if (maUnit > 1.0)
        {
            return pClip + vClip / maUnit;
        }
        else
        {
            return p; // Point inside AABB.
        }
}

float3 QuatRotate(float4 quat, float3 vec)
{
    const float3 v = float3(quat.y, quat.z, quat.w);
    const float3 uv = cross(v, vec);
    const float3 uuv = cross(v, uv);

    return vec + (uuv + uv * quat.x) * 2.0;
}

struct BarycentricData
{
    float3 lambda;
    float3 dldx;
    float3 dldy;
    float interpInvW;
};

// https://chaojia.github.io/posts/21-11-29-vertex-attrib-interp/
// TODO: there's a rare issue, texture coordinates and derivatives
// for a triangle or two are way off for some angles, formulas seem to be fine,
// In the reference forward renderer it's fine so attribute quantization should not be an issue.
// Maybe try The Forge's approach. The math makes sense except the last part:
// https://github.com/ConfettiFX/The-Forge/blob/master/Common_3/Renderer/VisibilityBuffer2/Shaders/FSL/VisibilityBufferShadingUtilities.h.fsl#L136
// But maybe it's better, should derive this last formula though.
// TODO: this probably happens because we need to clip against the near plane,
// long triangles are the worst offenders.
BarycentricData CalcBarycentricData(
    float4 posClip0,
    float4 posClip1,
    float4 posClip2,
    float2 pixelNdc,
    float2 twoOverImageSize
)
{
    const float3 invW012 = rcp(float3(posClip0.w, posClip1.w, posClip2.w));

    const float2 ndc0 = posClip0.xy * invW012.x;
    const float2 ndc1 = posClip1.xy * invW012.y;
    const float2 ndc2 = posClip2.xy * invW012.z;

    const float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));

    const float2 deltaNdc0 = pixelNdc - ndc0;
    const float2 deltaNdc1 = pixelNdc - ndc1;
    const float2 deltaNdc2 = pixelNdc - ndc2;

    float3 lambda;
    lambda.y = determinant(float2x2(deltaNdc2, deltaNdc0)) * invDet;
    lambda.z = determinant(float2x2(deltaNdc0, deltaNdc1)) * invDet;
    lambda.x = 1.0 - lambda.y - lambda.z;

    const float3 dldx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet;
    const float3 dldy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet;

    const float interpInvW =
        invW012.x + deltaNdc0.x * dot(invW012, dldx) + deltaNdc0.y * dot(invW012, dldy);
    const float interpW = rcp(interpInvW);

    const float3 interpW_mul_invW012 = interpW * invW012;

    BarycentricData result;

    result.lambda = interpW_mul_invW012 * lambda; // Pre-perspective bary.
    result.dldx = twoOverImageSize.x * interpW_mul_invW012 * dldx;
    result.dldy = -twoOverImageSize.y * interpW_mul_invW012 * dldy;
    result.interpInvW = interpInvW;

    return result;
}

struct InterpolatedData2D
{
    float2 c;
    float2 dcdx;
    float2 dcdy;
};

InterpolatedData2D Interpolate2D(BarycentricData data, float2 attr0, float2 attr1, float2 attr2)
{
    InterpolatedData2D result;

    result.c = attr0 * data.lambda.x + attr1 * data.lambda.y + attr2 * data.lambda.z;

    const float2 delta0 = attr0 - result.c;
    const float2 delta1 = attr1 - result.c;
    const float2 delta2 = attr2 - result.c;

    result.dcdx = delta0 * data.dldx.x + delta1 * data.dldx.y + delta2 * data.dldx.z;
    result.dcdy = delta0 * data.dldy.x + delta1 * data.dldy.y + delta2 * data.dldy.z;

    return result;
}

struct InterpolatedData3D
{
    float3 c;
    float3 dcdx;
    float3 dcdy;
};

InterpolatedData3D Interpolate3D(BarycentricData data, float3 attr0, float3 attr1, float3 attr2)
{
    InterpolatedData3D result;

    result.c = attr0 * data.lambda.x + attr1 * data.lambda.y + attr2 * data.lambda.z;

    const float3 delta0 = attr0 - result.c;
    const float3 delta1 = attr1 - result.c;
    const float3 delta2 = attr2 - result.c;

    result.dcdx = delta0 * data.dldx.x + delta1 * data.dldx.y + delta2 * data.dldx.z;
    result.dcdy = delta0 * data.dldy.x + delta1 * data.dldy.y + delta2 * data.dldy.z;

    return result;
}
