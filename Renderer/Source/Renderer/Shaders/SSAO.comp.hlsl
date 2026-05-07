#include "Common.hlsli"
#include "Math.hlsli"

// Referenced this:
// Robust Screen Space Ambient Occlusion in 1 ms in 1080p on PS4, Wojciech Sterna, GPU Zen.
// https://www.4rknova.com/blog/2017/01/01/vogel
// https://research.nvidia.com/sites/default/files/pubs/2011-08_The-Alchemy-Screen-space/paper.pdf

ConstantBuffer<UniformData> uniformBuffer;
StructuredBuffer<uint32_t> drawIndicesEarlyBuffer;
StructuredBuffer<uint32_t> drawIndicesLateBuffer;
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdEarlyBuffer;
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdLateBuffer;
StructuredBuffer<DrawData> drawDataBuffer;
StructuredBuffer<uint32_t> indexBuffer;
StructuredBuffer<Vertex> vertexBuffer;

SamplerState nearestSampler;
Texture2D<float> depthViewImage;
Texture2D<uint2> visibilityImage;
[[vk::image_format("r8")]]
RWTexture2D<float> outImage;

static const float MAX_RADIUS_SCREEN = 0.1;
static const float RADIUS_WORLD = 0.3;
static const int SAMPLES_COUNT = 16;

float2 VogelDiskOffset(int sampleIndex, float phi)
{
    const float GOLDEN_ANGLE = 2.4;

    const float r = sqrt(float(sampleIndex) + 0.5) / sqrt(SAMPLES_COUNT);
    const float theta = sampleIndex * GOLDEN_ANGLE + phi;

    float sinTheta;
    float cosTheta;
    sincos(theta, sinTheta, cosTheta);

    return float2(r * cosTheta, r * sinTheta);
}

float3 CalcPosView(float2 uv)
{
    float3 pos = float3(uv, -1.0);
    pos.xy = pos.xy * 2.0 - 1.0;
    pos.y *= -1.0;
    pos.xy *= uniformBuffer.viewToClipInv0011;
    pos *= -depthViewImage.SampleLevel(nearestSampler, uv, 0);
    return pos;
}

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

    // TODO: unjitter uv or something, TAA messes it up.

    // TODO: reconstructing normals from the depth buffer may be faster
    // and usable in forward pipelines:
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

    const float2 imageSizeInv = uniformBuffer.ambientOcclusionImageSizeInv;

    const float2 pixelUV = (dtid.xy + 0.5) * imageSizeInv;
    float2 pixelNdc = pixelUV * 2.0 - 1.0;
    pixelNdc.y *= -1.0;

    const BarycentricData baryData = CalcBarycentricData(
        posClip0,
        posClip1,
        posClip2,
        pixelNdc,
        2.0 * imageSizeInv
    );

    const float3 posView = CalcPosView(pixelUV);

    const float3 n0 = UnpackNormalOctahedral(UnpackRG8SnormToFloat2(v0.normal));
    const float3 n1 = UnpackNormalOctahedral(UnpackRG8SnormToFloat2(v1.normal));
    const float3 n2 = UnpackNormalOctahedral(UnpackRG8SnormToFloat2(v2.normal));

    const InterpolatedData3D interpNormal = Interpolate3D(baryData, n0, n1, n2);

    const float3 normalWorld = QuatRotate(drawData.orientation, normalize(interpNormal.c));
    const float3 normalView = mul(uniformBuffer.worldToView, float4(normalWorld, 0.0)).xyz;

    const float noisePixel = InterleavedGradientNoise(dtid.x, dtid.y);

    float2 radiusScreen = RADIUS_WORLD / posView.z;
    // To prevent radiusScreen getting too large for pixels that are close to the camera.
    radiusScreen = min(radiusScreen, MAX_RADIUS_SCREEN);
    // Vogel disk returns samples in the [0, 1] unit disk, multiplying by aspect ratio
    // preserves this unit disk, otherwise it would be an ellipse.
    radiusScreen.y *= uniformBuffer.aspect;

    float occlusion = 0.0;

    for (int i = 0; i < SAMPLES_COUNT; ++i)
    {
        const float2 sampleOffset = VogelDiskOffset(i, 2.0 * M_PIf * noisePixel);
        const float2 sampleUV = pixelUV + radiusScreen * sampleOffset;
        const float3 sampleView = CalcPosView(sampleUV);

        const float3 v = sampleView - posView;

        // Tweak beta to reduce self-shadowing.
        const float beta = 1e-3;
        occlusion += max(0.0, dot(v, normalView) + beta * posView.z) / (dot(v, v) + 1e-5);
    }

    occlusion = pow(1.0 - saturate(occlusion / SAMPLES_COUNT), 2);

    outImage[dtid.xy] = occlusion;
}
