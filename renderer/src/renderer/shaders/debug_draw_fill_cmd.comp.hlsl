#include "common.hlsli"

ByteAddressBuffer count_buffer;
RWStructuredBuffer<DrawIndirectCommand> cmd_buffer;

[numthreads(1, 1, 1)]
void main() {
    const uint count = count_buffer.Load(0);

    DrawIndirectCommand cmd;
    cmd.vertex_count = 6 * count;
    cmd.instance_count = 1;
    cmd.first_vertex = 0;
    cmd.first_instance = 0;

    cmd_buffer[0] = cmd;
}
