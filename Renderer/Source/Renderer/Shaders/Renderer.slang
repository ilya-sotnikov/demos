#include "Common.slang"
#include "Math.slang"
#include "PBR.slang"

struct VertexOutput
{
    float4 positionClip : SV_Position;
    float3 positionWorld;
    float3 normalWorld;
    float4 tangent;
    float2 uv;
    nointerpolation uint materialIdx;
};

// TODO: consider a visibility buffer?
// Depth prepass does not solve overdraw due to small triangles and it will kill RT
// performance, so RT needs to be done in screen space, and for shadows I need albedo
// (for ambient lighting) so it's already deferred-lite but very scuffed since PBR
// calculations are still in forward pass.

[shader("vertex")]
void VertexMain(
    uint vertexId : SV_VertexID,
    uint vertexOffset : SV_StartVertexLocation,
    uint rawDrawIdx : SV_DrawIndex,
    uniform PushConstants pushConstants,
    out VertexOutput output
)
{
    const Ptr<UniformData, Access.Read> uniformData = pushConstants.uniformData;

    const uint drawIdx = pushConstants.drawCmds2[rawDrawIdx].idx;
    const DrawData drawData = pushConstants.drawData[drawIdx];
    const Vertex vertex = pushConstants.vertices[vertexId + vertexOffset];

    const float4 positionWorld =
        mul(drawData.localToWorld, float4(vertex.px, vertex.py, vertex.pz, 1.0));

    output.positionClip = mul(uniformData.worldToClip, positionWorld);

    output.positionWorld = positionWorld.xyz;

    // TODO: doesn't work on non-uniform scale.
    output.normalWorld = mul(drawData.localToWorld, float4(vertex.nx, vertex.ny, vertex.nz, 0.0)).xyz;

    output.tangent = float4(vertex.tx, vertex.ty, vertex.tz, vertex.tw);

    output.uv = float2(vertex.u, vertex.v);
    output.materialIdx = drawData.materialIdx;
}

float4 SampleTex(uint32_t idx, float2 uv)
{
    return textures[NonUniformResourceIndex(idx)].Sample(samplers[SAMPLER_TEXTURE_IDX], uv);
}

struct FragmentOutput
{
    float4 color : SV_Target0;
    float4 albedo : SV_Target1;
}

[shader("fragment")]
[earlydepthstencil]
void FragmentMain(
    VertexOutput input,
    uniform PushConstants pushConstants,
    out FragmentOutput output
) : SV_Target
{
    const Ptr<UniformData, Access.Read> uniformData = pushConstants.uniformData;
    const Material material = pushConstants.materials[input.materialIdx];

    float4 albedo = material.albedoFactor;
    if (material.albedoTexIdx > 0)
    {
        albedo *= SampleTex(material.albedoTexIdx, input.uv);
    }

    float2 metallicRoughness = float2(material.metallicFactor, material.roughnessFactor);
    if (material.albedoTexIdx > 0)
    {
        metallicRoughness *= SampleTex(material.metallicRoughnessTexIdx, input.uv).gr;
    }
    const float metallic = metallicRoughness.r;
    const float roughness = metallicRoughness.g;

    const float3 normalInterpolated = normalize(input.normalWorld);
    float3 normal = normalInterpolated;

    if (material.normalTexIdx > 0)
    {
        const float3 normalSample = SampleTex(material.normalTexIdx, input.uv).xyz * 2.0 - 1.0;

        const float3 tangent = normalize(input.tangent.xyz);
        // w is handedness.
        const float3 bitangent = cross(normal, input.tangent.xyz) * input.tangent.w;

        normal = normalize(
            normalSample.r * input.tangent.xyz +
            normalSample.g * bitangent +
            normalSample.b * normal
        );
    }

    const float3 fragmentToCamera = normalize(uniformData.cameraPosition - input.positionWorld);

    const float3 halfVecViewLight = normalize(fragmentToCamera - uniformData.sunDirectionWorld);

    float3 fresnel0 = float3(0.04, 0.04, 0.04); // Good default for dielectrics.
    fresnel0 = lerp(fresnel0, albedo.rgb, metallic);

    const float dotNormalLight = max(0.0, dot(normal, -uniformData.sunDirectionWorld));
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
    float3 kDiffuse = float3(1.0) - kSpecular;
    kDiffuse *= 1.0 - metallic;

    const float3 radianceOut = (kDiffuse * albedo.rgb / M_PIf + specular) * dotNormalLight;

    const float3 color = radianceOut;

    output.color = float4(color, 1.0);
    output.albedo = float4(albedo.rgb, 1.0);
}

// TODO: AO.

// TODO: if I decide to continue RT route, some form of temporal anti-aliasing
// is basically inevitable, and probably upscaling too.
// Need to research if blurriness and ghosting can be avoided when using TAA.

struct FullscreenVertexOutput
{
    float4 position : SV_Position;
    float2 uv;
};

[shader("vertex")]
void FullscreenVertexMain(uint vertexId : SV_VertexID, out FullscreenVertexOutput output)
{
    // Fullscreen triangle.
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

[shader("fragment")]
float4 FullscreenFragmentMain(
    FullscreenVertexOutput input,
    uniform PushConstants pushConstants,
) : SV_Target
{
    const Ptr<UniformData, Access.Read> uniformData = pushConstants.uniformData;

    const float shadow =
        combinedImageSamplers[COMBINED_IMAGE_SAMPLER_SHADOW_IDX].Sample(input.uv).r;

    const float3 ambient =
        combinedImageSamplers[COMBINED_IMAGE_SAMPLER_ALBEDO_IDX].Sample(input.uv).rgb * 0.02;

    float3 color = combinedImageSamplers[COMBINED_IMAGE_SAMPLER_RENDER_IDX].Sample(input.uv).rgb;

    color = color * shadow + ambient;

    return float4(TonemapHejlBurgessDawson(color, uniformData.exposure), 1.0);
}
