#include "common.hlsli"
#include "visibility_buffer.hlsli"

struct FragmentOutput {
    uint2 visibility_data : SV_Target0;
};

void main(
    VertexOutput input,
    uint primitive_id : SV_PrimitiveID,
    out FragmentOutput output
) : SV_Target {
    output.visibility_data = uint2(primitive_id, input.raw_draw_idx);
}
