#include "Common.hlsli"

// Referenced this:
// Robust Screen Space Ambient Occlusion in 1 ms in 1080p on PS4, Wojciech Sterna, GPU Zen.

ConstantBuffer<UniformData> uniformBuffer;
SamplerState nearestSampler;

Texture2D<float> depthTexture;
Texture2D<float> depthViewTexture;
Texture2D<float> inTexture;
[[vk::image_format("r8")]]
RWTexture2D<float> outTexture;

// Bilinear depth-aware upsampling.
[numthreads(RENDERER_SSAO_UPSAMPLE_WORKGROUP_SIZE_X, RENDERER_SSAO_UPSAMPLE_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 textureSize = uint2(uniformBuffer.renderWidth, uniformBuffer.renderHeight);

    if (dtid.x >= textureSize.x || dtid.y >= textureSize.y)
    {
        return;
    }

    const float depthView = -RENDERER_NEAR_PLANE / (depthTexture[dtid.xy] + 1e-5);

    const float2 textureSizeInv = uniformBuffer.renderTextureSizeInv;
    const float2 pixelCoord = dtid.xy + 0.5;
    const float2 pixelUV = pixelCoord * textureSizeInv;

    // NOTE: According to the Vulkan spec:
    // SPIR-V instructions with Gather in the name return a vector derived from 4 texels
    // in the base level of the image view. The rules for the VK_FILTER_LINEAR minification
    // filter are applied to identify the four selected texels.
    // Just below there's a formula for selecting texels, we are just interested in order:
    // RGBA = 01, 11, 10, 00
    // We'll remap them to:
    // RGBA = 00, 10, 01, 11
    const float4 depthsView = depthViewTexture.GatherRed(nearestSampler, pixelUV).abrg;
    const float4 depthViewDiffs = abs(depthView.rrrr - depthsView);

    const float4 occlusions = inTexture.GatherRed(nearestSampler, pixelUV).abrg;

    // https://en.wikipedia.org/wiki/Bilinear_interpolation#On_the_unit_square
    const float2 fractional = frac(pixelCoord);
    const float a = (1.0 - fractional.x) * (1.0 - fractional.y);
    const float b = fractional.x * (1.0 - fractional.y);
    const float c = (1.0 - fractional.x) * fractional.y;
    const float d = fractional.x * fractional.y;

    float result = 0.0;
    float weightSum = 0.0;

    const float weight00 = a / (depthViewDiffs.x + 1e-5);
    result += weight00 * occlusions.x;
    weightSum += weight00;

    const float weight10 = b / (depthViewDiffs.y + 1e-5);
    result += weight10 * occlusions.y;
    weightSum += weight10;

    const float weight01 = c / (depthViewDiffs.z + 1e-5);
    result += weight01 * occlusions.z;
    weightSum += weight01;

    const float weight11 = d / (depthViewDiffs.w + 1e-5);
    result += weight11 * occlusions.w;
    weightSum += weight11;

    result /= weightSum;

    outTexture[dtid.xy] = result;
}
