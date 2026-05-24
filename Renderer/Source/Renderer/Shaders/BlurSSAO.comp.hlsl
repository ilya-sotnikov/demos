#include "Common.hlsli"

// Referenced this:
// Robust Screen Space Ambient Occlusion in 1 ms in 1080p on PS4, Wojciech Sterna, GPU Zen.

ConstantBuffer<UniformData> uniformBuffer;
Texture2D<float> depthViewTexture;
Texture2D<float> inTexture;
[[vk::image_format("r8")]]
RWTexture2D<float> outTexture;

[[vk::push_constant]]
PushConstantsSsaoBlur pushConstants;

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

// Bilateral depth-aware filter with 2 passes (vertical and horizontal).
// NOTE: although bilateral filters are not separable, it's mostly fine and definitely faster.
[numthreads(RENDERER_SSAO_BLUR_WORKGROUP_SIZE_X, RENDERER_SSAO_BLUR_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 textureSize = uint2(
        uniformBuffer.ambientOcclusionWidth,
        uniformBuffer.ambientOcclusionHeight
    );

    if (dtid.x >= textureSize.x || dtid.y >= textureSize.y)
    {
        return;
    }

    const float depth = depthViewTexture[dtid.xy];

    float result = 0.0;
    float weightSum = 0.0;

    // 7 samples in horizontal and vertical direction (separate passes).
    for (int i = -3; i <= 3; ++i)
    {
        int2 sampleCoord =
            dtid.xy + i * int2(pushConstants.pixelOffsetX, pushConstants.pixelOffsetY);
        sampleCoord = clamp(sampleCoord, int2(0, 0), textureSize - 1);
        const float sampleDepth = depthViewTexture[sampleCoord];

        float deltaDepth = 0.1 * abs(depth - sampleDepth);
        deltaDepth *= deltaDepth;

        const float weight = GAUSS_WEIGHTS[3 + i] / (deltaDepth + 1e-5);

        result += weight * inTexture[sampleCoord];
        weightSum += weight;
    }

    outTexture[dtid.xy] = result / weightSum;
}
