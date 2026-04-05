#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"

uint2 Main(
    VertexOutput input,
    uint primitiveId : SV_PrimitiveID
) : SV_Target
{
    return uint2(primitiveId, input.rawDrawIdx);
}
