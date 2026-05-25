#include "Common.hlsli"
#include "Math.hlsli"
#include "PBR.hlsli"
#include "Sky.hlsli"

// https://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/

ConstantBuffer<UniformData> uniformBuffer;
StructuredBuffer<uint32_t> drawIndicesEarlyBuffer;
StructuredBuffer<uint32_t> drawIndicesLateBuffer;
StructuredBuffer<DrawIndexedIndirectCommand> drawCmdEarlyBuffer;
StructuredBuffer<DrawIndexedIndirectCommand> drawCmdLateBuffer;
StructuredBuffer<DrawData> drawDataBuffer;
StructuredBuffer<uint32_t> indexBuffer;
StructuredBuffer<Vertex> vertexBuffer;
StructuredBuffer<Material> materialBuffer;
SamplerState linearSampler;
SamplerState textureSampler;
SamplerComparisonState shadowSampler;
SamplerState shadowPcfJitterSampler;

Texture2DArray<float> shadowTexture;
Texture3D shadowPcfJitterTexture;
Texture2D<float> fogTexture;
Texture2D<uint2> visibilityTexture;
Texture2D<float> ambientOcclusionTexture;
[[vk::image_format("rg16f")]]
RWTexture2D<float2> velocityTextureRW;
RWTexture2D<float3> renderTextureRW;

// TODO: very messy.
#include "CalcShadow.hlsli"

// NOTE: full bindless is not practical yet since it messes up synchronization validation.
// For now it's only for read-only resources (textures).
[[vk::binding(0, 1)]]
Texture2D textures[];

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
    const float3x3 TBN = float3x3(-T * invMax, B * invMax, normalWorld);

    return normalize(mul(normalSampled, TBN)); // Mul order is intentional.
}

float4 SampleTex(uint32_t idx, InterpolatedData2D uv)
{
    return textures[NonUniformResourceIndex(idx)]
        .SampleGrad(textureSampler, uv.c, uv.dcdx, uv.dcdy);
}

