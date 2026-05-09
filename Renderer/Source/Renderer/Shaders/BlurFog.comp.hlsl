#include "Common.hlsli"

ConstantBuffer<UniformData> uniformBuffer;
Texture2D<float> inImage;
[[vk::image_format("r16f")]]
RWTexture2D<float> outImage;

[[vk::push_constant]]
PushConstantsFogBlur pushConstants;

// Sigma = 3.0;
static float GAUSS_WEIGHTS[] =
{
    0.106289,
    0.140321,
    0.165770,
    0.175240,
    0.165770,
    0.140321,
    0.106289,
};

// Gaussian blur with 2 passes (vertical and horizontal).
[numthreads(RENDERER_FOG_BLUR_WORKGROUP_SIZE_X, RENDERER_FOG_BLUR_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 imageSize = uint2(uniformBuffer.renderWidth, uniformBuffer.renderHeight);

    if (dtid.x >= imageSize.x || dtid.y >= imageSize.y)
    {
        return;
    }

    float result = 0.0;
    float weightSum = 0.0;

    // 7 samples in horizontal and vertical direction (separate passes).
    for (int i = -3; i <= 3; ++i)
    {
        int2 sampleCoord = dtid.xy + select(pushConstants.horizontal == 1, int2(i, 0),  int2(0, i));
        sampleCoord = clamp(sampleCoord, int2(0, 0), imageSize - 1);

        const float weight = GAUSS_WEIGHTS[3 + i];

        result += weight * inImage[sampleCoord];
        weightSum += weight;
    }

    outImage[dtid.xy] = result / weightSum;
}
