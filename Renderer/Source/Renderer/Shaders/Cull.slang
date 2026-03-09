#include "Common.slang"

[shader("compute")]
[numthreads(RENDERER_CULL_WORKGROUP_SIZE, 1, 1)]
void Main(
    uint3 dtid : SV_DispatchThreadID,
    uniform PushConstants pushConstants,
)
{
    const Ptr<UniformData, Access.Read> uniformData = pushConstants.uniformData;

    const uint drawIdx = dtid.x;

    if (drawIdx >= uniformData.drawCount)
    {
        return;
    }

    const DrawData drawData = pushConstants.drawData[drawIdx];

    // TODO: support translucent materials.
    if (drawData.renderPassFlags != RenderPassFlagBits::RENDER_PASS_OPAQUE_BIT)
    {
        return;
    }

    const float3 sphereCenterView = mul(
        uniformData.worldToView,
        mul(drawData.localToWorld, float4(drawData.sphereCenter, 1.0))
    ).xyz;
    const float sphereRadius = drawData.scale * drawData.sphereRadius;

    // Stole from niagara:
    // https://github.com/zeux/niagara
    bool visible = true;
    // View frustum is symmetrical, therefore we can cull against two opposite planes simultaneously.
    visible = visible && sphereCenterView.z * uniformData.frustumPlaneXZ -
        abs(sphereCenterView.x) * uniformData.frustumPlaneXX > -sphereRadius;

    visible = visible && sphereCenterView.z * uniformData.frustumPlaneYZ -
        abs(sphereCenterView.y) * uniformData.frustumPlaneYY > -sphereRadius;

    visible = visible && sphereCenterView.z - sphereRadius < -RENDERER_NEAR_PLANE;

    // TODO: occlusion culling.

    if (visible)
    {
        uint32_t visibleCount;
        // TODO: subgroup operations.
        InterlockedAdd(*pushConstants.indirectCountPtr, 1, visibleCount);

        const VkDrawIndexedIndirectCommand drawCmd = pushConstants.drawCmds1[drawIdx];

        DrawCmd writeDrawCmd;
        writeDrawCmd.idx = drawIdx;
        writeDrawCmd.cmd.indexCount = drawCmd.indexCount;
        writeDrawCmd.cmd.instanceCount = 1;
        writeDrawCmd.cmd.firstIndex = drawCmd.firstIndex;
        writeDrawCmd.cmd.vertexOffset = drawCmd.vertexOffset;
        writeDrawCmd.cmd.firstInstance = 0;

        pushConstants.drawCmds2[visibleCount] = writeDrawCmd;
    }
}

