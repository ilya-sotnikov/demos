#include "Common.hlsli"

ByteAddressBuffer countBuffer;
RWStructuredBuffer<DrawIndirectCommand> cmdBuffer;

[numthreads(1, 1, 1)]
void Main()
{
    const uint count = countBuffer.Load(0);

    DrawIndirectCommand cmd;
    cmd.vertexCount = 6 * count;
    cmd.instanceCount = 1;
    cmd.firstVertex = 0;
    cmd.firstInstance = 0;

    cmdBuffer[0] = cmd;
}
