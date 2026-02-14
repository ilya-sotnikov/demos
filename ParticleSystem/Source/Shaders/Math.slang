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
