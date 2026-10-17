#pragma once

#include "common.hlsli"

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
    uint64_t m_state; // RNG state.  All values are possible.
    uint64_t m_inc; // Controls which RNG sequence (stream) is selected. Must *always* be odd.

    void init(uint64_t seed, uint64_t seq) {
        m_state = 0U;
        m_inc = (seq << 1U) | 1U;
        next();
        m_state += seed;
        next();
    }

    uint32_t next() {
        const uint64_t old_state = m_state;
        m_state = old_state * 6364136223846793005ULL + m_inc;
        const uint32_t xor_shifted = uint32_t(((old_state >> 18U) ^ old_state) >> 27U);
        const uint32_t rot = uint32_t(old_state >> 59U);
        return (xor_shifted >> rot) | (xor_shifted << ((-rot) & 31U));
    }

    float next_float() {
        // 23 bit mantissa => 32 - 23 = 9.
        const uint32_t raw = 0x3f800000U | (next() >> 9U); // Float range [1, 2].
        return asfloat(raw) - 1.0f; // Float range [0, 1].
    }

    float2 next_float2() {
        return float2(next_float(), next_float());
    }

    float3 next_float3() {
        return float3(next_float(), next_float(), next_float());
    }
};

// https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
float interleaved_gradient_noise(int x, int y) {
    return fmod(52.9829189f * fmod(0.06711056f * float(x) + 0.00583715f * float(y), 1.0f), 1.0f);
}

float min(float3 x) {
    return min(x.x, min(x.y, x.z));
}

float max(float3 x) {
    return max(x.x, max(x.y, x.z));
}

// [0, 1] -> [-x, x]
template<typename Float>
Float interval_unit_to_symmetric(Float val, float x) {
    return ((val - 0.5f) * 2.0f) * x;
}

// https://www.elopezr.com/the-art-of-packing-data/
uint32_t pack_float4_to_rgba8unorm(float4 val) {
    const uint4 uval = uint4(val * 255.0f + 0.5f);
    return pack_u8(uval);
}

float4 unpack_rgba8unorm_to_float4(uint32_t packed) {
    return float4(unpack_u8u32(packed)) / 255.0f;
}

float2 pack_normal_octahedral(float3 normal) {
    normal /= (abs(normal.x) + abs(normal.y) + abs(normal.z));
    normal.xy = normal.z >= 0.0f ? normal.xy :
        (1.0f - abs(normal.yx)) * select(normal.xy >= 0.0f, 1.0f, -1.0f);
    normal.xy = normal.xy * 0.5f + 0.5f;
    return normal.xy;
}

float3 unpack_normal_octahedral(float2 packed) {
    packed = packed * 2.0f - 1.0f;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(packed.xy, 1.0f - abs(packed.x) - abs(packed.y));
    const float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0f, -t, t);
    return normalize(n);
}

uint16_t pack_float2_to_rg8snorm(float2 value) {
    const int2 ivalue = int2(round(value * 127.0f));
    return uint16_t((ivalue.g << 8) | ivalue.r);
}

float2 unpack_rg8snorm_to_float2(uint16_t packed) {
    const int ri = (int(packed) << 24) >> 24;
    const int gi = (int(packed) << 16) >> 24;
    return float2(ri, gi) / 127.0f;
}

// These SRGB functions are approximations.
float3 srgb_to_linear(float3 color) {
    return pow(color, 2.2f);
}

float3 linear_to_srgb(float3 color) {
    return pow(color, 1.0f / 2.2f);
}

float luminance(float3 linear_color) {
    return dot(linear_color, float3(0.2127f, 0.7152f, 0.0722f));
}

// A Fast and Robust Method for Avoiding Self-Intersection, Carsten Wächter, Nikolaus Binder.
// https://link.springer.com/content/pdf/10.1007/978-1-4842-4427-2_6
float3 offset_ray(float3 position, float3 normal) {
    const float origin = 1.0f / 32.0f;
    const float float_scale = 1.0f / 65536.0f;
    const float int_scale = 256.0f;

    const int3 offset_int = int3(
        int(int_scale * normal.x),
        int(int_scale * normal.y),
        int(int_scale * normal.z)
    );

    const float3 pos_int = float3(
        asfloat(asint(position.x) + (position.x < 0.0 ? -offset_int.x : offset_int.x)),
        asfloat(asint(position.y) + (position.y < 0.0 ? -offset_int.y : offset_int.y)),
        asfloat(asint(position.z) + (position.z < 0.0 ? -offset_int.z : offset_int.z))
    );

    return float3(
        abs(position.x) < origin ? position.x + float_scale * normal.x : pos_int.x,
        abs(position.y) < origin ? position.y + float_scale * normal.y : pos_int.y,
        abs(position.z) < origin ? position.z + float_scale * normal.z : pos_int.z
    );
}

