#include "common.hlsli"
#include "visibility_buffer.hlsli"

struct FragmentOutput
{
    uint2 visibilityData : SV_Target0;
};

void main(
    VertexOutput input,
    uint primitiveId : SV_PrimitiveID,
    out FragmentOutput output
) : SV_Target
{
    output.visibilityData = uint2(primitiveId, input.rawDrawIdx);
}
