#include "Common.hlsli"

ConstantBuffer<UniformData> uniformBuffer;
SamplerState linearSampler;
Texture2D<float> depthImage;
Texture2D<float> inImage;
[[vk::image_format("r8")]]
RWTexture2D<float> outImage;

[numthreads(RENDERER_SSAO_BLUR_WORKGROUP_SIZE_X, RENDERER_SSAO_BLUR_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 imageSize = uint2(
        uniformBuffer.ambientOcclusionWidth,
        uniformBuffer.ambientOcclusionHeight
    );

    if (dtid.x >= imageSize.x || dtid.y >= imageSize.y)
    {
        return;
    }

    const float EPSILON = 1e-5;

    const uint2 center = dtid.xy;
    const float centerDepth = RENDERER_NEAR_PLANE / (depthImage[center * 2] + EPSILON);

    float result = 0.0;
    float kSum = 0.0;

    // TODO: better filter.
    // TODO: optimize (i.e. proper linear sampling, maybe separate x/y, maybe shared memory).
    // TODO: look into upsampling.
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            int2 sampleCoord = center + int2(x, y);
            sampleCoord = clamp(sampleCoord, int2(0, 0), imageSize - 1);
            float k = exp(-(x * x + y * y) * 0.5); // TODO: LUT?
            const float sampleDepth = RENDERER_NEAR_PLANE / depthImage[sampleCoord * 2] + EPSILON;
            k = k / (abs(centerDepth - sampleDepth) + EPSILON); // Somewhat depth aware i guess.
            result += k * inImage.SampleLevel(
                linearSampler,
                float2(sampleCoord + 0.5) / imageSize,
                0
            );
            kSum += k;
        }
    }

    outImage[dtid.xy] = result / kSum;
}