// Building an Orthonormal Basis, Revisited, Tom Duff, James Burgess,
// Per Christensen, Christophe Hery, Andrew Kensler, Max Liani, and Ryusuke Villemin:
// https://www.jcgt.org/published/0006/01/01/
void compute_basis(float3 normal, out float3 tangent1, out float3 tangent2) {
    const float nz_sign = sign(normal.z);
    const float a = -1.0f / (nz_sign + normal.z);
    const float b = normal.x * normal.y * a;
    tangent1 = float3(1.0f + nz_sign * normal.x * normal.x * a, nz_sign * b, -nz_sign * normal.x);
    tangent2 = float3(b, nz_sign + normal.y * normal.y * a, -normal.y);
}

float3 change_basis(float3 vec, float3 direction) {
    float3 t1;
    float3 t2;
    compute_basis(direction, t1, t2);

    return float3(t1 * vec.x + t2 * vec.y + direction * vec.z);
}

// Doesn't need additional linear -> SRGB.
// http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 tonemap_hejl_burgess_dawson(float3 color) {
   const float3 x = max(0.0f, color - 0.004f);
   return (x * (6.2f * x + 0.5f)) /
          (x * (6.2f * x + 1.7f) + 0.06f);
}

// https://mynameismjp.wordpress.com/2012/10/28/msaa-resolve-filters/
// https://en.wikipedia.org/wiki/Mitchell%E2%80%93Netravali_filters
float filter_mitchell_netravali(float x, float B = 1.0f / 3.0f, float C = 1.0f / 3.0f) {
    float y = 0.0f;
    const float x2 = x * x;
    const float x3 = x * x * x;

    if (x < 1.0f)
    {
        y = (12.0f - 9.0f * B - 6.0f * C) * x3 +
            (-18.0f + 12.0f * B + 6.0f * C) * x2 +
            (6.0f - 2.0f * B);
    }
    else if (x < 2.0f)
    {
        y = (-B - 6.0f * C) * x3 +
            (6.0f * B + 30.0f * C) * x2 +
            (-12.0f * B - 48.0f * C) * x +
            (8.0f * B + 24.0f * C);
    }

    return y / 6.0f;
}

