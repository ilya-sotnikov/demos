#include "Common.hlsli"
#include "Math.hlsli"

ConstantBuffer<UniformData> uniformBuffer;
StructuredBuffer<uint32_t> drawIndicesEarlyBuffer;
StructuredBuffer<uint32_t> drawIndicesLateBuffer;
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdEarlyBuffer;
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdLateBuffer;
StructuredBuffer<DrawData> drawDataBuffer;
StructuredBuffer<uint32_t> indexBuffer;
StructuredBuffer<Vertex> vertexBuffer;

SamplerState nearestSampler;
Texture2D<float> depthImage;
Texture2D<uint2> visibilityImage;
[[vk::image_format("r8")]]
RWTexture2D<float> outImage;

// https://kayru.org/articles/dssdo/
// https://github.com/kayru/dssdo/blob/master/dssdo.hlsl
// Except that for now we'll just calculate L0 (ambient occlusion).
// TODO: maybe revisit SH after adding point lights (and clustered lighting)?

[numthreads(RENDERER_SSAO_WORKGROUP_SIZE_X, RENDERER_SSAO_WORKGROUP_SIZE_Y, 1)]
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

    // TODO: reconstructing normals from the depth buffer should be faster:
    // https://turanszkij.wpcomstaging.com/2019/09/improved-normal-reconstruction-from-depth/
    const uint2 visibilityData = visibilityImage[dtid.xy * 2];
    uint rawDrawIdx = visibilityData.y;

    const bool isCullLate = rawDrawIdx & 0x80000000;
    rawDrawIdx &= 0x7fffffff;

    if (rawDrawIdx == 0)
    {
        outImage[dtid.xy] = 0.0;
        return;
    }

    --rawDrawIdx;

    const uint triangleIdx = visibilityData.x;

    uint drawIdx;
    if (isCullLate)
    {
        drawIdx = drawIndicesLateBuffer[rawDrawIdx];
    }
    else
    {
        drawIdx = drawIndicesEarlyBuffer[rawDrawIdx];
    }

    VkDrawIndexedIndirectCommand drawCmd;
    if (isCullLate)
    {
        drawCmd = drawCmdLateBuffer[rawDrawIdx];
    }
    else
    {
        drawCmd = drawCmdEarlyBuffer[rawDrawIdx];
    }

    const DrawData drawData = drawDataBuffer[drawIdx];

    const uint indexBufferIdx0 = drawCmd.firstIndex + (triangleIdx * 3 + 0);
    const uint indexBufferIdx1 = drawCmd.firstIndex + (triangleIdx * 3 + 1);
    const uint indexBufferIdx2 = drawCmd.firstIndex + (triangleIdx * 3 + 2);

    const uint idx0 = indexBuffer[indexBufferIdx0];
    const uint idx1 = indexBuffer[indexBufferIdx1];
    const uint idx2 = indexBuffer[indexBufferIdx2];

    const Vertex v0 = vertexBuffer[drawCmd.vertexOffset + idx0];
    const Vertex v1 = vertexBuffer[drawCmd.vertexOffset + idx1];
    const Vertex v2 = vertexBuffer[drawCmd.vertexOffset + idx2];

    const float3 posLocal0 = float3(v0.px, v0.py, v0.pz);
    const float3 posLocal1 = float3(v1.px, v1.py, v1.pz);
    const float3 posLocal2 = float3(v2.px, v2.py, v2.pz);

    const float3 posWorld0 =
        QuatRotate(drawData.orientation, posLocal0.xyz) * drawData.scale + drawData.position;
    const float3 posWorld1 =
        QuatRotate(drawData.orientation, posLocal1.xyz) * drawData.scale + drawData.position;
    const float3 posWorld2 =
        QuatRotate(drawData.orientation, posLocal2.xyz) * drawData.scale + drawData.position;

    const float4 posClip0 = mul(uniformBuffer.worldToClip, float4(posWorld0, 1.0));
    const float4 posClip1 = mul(uniformBuffer.worldToClip, float4(posWorld1, 1.0));
    const float4 posClip2 = mul(uniformBuffer.worldToClip, float4(posWorld2, 1.0));

    const float2 pixelUV = (dtid.xy + 0.5) / imageSize;
    float2 pixelNdc = pixelUV * 2.0 - 1.0;
    pixelNdc.y *= -1.0;

    const BarycentricData baryData = CalcBarycentricData(
        posClip0,
        posClip1,
        posClip2,
        pixelNdc,
        2.0 / imageSize
    );

    // TODO: will break on anything other than infinite reversed Z.
    const float pixelDepth = RENDERER_NEAR_PLANE * baryData.interpInvW;

    const float4 pixelClip = mul(uniformBuffer.clipToWorld, float4(pixelNdc, pixelDepth, 1.0));
    const float3 pixelWorld = pixelClip.xyz / pixelClip.w;

    const float3 n0 = UnpackNormalOctahedral(UnpackRG8SnormToFloat2(v0.normal));
    const float3 n1 = UnpackNormalOctahedral(UnpackRG8SnormToFloat2(v1.normal));
    const float3 n2 = UnpackNormalOctahedral(UnpackRG8SnormToFloat2(v2.normal));

    const InterpolatedData3D interpNormal = Interpolate3D(baryData, n0, n1, n2);

    // TODO: better sampling.
    // Points inside of the unit disk.
    const float2 points[] =
    {
        float2(-0.134, 0.044),
        float2(0.045, -0.431),
        float2(-0.537, 0.195),
        float2(0.525, -0.397),
        float2(0.895, 0.302),
        float2(-0.613, -0.408),
        float2(0.307, 0.822),
        float2(-0.819, 0.037),
        float2(0.376, 0.009),
        float2(-0.006, -0.103),
        float2(0.098, 0.393),
        float2(0.542, -0.218),
        float2(0.526, -0.183),
        float2(-0.529, -0.178),
        float2(0.066, -0.657),
        float2(-0.214, 0.288),
        float2(-0.689, -0.222),
        float2(-0.008, -0.212),
        float2(0.053, -0.863),
        float2(0.639, -0.558),
        float2(-0.255, 0.958),
        float2(-0.488, 0.473),
        float2(-0.592, -0.332),
        float2(0.080, 0.756),
        float2(-0.638, 0.319),
        float2(-0.663, 0.230),
        float2(0.235, -0.547),
        float2(0.164, -0.710),
        float2(-0.009, 0.493),
        float2(-0.322, 0.147),
        float2(-0.554, -0.725),
        float2(0.534, 0.157),
    };

    const int numSamples = ARRAY_SIZE(points);

    const float3 centerPosWorld = pixelWorld;
    const float3 centerNormalWorld = QuatRotate(drawData.orientation, normalize(interpNormal.c));
    const float centerDepth = distance(centerPosWorld, uniformBuffer.cameraPosition);

    const float radius = 0.25 / centerDepth; // TODO: uniform.
    const float maxDistanceInv = 1.0 / 2.0; // TODO: uniform.

    Pcg32 pcg32;
    pcg32.Init(uint64_t(dtid.y) << 32 | dtid.x, 0);

    float occlusion = 0;
    const float weight = 1.0 / numSamples;

    for (int i = 0; i < numSamples; ++i)
    {
        // Reflecting points inside of the unit disk by a random noise vector [-1, 1].
        const float2 offsetUV = reflect(points[i], pcg32.NextFloat2() * 2.0 - 1.0) * radius;

        const float2 sampleUV = pixelUV + offsetUV;
        const float sampleDepth = depthImage.SampleLevel(nearestSampler, sampleUV, 0);
        float2 sampleNdc = sampleUV * 2.0 - 1.0;
        sampleNdc.y *= -1.0;
        const float4 sampleClip = mul(
            uniformBuffer.clipToWorld,
            float4(sampleNdc, sampleDepth, 1.0)
        );
        const float3 sampleWorld = sampleClip.xyz / sampleClip.w;

        const float3 centerToSample = sampleWorld - centerPosWorld;
        const float dist = length(centerToSample);
        const float3 centerToSampleNormalized = centerToSample / dist;
        const float dp = dot(centerNormalWorld, centerToSampleNormalized);
        float attenuation = 1.0 - saturate(dist * maxDistanceInv);

        attenuation = attenuation * attenuation * step(0.1, dp);

        occlusion += sampleDepth > 0.0 ? attenuation * weight : 0.0;
    }

    outImage[dtid.xy] = occlusion;
    // TODO: half resolution 16 bit depth buffer?
}
