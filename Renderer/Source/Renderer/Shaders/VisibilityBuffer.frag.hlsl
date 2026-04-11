#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"

struct FragmentOutput
{
    uint2 visibilityData : SV_Target0;
};

void Main(
    VertexOutput input,
    uint primitiveId : SV_PrimitiveID,
    out FragmentOutput output
) : SV_Target
{
    output.visibilityData = uint2(primitiveId, input.rawDrawIdx);
}
