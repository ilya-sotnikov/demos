#include "Common.slang"
#include "Math.slang"

ConstantBuffer<UniformData> uniformBuffer;
StructuredBuffer<DrawData> drawDataBuffer;
RWByteAddressBuffer indirectCountBuffer;
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdsBuffer1;
RWStructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdsBuffer2;
RWStructuredBuffer<uint32_t> drawIndicesBuffer;

[shader("compute")]
[numthreads(RENDERER_CULL_WORKGROUP_SIZE, 1, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    const uint drawIdx = dtid.x;

    if (drawIdx >= uniformBuffer.drawCount)
    {
        return;
    }

    const DrawData drawData = drawDataBuffer[drawIdx];

    // TODO: support translucent materials.
    if (drawData.renderPassFlags != RenderPassFlagBits::RENDER_PASS_OPAQUE_BIT)
    {
        return;
    }

    const float3 sphereCenterWorld =
        QuatRotate(drawData.orientation, drawData.sphereCenter) * drawData.scale + drawData.position;
    const float3 sphereCenterView = mul(
        uniformBuffer.worldToView,
        float4(sphereCenterWorld, 1.0)
    ).xyz;
    const float sphereRadius = drawData.scale * drawData.sphereRadius;

    // Stole from niagara:
    // https://github.com/zeux/niagara
    bool visible = true;
    // View frustum is symmetrical, therefore we can cull against two opposite planes simultaneously.
    visible = visible && sphereCenterView.z * uniformBuffer.frustumPlaneXZ -
        abs(sphereCenterView.x) * uniformBuffer.frustumPlaneXX > -sphereRadius;

    visible = visible && sphereCenterView.z * uniformBuffer.frustumPlaneYZ -
        abs(sphereCenterView.y) * uniformBuffer.frustumPlaneYY > -sphereRadius;

    visible = visible && sphereCenterView.z - sphereRadius < -RENDERER_NEAR_PLANE;

    // TODO: visualize frustum culling.

    if (visible)
    {
        uint32_t visibleCount;
        // TODO: subgroup operations.
        // Although on Nvidia doesn't matter, driver optimizes it.
        // Not sure if every vendor does this.
        indirectCountBuffer.InterlockedAdd(0, 1, visibleCount);

        const VkDrawIndexedIndirectCommand drawCmd = drawCmdsBuffer1[drawIdx];

        VkDrawIndexedIndirectCommand writeDrawCmd;
        writeDrawCmd.indexCount = drawCmd.indexCount;
        writeDrawCmd.instanceCount = 1;
        writeDrawCmd.firstIndex = drawCmd.firstIndex;
        writeDrawCmd.vertexOffset = drawCmd.vertexOffset;
        writeDrawCmd.firstInstance = 0;

        drawCmdsBuffer2[visibleCount] = writeDrawCmd;
        drawIndicesBuffer[visibleCount] = drawIdx;
    }
}

