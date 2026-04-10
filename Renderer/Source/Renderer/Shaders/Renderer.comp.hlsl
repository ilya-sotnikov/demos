#include "Common.hlsli"
#include "Math.hlsli"
#include "PBR.hlsli"

// https://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
// Derivations of interpolation formulas and stuff are in this article:
// https://chaojia.github.io/posts/21-11-29-vertex-attrib-interp/

ConstantBuffer<UniformData> uniformBuffer;
StructuredBuffer<uint32_t> drawIndicesEarlyBuffer;
StructuredBuffer<uint32_t> drawIndicesLateBuffer;
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdEarlyBuffer;
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdLateBuffer;
StructuredBuffer<DrawData> drawDataBuffer;
StructuredBuffer<uint32_t> indexBuffer;
StructuredBuffer<Vertex> vertexBuffer;
StructuredBuffer<Material> materialBuffer;
SamplerState textureSampler;
RaytracingAccelerationStructure tlas;

Texture2D<uint2> visibilityImage;
[[vk::image_format("rg16f")]]
RWTexture2D<float2> velocityImageRW;
RWTexture2D<float3> renderImageRW;

// NOTE: full bindless is not practical yet since it messes up synchronization validation.
// For now it's only for read-only resources (textures).
[[vk::binding(0, 1)]]
Texture2D textures[];

float CalcShadow(float3 posWorld, float3 sunDirectionWorld)
{
    RayDesc rayDesc;
    rayDesc.Origin = posWorld;
    // TODO: jitter and blur in screen space for soft shadows.
    rayDesc.Direction = -sunDirectionWorld;
    rayDesc.TMin = 0.01; // TODO: offset by geometric normal instead?
    rayDesc.TMax = 100.0;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE> rayQuery;
    rayQuery.TraceRayInline(
        tlas,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
        0xff,
        rayDesc
    );

    rayQuery.Proceed();

    return float(rayQuery.CommittedStatus() == COMMITTED_NOTHING);
}

// http://www.thetenthplanet.de/archives/1180
// https://github.com/ConfettiFX/The-Forge/blob/master/Common_3/Renderer/VisibilityBuffer2/Shaders/FSL/VisibilityBufferShadingUtilities.h.fsl
float3 PerturbNormal(
    float3 normalWorld,
    float3 normalSampled,
    float3 ddxPosWorld,
    float3 ddyPosWorld,
    float2 ddxUV,
    float2 ddyUV
)
{
    // Solve the linear system.
    const float3 ddyPosPerp = cross(ddyPosWorld, normalWorld);
    const float3 ddxPosPerp = cross(normalWorld, ddxPosWorld);
    const float3 T = ddyPosPerp * ddxUV.x + ddxPosPerp * ddyUV.x;
    const float3 B = ddyPosPerp * ddxUV.y + ddxPosPerp * ddyUV.y;

    // Construct a scale-invariant frame.
    const float invMax = rsqrt(max(dot(T, T), dot(B, B)));
    const float3x3 TBN = float3x3(T * invMax, B * invMax, normalWorld);

    return normalize(mul(normalSampled, TBN)); // Mul order is intentional.
}

float4 SampleTex(uint32_t idx, InterpolatedData2D uv)
{
    return textures[NonUniformResourceIndex(idx)]
        .SampleGrad(textureSampler, uv.c, uv.dcdx, uv.dcdy);
}

