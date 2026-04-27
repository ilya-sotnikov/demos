#include "Common.hlsli"
#include "Math.hlsli"

ConstantBuffer<UniformData> uniformBuffer;
StructuredBuffer<DrawData> drawDataBuffer;
RWByteAddressBuffer drawCountBuffer;
StructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdsBuffer1;
RWStructuredBuffer<VkDrawIndexedIndirectCommand> drawCmdsBuffer2;
RWStructuredBuffer<uint32_t> drawIndicesBuffer;

[[vk::push_constant]]
PushConstantsShadow pushConstants;

[numthreads(RENDERER_CULL_WORKGROUP_SIZE, 1, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    const uint drawIdx = dtid.x;

    if (drawIdx >= uniformBuffer.drawCount)
    {
        return;
    }

    const DrawData drawData = drawDataBuffer[drawIdx];

    bool visible = drawData.renderPassFlags == pushConstants.renderPassFlags;

    // TODO: ortho frustum cull.

    const uint subgroupVisibleIdx = WavePrefixCountBits(visible);
    const uint subgroupVisibleCount = WaveActiveCountBits(visible);
    uint baseVisibleIdx;
    if (WaveIsFirstLane())
    {
        drawCountBuffer.InterlockedAdd(0, subgroupVisibleCount, baseVisibleIdx);
    }
    baseVisibleIdx = WaveReadLaneFirst(baseVisibleIdx);

    if (visible)
    {
        const uint visibleIdx = baseVisibleIdx + subgroupVisibleIdx;
        const VkDrawIndexedIndirectCommand drawCmd = drawCmdsBuffer1[drawIdx];

        VkDrawIndexedIndirectCommand writeDrawCmd;
        writeDrawCmd.indexCount = drawCmd.indexCount;
        writeDrawCmd.instanceCount = 1;
        writeDrawCmd.firstIndex = drawCmd.firstIndex;
        writeDrawCmd.vertexOffset = drawCmd.vertexOffset;
        writeDrawCmd.firstInstance = 0;

        drawCmdsBuffer2[visibleIdx] = writeDrawCmd;
        drawIndicesBuffer[visibleIdx] = drawIdx;
    }
}

