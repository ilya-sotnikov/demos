#include "Common.hlsli"
#include "Math.hlsli"

ConstantBuffer<UniformData> uniformBuffer;
Texture2D renderImage;
Texture2D depthImage;
Texture2D velocityImage;
Texture2D prevResolvedRenderImage;
RWTexture2D<float3> resolvedRenderImageRW;
SamplerState linearSampler;

// Took an implementation from this article:
// https://alextardif.com/TAA.html

// Additional resources that explain these techniques in more depth:
// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/
// https://advances.realtimerendering.com/s2014/epic/TemporalAA.pptx
// https://graphicrants.blogspot.com/2013/12/tone-mapping.html
// http://behindthepixels.io/assets/files/TemporalAA.pdf
// https://developer.download.nvidia.com/gameworks/events/GDC2016/msalvi_temporal_supersampling.pdf
// https://github.com/playdeadgames/temporal

// TODO: thin stuff flickers.
// TODO: changes color a little.
// TODO: more explanations in comments.
// TODO: optimize.

// TODO: now that we have a visibility buffer, is it useful for history rejection?

[shader("compute")]
[numthreads(RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_X, RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    int2 renderImageSize = int2(uniformBuffer.renderWidth, uniformBuffer.renderHeight);

    if (dtid.x >= renderImageSize.x || dtid.y >= renderImageSize.y)
    {
        return;
    }

    if (uniformBuffer.taaEnable == 0)
    {
        resolvedRenderImageRW[dtid.xy] = renderImage[dtid.xy].rgb;
        return;
    }

    float3 currSampleTotal = 0.0;
    float currSampleWeight = 0.0;
    float3 neighborhoodMin = 10000.0;
    float3 neighborhoodMax = -10000.0;
    float3 m1 = 0.0;
    float3 m2 = 0.0;
    float closestDepth = 0.0;
    int2 closestDepthPixelPosition = int2(0, 0);

    // 3x3 pixel neighborhood grid.
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            int2 pixelPosition = dtid.xy + int2(x, y);
            pixelPosition = clamp(pixelPosition, 0, renderImageSize - 1);

            const float3 neighbor = max(0, renderImage[pixelPosition].rgb);

            // A filter over a neighborhood negates jitter.
            const float subSampleDistance = length(float2(x, y));
            const float subSampleWeight = FilterMitchellNetravali(subSampleDistance);

            currSampleTotal += neighbor * subSampleWeight;
            currSampleWeight += subSampleWeight;

            // For neighborhood clamping.
            neighborhoodMin = min(neighborhoodMin, neighbor);
            neighborhoodMax = max(neighborhoodMax, neighbor);

            // For variance clipping.
            m1 += neighbor;
            m2 += neighbor * neighbor;

            // For sampling the velocity buffer.
            const float currentDepth = depthImage[pixelPosition].r;
            if (currentDepth > closestDepth)
            {
                closestDepth = currentDepth;
                closestDepthPixelPosition = pixelPosition;
            }
        }
    }

    const float2 motionVector =
        velocityImage[closestDepthPixelPosition].xy * float2(0.5, 0.5);
    const float2 uv = float2(dtid.xy + 0.5) / float2(renderImageSize);
    const float2 prevUV = uv - motionVector;
    const float3 currSample = currSampleTotal / currSampleWeight;

    if (any(prevUV != saturate(prevUV)))
    {
        resolvedRenderImageRW[dtid.xy] = currSample;
        return;
    }

    float3 prevSample = SampleTextureCatmullRom(
        prevResolvedRenderImage,
        linearSampler,
        prevUV,
        float2(renderImageSize)
    ).rgb;

    const float sampleCountInv = 1.0 / 9.0;
    const float gamma = 1.0;
    const float3 mu = m1 * sampleCountInv;
    const float3 sigma = sqrt(abs((m2 * sampleCountInv) - (mu * mu)));
    const float3 minColor = mu - gamma * sigma;
    const float3 maxColor = mu + gamma * sigma;

    prevSample =
        ClipAabbCenter(minColor, maxColor, clamp(prevSample, neighborhoodMin, neighborhoodMax));

    const float3 currCompressed = currSample * rcp(Max(currSample) + 1.0);
    const float3 prevCompressed = prevSample * rcp(Max(prevSample) + 1.0);
    const float currLuminance = Luminance(currCompressed);
    const float prevLuminance = Luminance(prevCompressed);

    float currWeight = uniformBuffer.taaBlendWeight;
    float prevWeight = 1.0 - currWeight;
    currWeight *= 1.0 / (1.0 + currLuminance);
    prevWeight *= 1.0 / (1.0 + prevLuminance);

    const float3 result =
        (currSample * currWeight + prevSample * prevWeight) / max(currWeight + prevWeight, 0.00001);

    resolvedRenderImageRW[dtid.xy] = result;
}
