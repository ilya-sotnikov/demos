#include "grid.hlsli"

ConstantBuffer<UniformData> uniformBuffer : register(b0, space1);

void main(uint vertexId : SV_VertexID, out VertexOutput output)
{
    const float2 positionsXZ[] = {
        float2(-1.0, -1.0) * GRID_SIZE,
        float2( 1.0, -1.0) * GRID_SIZE,
        float2( 1.0,  1.0) * GRID_SIZE,
        float2(-1.0,  1.0) * GRID_SIZE,
    };
    const int indices[] = {0, 2, 1, 2, 0, 3};

    const int idx = indices[vertexId];
    const float2 pos = positionsXZ[idx];
    const float3 cameraPosWorld = uniformBuffer.cameraPositionWorld;
    const float2 gridPosWorld = float2(pos.x + cameraPosWorld.x, pos.y + cameraPosWorld.z);

    output.positionWorldXZ = gridPosWorld;
    output.positionClip = mul(
        uniformBuffer.worldToClip,
        float4(gridPosWorld.x, 0.0, gridPosWorld.y, 1.0));
}
