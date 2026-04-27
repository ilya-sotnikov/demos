#include "Common.hlsli"
#include "Math.hlsli"
#include "PBR.hlsli"
#include "ForwardRender.hlsli"

// TODO: try unjittering uv?
float4 SampleTex(uint32_t idx, float2 uv)
{
    return textures[NonUniformResourceIndex(idx)].Sample(textureSampler, uv);
}

struct FragmentOutput
{
    float3 color : SV_Target0;
    float2 velocity : SV_Target1;
};

void Main(VertexOutput input, out FragmentOutput output)
{
    const DrawData drawData = drawDataBuffer[input.drawIdx];
    const Material material = materialBuffer[drawData.materialIdx];

    const float3 positionWorld = input.positionWorld;
    const float2 uv = input.uv;

    float3 normal = normalize(input.normalWorld);

    float4 albedo = material.albedoFactor;
    if (material.albedoTexIdx > 0)
    {
        albedo *= SampleTex(material.albedoTexIdx, uv);
    }

    float2 metallicRoughness = float2(material.metallicFactor, material.roughnessFactor);
    if (material.albedoTexIdx > 0)
    {
        metallicRoughness *= SampleTex(material.metallicRoughnessTexIdx, uv).gr;
    }
    const float metallic = metallicRoughness.r;
    const float roughness = metallicRoughness.g;

    if (material.normalTexIdx > 0)
    {
        const float3 normalSample = SampleTex(material.normalTexIdx, uv).xyz * 2.0 - 1.0;

        const float3 tangent = normalize(input.tangent.xyz);
        // w is handedness.
        const float3 bitangent = normalize(cross(normal, tangent)) * input.tangent.w;

        normal = normalize(
            normalSample.r * tangent +
            normalSample.g * bitangent +
            normalSample.b * normal
        );
    }

    const float3 fragmentToCamera = normalize(uniformBuffer.cameraPosition - input.positionWorld);

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

    // TODO
    float shadow = 1.0;

    const float3 ambient = albedo.rgb * uniformBuffer.ambientIntensity;

    // TODO: separate BRDF function to reduce code duplication.
    const float3 color = radianceOut * shadow + ambient;

    // TODO: support movable objects (double-buffered draw data).
    const float3 prevPosWorld = positionWorld;
    const float4 prevPosClip = mul(uniformBuffer.prevWorldToClip, float4(prevPosWorld, 1.0));
    const float2 prevPosNdc = prevPosClip.xy / prevPosClip.w;

    float2 posNdc =
        input.positionClip.xy / float2(uniformBuffer.renderWidth, uniformBuffer.renderHeight);
    posNdc = posNdc * 2.0 - 1.0;
    posNdc.y *= -1.0;

    float2 velocity =
        (posNdc - uniformBuffer.taaJitter) - (prevPosNdc - uniformBuffer.prevTaaJitter);
    velocity.y *= -1.0;

    output.color = color;
    output.velocity = velocity;
}
