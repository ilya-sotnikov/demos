#include "Common.slang"
#include "Math.slang"

// TODO: is it possible to overlap this as async compute with the main pass?
// TODO: 1rpp is very expensive, will figure something out after TAA.
[shader("compute")]
[numthreads(RENDERER_SHADOW_WORKGROUP_SIZE_X, RENDERER_SHADOW_WORKGROUP_SIZE_Y, 1)]
void ShadowMain(
    uint3 dtid : SV_DispatchThreadID,
    uniform PushConstants pushConstants,
)
{
    float2 shadowImageSize;
    shadowStorageImage.GetDimensions(shadowImageSize.x, shadowImageSize.y);

    if (dtid.x >= shadowImageSize.x || dtid.y >= shadowImageSize.y)
    {
        return;
    }

    const Ptr<UniformData, Access.Read> uniformData = pushConstants.uniformData;

    const float depth = depthSampledImage.Load(int3(dtid.xy, 0)).r;

    const float2 uv = (float2(dtid.xy) + 0.5) / shadowImageSize;

    const float4 posClip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    const float4 posWorldHomo = mul(uniformData.clipToWorld, posClip);
    const float3 posWorld = posWorldHomo.xyz / posWorldHomo.w;

    // TODO: only trace when color > 0.0?
    float shadow = 1.0;

    RayDesc rayDesc;
    rayDesc.Origin = posWorld;
    // TODO: jitter and blur in screen space for soft shadows.
    rayDesc.Direction = -uniformData.sunDirectionWorld;
    rayDesc.TMin = 0.01; // TODO: offset by geometric normal instead?
    rayDesc.TMax = 100.0;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE> rayQuery;
    rayQuery.TraceRayInline(
        RaytracingAccelerationStructure(pushConstants.tlas),
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
        0xff,
        rayDesc
    );

    rayQuery.Proceed();

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        shadow = 0.0;
    }

    shadowStorageImage.Store(dtid.xy, float4(shadow, 0.0, 0.0, 0.0));
}

[shader("compute")]
[numthreads(RENDERER_SHADOW_WORKGROUP_SIZE_X, RENDERER_SHADOW_WORKGROUP_SIZE_Y, 1)]
void ShadowMergeMain(
    uint3 dtid : SV_DispatchThreadID,
    uniform PushConstants pushConstants,
)
{
    const Ptr<UniformData, Access.Read> uniformData = pushConstants.uniformData;

    float2 shadowImageSize;
    shadowSampledImage.GetDimensions(shadowImageSize.x, shadowImageSize.y);

    if (dtid.x >= shadowImageSize.x || dtid.y >= shadowImageSize.y)
    {
        return;
    }

    const float shadow = shadowSampledImage[dtid.xy].r;

    const float3 ambient = albedoSampledImage[dtid.xy].rgb * uniformData.ambientIntensity;

    float3 color = renderStorageImage[dtid.xy].rgb;

    color = color * shadow + ambient;

    renderStorageImage[dtid.xy] = float4(color, 1.0);
}
