#include "Common.slang"

[shader("compute")]
[numthreads(RENDERER_SHADOW_WORKGROUP_SIZE_X, RENDERER_SHADOW_WORKGROUP_SIZE_Y, 1)]
void Main(
    uint3 dtid : SV_DispatchThreadID,
    uniform PushConstants pushConstants,
)
{
    const Ptr<UniformData, Access.Read> uniformData = pushConstants.uniformData;

    const uint2 posScreen = dtid.xy;

    const float depth =
        combinedImageSamplers[COMBINED_IMAGE_SAMPLER_DEPTH_IDX].Load(int3(dtid.xy, 0)).r;

    const RWTexture2D shadowImageRW = storageImagesRW[STORAGE_IMAGE_RW_SHADOW_IDX];

    float2 shadowImageSize;
    shadowImageRW.GetDimensions(shadowImageSize.x, shadowImageSize.y);
    const float2 uv = (float2(dtid.xy) + 0.5) / shadowImageSize;

    const float4 posClip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    const float4 posWorldHomo = mul(uniformData.clipToWorld, posClip);
    const float3 posWorld = posWorldHomo.xyz / posWorldHomo.w;

    // TODO: only trace when color > 0.0?
    float shadow = 1.0;

    RayDesc rayDesc;
    rayDesc.Origin = posWorld; // TODO: jitter position and blur in screen space for soft shadows.
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

    shadowImageRW.Store(dtid.xy, float4(shadow, 0.0, 0.0, 0.0));
}
