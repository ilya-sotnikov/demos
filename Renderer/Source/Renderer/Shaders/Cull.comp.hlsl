#include "Common.hlsli"
#include "Math.hlsli"

[[vk::binding(0)]]
ConstantBuffer<UniformData> uniformBuffer;
[[vk::binding(1)]]
StructuredBuffer<DrawData> drawDataBuffer;
[[vk::binding(2)]]
RWByteAddressBuffer indirectCountBuffer;
[[vk::binding(3)]]
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdsBuffer1;
[[vk::binding(4)]]
RWStructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdsBuffer2;
[[vk::binding(5)]]
RWStructuredBuffer<uint32_t> drawIndicesBuffer;

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
    if (drawData.renderPassFlags != RENDER_PASS_OPAQUE_BIT)
    {
        return;
    }

    const float3 sphereCenterWorld =
        QuatRotate(drawData.orientation, drawData.sphereCenter) * drawData.scale + drawData.position;
    const float3 sphereCenterView = mul(
        uniformBuffer.cullWorldToView,
        float4(sphereCenterWorld, 1.0)
    ).xyz;
    const float sphereRadius = drawData.scale * drawData.sphereRadius;

    // Stole from niagara:
    // https://github.com/zeux/niagara
    bool visible = true;
    // View frustum is symmetrical, therefore we can cull against two opposite planes simultaneously.
    visible = visible && sphereCenterView.z * uniformBuffer.cullFrustumPlaneXZ -
        abs(sphereCenterView.x) * uniformBuffer.cullFrustumPlaneXX > -sphereRadius;

    visible = visible && sphereCenterView.z * uniformBuffer.cullFrustumPlaneYZ -
        abs(sphereCenterView.y) * uniformBuffer.cullFrustumPlaneYY > -sphereRadius;

    visible = visible && sphereCenterView.z - sphereRadius < -RENDERER_NEAR_PLANE;

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