// https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
// See https://vec3.ca/posts/bicubic-filtering-in-fewer-taps for more details.
float4 sample_texture_catmull_rom(
    Texture2D<float4> tex,
    SamplerState linear_sampler,
    float2 uv,
    float2 tex_size
) {
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate.
    // We'll do this by rounding down the sample location to get the exact center of our
    // "starting" texel. The starting texel will be at location [1, 1] in the grid,
    // where [0, 0] is the top left corner.
    const float2 sample_pos = uv * tex_size;
    const float2 tex_pos1 = floor(sample_pos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample
    // location, which we'll feed into the Catmull-Rom spline function to get our filter weights.
    const float2 f = sample_pos - tex_pos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    const float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    const float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    const float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    const float2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    const float2 w12 = w1 + w2;
    const float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture.
    float2 tex_pos0 = tex_pos1 - 1.0f;
    float2 tex_pos3 = tex_pos1 + 2.0f;
    float2 tex_pos12 = tex_pos1 + offset12;

    tex_pos0 /= tex_size;
    tex_pos3 /= tex_size;
    tex_pos12 /= tex_size;

    float4 result = 0.0f;

    result += tex.SampleLevel(linear_sampler, float2(tex_pos0.x, tex_pos0.y), 0.0f) * w0.x * w0.y;
    result += tex.SampleLevel(linear_sampler, float2(tex_pos12.x, tex_pos0.y), 0.0f) * w12.x * w0.y;
    result += tex.SampleLevel(linear_sampler, float2(tex_pos3.x, tex_pos0.y), 0.0f) * w3.x * w0.y;

    result += tex.SampleLevel(linear_sampler, float2(tex_pos0.x, tex_pos12.y), 0.0f) * w0.x * w12.y;
    result += tex.SampleLevel(linear_sampler, float2(tex_pos12.x, tex_pos12.y), 0.0f) * w12.x * w12.y;
    result += tex.SampleLevel(linear_sampler, float2(tex_pos3.x, tex_pos12.y), 0.0f) * w3.x * w12.y;

    result += tex.SampleLevel(linear_sampler, float2(tex_pos0.x, tex_pos3.y), 0.0f) * w0.x * w3.y;
    result += tex.SampleLevel(linear_sampler, float2(tex_pos12.x, tex_pos3.y), 0.0f) * w12.x * w3.y;
    result += tex.SampleLevel(linear_sampler, float2(tex_pos3.x, tex_pos3.y), 0.0f) * w3.x * w3.y;

    return result;
}

// https://github.com/playdeadgames/temporal/blob/master/Assets/Shaders/TemporalReprojection.shader
float3 clip_aabb_center(float3 aabb_min, float3 aabb_max, float3 p) {
        // NOTE: only clips towards AABB center (but fast!).
        const float3 p_clip = 0.5f * (aabb_max + aabb_min);
        const float3 e_clip = 0.5f * (aabb_max - aabb_min) + 0.0001f;

        const float3 v_clip = p - p_clip;
        const float3 v_unit = v_clip / e_clip;
        const float3 a_unit = abs(v_unit);
        const float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

        if (ma_unit > 1.0f) {
            return p_clip + v_clip / ma_unit;
        } else {
            return p; // Point inside AABB.
        }
}

float3 quat_rotate(float4 quat, float3 vec) {
    const float3 v = float3(quat.y, quat.z, quat.w);
    const float3 uv = cross(v, vec);
    const float3 uuv = cross(v, uv);

    return vec + (uuv + uv * quat.x) * 2.0f;
}

struct BarycentricData {
    float3 lambda;
    float3 dldx;
    float3 dldy;
    float interp_inv_w;
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
BarycentricData calc_barycentric_data(
    float4 pos_clip0,
    float4 pos_clip1,
    float4 pos_clip2,
    float2 pixel_ndc,
    float2 two_over_texture_size
) {
    const float3 inv_w012 = rcp(float3(pos_clip0.w, pos_clip1.w, pos_clip2.w));

    const float2 ndc0 = pos_clip0.xy * inv_w012.x;
    const float2 ndc1 = pos_clip1.xy * inv_w012.y;
    const float2 ndc2 = pos_clip2.xy * inv_w012.z;

    const float inv_det = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));

    const float2 delta_ndc0 = pixel_ndc - ndc0;
    const float2 delta_ndc1 = pixel_ndc - ndc1;
    const float2 delta_ndc2 = pixel_ndc - ndc2;

    float3 lambda;
    lambda.y = determinant(float2x2(delta_ndc2, delta_ndc0)) * inv_det;
    lambda.z = determinant(float2x2(delta_ndc0, delta_ndc1)) * inv_det;
    lambda.x = 1.0f - lambda.y - lambda.z;

    const float3 dldx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * inv_det;
    const float3 dldy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * inv_det;

    const float interp_inv_w =
        inv_w012.x + delta_ndc0.x * dot(inv_w012, dldx) + delta_ndc0.y * dot(inv_w012, dldy);
    const float interp_w = rcp(interp_inv_w);

    const float3 interp_w_mul_inv_w012 = interp_w * inv_w012;

    BarycentricData result;

    result.lambda = interp_w_mul_inv_w012 * lambda; // Pre-perspective bary.
    result.dldx = two_over_texture_size.x * interp_w_mul_inv_w012 * dldx;
    result.dldy = -two_over_texture_size.y * interp_w_mul_inv_w012 * dldy;
    result.interp_inv_w = interp_inv_w;

    return result;
}

struct InterpolatedData2D {
    float2 c;
    float2 dcdx;
    float2 dcdy;
};

InterpolatedData2D interpolate_2d(BarycentricData data, float2 attr0, float2 attr1, float2 attr2) {
    InterpolatedData2D result;

    result.c = attr0 * data.lambda.x + attr1 * data.lambda.y + attr2 * data.lambda.z;

    const float2 delta0 = attr0 - result.c;
    const float2 delta1 = attr1 - result.c;
    const float2 delta2 = attr2 - result.c;

    result.dcdx = delta0 * data.dldx.x + delta1 * data.dldx.y + delta2 * data.dldx.z;
    result.dcdy = delta0 * data.dldy.x + delta1 * data.dldy.y + delta2 * data.dldy.z;

    return result;
}

struct InterpolatedData3D {
    float3 c;
    float3 dcdx;
    float3 dcdy;
};

InterpolatedData3D interpolate_3d(BarycentricData data, float3 attr0, float3 attr1, float3 attr2) {
    InterpolatedData3D result;

    result.c = attr0 * data.lambda.x + attr1 * data.lambda.y + attr2 * data.lambda.z;

    const float3 delta0 = attr0 - result.c;
    const float3 delta1 = attr1 - result.c;
    const float3 delta2 = attr2 - result.c;

    result.dcdx = delta0 * data.dldx.x + delta1 * data.dldx.y + delta2 * data.dldx.z;
    result.dcdy = delta0 * data.dldy.x + delta1 * data.dldy.y + delta2 * data.dldy.z;

    return result;
}