[shader("compute")]
[numthreads(RENDERER_RENDER_WORKGROUP_SIZE_X, RENDERER_RENDER_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    const int2 renderImageSize = int2(uniformBuffer.renderWidth, uniformBuffer.renderHeight);

    if (dtid.x >= renderImageSize.x || dtid.y >= renderImageSize.y)
    {
        return;
    }

    const uint2 visibilityData = visibilityImage[dtid.xy];
    uint rawDrawIdx = visibilityData.y;

    const bool isCullLate = rawDrawIdx & 0x80000000;
    rawDrawIdx &= 0x7fffffff;

    if (rawDrawIdx == 0)
    {
        renderImageRW[dtid.xy] = float3(0.7, 0.8, 0.9);
        velocityImageRW[dtid.xy] = float2(0.0, 0.0);
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
    const Material material = materialBuffer[drawData.materialIdx];

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

    float2 pixelNdc = ((dtid.xy + 0.5) / renderImageSize) * 2.0 - 1.0;
    pixelNdc.y *= -1.0;

    const BarycentricData baryData = CalcBarycentricData(
        posClip0,
        posClip1,
        posClip2,
        pixelNdc,
        2.0 / renderImageSize
    );

    // TODO: will break on anything other than infinite reversed Z.
    const float pixelDepth = RENDERER_NEAR_PLANE * baryData.interpInvW;

    const float4 pixelClip = mul(uniformBuffer.clipToWorld, float4(pixelNdc, pixelDepth, 1.0));
    const float3 pixelWorld = pixelClip.xyz / pixelClip.w;

    const float2 uv0 = float2(v0.u, v0.v);
    const float2 uv1 = float2(v1.u, v1.v);
    const float2 uv2 = float2(v2.u, v2.v);
    const InterpolatedData2D interpUV = Interpolate2D(baryData, uv0, uv1, uv2);

    const float3 n0 = UnpackNormalOctahedral(float2(v0.nx, v0.ny));
    const float3 n1 = UnpackNormalOctahedral(float2(v1.nx, v1.ny));
    const float3 n2 = UnpackNormalOctahedral(float2(v2.nx, v2.ny));

    const InterpolatedData3D interpNormal = Interpolate3D(baryData, n0, n1, n2);

    float3 normal = QuatRotate(drawData.orientation, normalize(interpNormal.c));

    float4 albedo = material.albedoFactor;
    if (material.albedoTexIdx > 0)
    {
        albedo *= SampleTex(material.albedoTexIdx, interpUV);
    }

    float2 metallicRoughness = float2(material.metallicFactor, material.roughnessFactor);
    if (material.albedoTexIdx > 0)
    {
        metallicRoughness *= SampleTex(material.metallicRoughnessTexIdx, interpUV).gr;
    }
    const float metallic = metallicRoughness.r;
    const float roughness = metallicRoughness.g;

    if (material.normalTexIdx > 0)
    {
        const float3 normalSampled = SampleTex(material.normalTexIdx, interpUV).xyz * 2.0 - 1.0;

        const InterpolatedData3D interpPosWorld =
            Interpolate3D(baryData, posWorld0, posWorld1, posWorld2);

        normal = PerturbNormal(
            normal,
            normalSampled,
            interpPosWorld.dcdx,
            interpPosWorld.dcdy,
            interpUV.dcdx,
            interpUV.dcdy
        );
    }

    const float3 fragmentToCamera = normalize(uniformBuffer.cameraPosition - pixelWorld);

    const float3 halfVecViewLight = normalize(fragmentToCamera - uniformBuffer.sunDirectionWorld);

    float3 fresnel0 = float3(0.04, 0.04, 0.04); // Good default for dielectrics.
    fresnel0 = lerp(fresnel0, albedo.rgb, metallic);

    const float dotNormalLight = max(0.0, dot(normal, -uniformBuffer.sunDirectionWorld));
    const float dotNormalView = max(0.0, dot(normal, fragmentToCamera));
    const float dotNormalHalf = max(0.0, dot(normal, halfVecViewLight));
    const float dotHalfView = max(0.0, dot(halfVecViewLight, fragmentToCamera));

    const float normalDistr = NormalDistributionGGX(dotNormalHalf, roughness);
    const float geometry = GeometrySmith(dotNormalView, dotNormalLight, roughness);
    const float3 fresnel = FresnelSchlick(dotHalfView, fresnel0);

    const float3 num = normalDistr * geometry * fresnel;
    // Adding a small constant to prevent division by zero.
    const float3 denom = 4.0 * dotNormalView * dotNormalLight + 0.0001;
    const float3 specular = num / denom;

    const float3 kSpecular = fresnel;
    float3 kDiffuse = 1.0 - kSpecular;
    kDiffuse *= 1.0 - metallic;

    const float3 radianceOut =
        (kDiffuse * albedo.rgb / M_PIf + specular) * uniformBuffer.sunIntensity * dotNormalLight;

    float shadow = 1.0;
    if (dotNormalLight > 0.0)
    {
        shadow = CalcShadow(pixelWorld, uniformBuffer.sunDirectionWorld);
    }

    const float3 ambient = albedo.rgb * uniformBuffer.ambientIntensity;

    const float3 color = radianceOut * shadow + ambient;

    renderImageRW[dtid.xy] = color;

    // TODO: support movable objects (double-buffered draw data).
    const float3 prevPixelWorld = pixelWorld;
    const float4 prevPixelClip = mul(uniformBuffer.prevWorldToClip, float4(prevPixelWorld, 1.0));
    const float2 prevPixelNdc = prevPixelClip.xy / prevPixelClip.w;

    float2 velocity =
        (pixelNdc - uniformBuffer.taaJitter) - (prevPixelNdc - uniformBuffer.prevTaaJitter);
    velocity.y *= -1.0;

    velocityImageRW[dtid.xy] = velocity;
}