[numthreads(RENDERER_RENDER_WORKGROUP_SIZE_X, RENDERER_RENDER_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    const int2 renderTextureSize = int2(uniformBuffer.renderWidth, uniformBuffer.renderHeight);

    if (dtid.x >= renderTextureSize.x || dtid.y >= renderTextureSize.y)
    {
        return;
    }

    const uint2 visibilityData = visibilityTexture[dtid.xy];
    uint rawDrawIdx = visibilityData.y;

    const bool isCullLate = rawDrawIdx & 0x80000000;
    rawDrawIdx &= 0x7fffffff;

    float2 pixelNdc = ((dtid.xy + 0.5) * uniformBuffer.renderTextureSizeInv) * 2.0 - 1.0;
    pixelNdc.y *= -1.0;

    if (rawDrawIdx == 0)
    {
        const float4 pos = mul(
            uniformBuffer.clipToWorld,
            float4(pixelNdc, 1.0, 1.0)
        );
        const float3 viewDirectionWorld = normalize(pos.xyz / pos.w - uniformBuffer.cameraPosition);

        renderTextureRW[dtid.xy] = CalcSky(viewDirectionWorld, uniformBuffer.sunDirectionWorld);
        velocityTextureRW[dtid.xy] = float2(0.0, 0.0);
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

    DrawIndexedIndirectCommand drawCmd;
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

    const BarycentricData baryData = CalcBarycentricData(
        posClip0,
        posClip1,
        posClip2,
        pixelNdc,
        2.0 * uniformBuffer.renderTextureSizeInv
    );

    // TODO: will break on anything other than infinite reversed Z.
    const float pixelDepth = RENDERER_NEAR_PLANE * baryData.interpInvW;

    const float4 pixelWorldHomo = mul(uniformBuffer.clipToWorld, float4(pixelNdc, pixelDepth, 1.0));
    const float3 pixelWorld = pixelWorldHomo.xyz / pixelWorldHomo.w;

    const float2 uv0 = float2(v0.u, v0.v);
    const float2 uv1 = float2(v1.u, v1.v);
    const float2 uv2 = float2(v2.u, v2.v);
    const InterpolatedData2D interpUV = Interpolate2D(baryData, uv0, uv1, uv2);

    const float3 n0 = UnpackNormalOctahedral(UnpackRG8SnormToFloat2(v0.normal));
    const float3 n1 = UnpackNormalOctahedral(UnpackRG8SnormToFloat2(v1.normal));
    const float3 n2 = UnpackNormalOctahedral(UnpackRG8SnormToFloat2(v2.normal));

    const InterpolatedData3D interpNormal = Interpolate3D(baryData, n0, n1, n2);

    const float3 normalWorld = normalize(QuatRotate(drawData.orientation, interpNormal.c));

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

    float3 normal = normalWorld;

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
        (kDiffuse * albedo.rgb / M_PIf + specular) * dotNormalLight * uniformBuffer.sunIntensity;

    float3 positionShadow;

    // Choosing cascade not by split distances, but by determining the smallest
    // cascade that contains this fragment.
    // TODO: somehow optimize? Maybe even ditch the idea above and choose by split distances.
    // TODO: better offsetting.
    const float normalOffsetScale =
        saturate(1.0 - dot(-uniformBuffer.sunDirectionWorld, normalWorld))
        * uniformBuffer.shadow.normalOffset;
    const float3 scaledNormalWorld = normalWorld * normalOffsetScale;
    int cascadeIdx = -1;
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        // Leaving a small room for PCF kernel, to avoid sampling outside the cascade.
        // TODO: hacky.
        const float MAX_VALUE = 0.99;

        const float4 shadowOffset = float4(
            scaledNormalWorld * uniformBuffer.shadow.texelSizes[i],
            0.0
        );
        positionShadow = mul(
            uniformBuffer.shadow.worldToClip[i],
            float4(pixelWorld, 1.0) + shadowOffset
        ).xyz;
        positionShadow.z += uniformBuffer.shadow.constantOffset / uniformBuffer.shadow.texelSizes[i];
        positionShadow.xy = positionShadow.xy * 0.5 + 0.5;

        const float minCoord = Min(positionShadow);
        const float maxCoord = Max(positionShadow);

        if ((minCoord >= 0.0) && (maxCoord <= MAX_VALUE))
        {
            cascadeIdx = i;
            break;
        }
    }

    const float shadow = cascadeIdx == -1 ? 1.0 :
        CalcShadow(
            uniformBuffer.shadow.enablePcf,
            uniformBuffer.shadow.pcfKernelScale,
            uniformBuffer.shadow.pcfKernelCascadeScales[cascadeIdx],
            positionShadow,
            float2(dtid.xy),
            dotNormalLight,
            cascadeIdx
        );

    float ambientOcclusion = 1.0;
    if (uniformBuffer.enableSSAO)
    {
        ambientOcclusion = ambientOcclusionTexture[dtid.xy];
    }

    const float3 ambient = albedo.rgb * uniformBuffer.ambientIntensity * ambientOcclusion;

    float fogFactor = 0.0;
    if (uniformBuffer.enableFog)
    {
        fogFactor = fogTexture.SampleLevel(
            linearSampler,
            (dtid.xy + 0.5) * uniformBuffer.renderTextureSizeInv,
            0
        );
    }

    float3 color = (
        ambient * (-uniformBuffer.sunDirectionWorld.y) +
        radianceOut * shadow +
        fogFactor
    ) * uniformBuffer.sunColor;

    switch (uniformBuffer.renderMode)
    {
    case RENDER_MODE_AMBIENT_OCCLUSION:
        color = ambientOcclusion;
        break;
    }

    renderTextureRW[dtid.xy] = color;

    // TODO: support movable objects (double-buffered draw data).
    const float3 prevPixelWorld = pixelWorld;
    const float4 prevPixelClip = mul(uniformBuffer.prevWorldToClip, float4(prevPixelWorld, 1.0));
    const float2 prevPixelNdc = prevPixelClip.xy / prevPixelClip.w;

    float2 velocity =
        (pixelNdc - uniformBuffer.taaJitter) - (prevPixelNdc - uniformBuffer.prevTaaJitter);
    velocity.y *= -1.0;

    // TODO: motion vectors only for dynamic objects.
    velocityTextureRW[dtid.xy] = velocity;
}
