#include "Common.hlsli"
#include "Math.hlsli"
#include "DebugDraw.hlsli"

// Heavily based on from niagara:
// https://github.com/zeux/niagara

ConstantBuffer<UniformData> uniformBuffer;
StructuredBuffer<DrawData> drawDataBuffer;
RWByteAddressBuffer drawCountBuffer;
StructuredBuffer<DrawIndexedIndirectCommand> drawCmdsBuffer1;
RWStructuredBuffer<DrawIndexedIndirectCommand> drawCmdsBuffer2;
RWStructuredBuffer<uint32_t> drawIndicesBuffer;
RWStructuredBuffer<uint32_t> meshPrimitiveVisibleBuffer;

SamplerState minSampler;
Texture2D<float> depthPyramidTexture;

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere, Michael Mara, Morgan McGuire
// http://jcgt.org/published/0002/02/05/paper.pdf
// Took the implementation from niagara:
// https://github.com/zeux/niagara
// The idea is to simplify the code in this paper, because it's very generic
// and we only care about getting a clip space (uv) AABB of a sphere.
bool ProjectSphere(
    float3 center,
    float radius,
    float zNear,
    float perspective00,
    float perspective11,
    out float4 aabb
)
{
    // If the bounding sphere intersects the near plane, we skip occlusion tests
    // since it simplifies math and in this case the object is probably not occluded anyway.
    if (center.z > -(radius + zNear))
    {
        return false;
    }

    const float3 cr = center * radius;
    const float czr2 = center.z * center.z - radius * radius;

    const float vx = sqrt(center.x * center.x + czr2);
    const float minx = (vx * center.x - cr.z) / (vx * center.z + cr.x);
    const float maxx = (vx * center.x + cr.z) / (vx * center.z - cr.x);

    const float vy = sqrt(center.y * center.y + czr2);
    const float miny = (vy * center.y - cr.z) / (vy * center.z + cr.y);
    const float maxy = (vy * center.y + cr.z) / (vy * center.z - cr.y);

    aabb = float4(
        minx * perspective00,
        miny * perspective11,
        maxx * perspective00,
        maxy * perspective11
    );
    aabb = aabb.zyxw * float4(-0.5, 0.5, -0.5, 0.5) + 0.5; // Clip space -> uv space.

    return true;
}

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
    bool visible = drawData.renderPassFlags == RENDER_PASS_OPAQUE_BIT;

    // In the early cull pass of 2-pass Hi-Z occlusion culling, we render only
    // mesh primitives that were visible last frame.
    if (CULL_LATE == 0)
    {
        visible = visible && meshPrimitiveVisibleBuffer[drawIdx] != 0;
    }

    const float3 sphereCenterWorld =
        QuatRotate(drawData.orientation, drawData.sphereCenter) * drawData.scale + drawData.position;
    const float3 sphereCenterView = mul(
        uniformBuffer.cullWorldToView,
        float4(sphereCenterWorld, 1.0)
    ).xyz;
    const float sphereRadius = drawData.scale * drawData.sphereRadius;

    // View frustum is symmetrical, therefore we can cull against two opposite planes simultaneously.
    visible = visible && sphereCenterView.z * uniformBuffer.cullFrustumPlaneXZ -
        abs(sphereCenterView.x) * uniformBuffer.cullFrustumPlaneXX > -sphereRadius;

    visible = visible && sphereCenterView.z * uniformBuffer.cullFrustumPlaneYZ -
        abs(sphereCenterView.y) * uniformBuffer.cullFrustumPlaneYY > -sphereRadius;

    visible = visible && sphereCenterView.z - sphereRadius < -RENDERER_NEAR_PLANE;

    if ((CULL_LATE == 1) && visible)
    {
        float4 aabb; // Screen-space (uv) AABB.
        if (ProjectSphere(
            sphereCenterView,
            sphereRadius,
            RENDERER_NEAR_PLANE,
            uniformBuffer.viewToClip[0][0],
            uniformBuffer.viewToClip[1][1],
            aabb))
        {
            const float width = (aabb.z - aabb.x) * uniformBuffer.depthPyramidWidth;
            const float height = (aabb.w - aabb.y) * uniformBuffer.depthPyramidHeight;

            const float mipLevel = ceil(log2(max(width, height)));

            // Min reduction sampler (minimum depth of 2x2 texel quad).
            const float depth =
                depthPyramidTexture.SampleLevel(minSampler, (aabb.xy + aabb.zw) * 0.5, mipLevel);
            const float depthSphere = RENDERER_NEAR_PLANE / (-sphereCenterView.z - sphereRadius);

            visible = visible && depthSphere > depth;

            if (uniformBuffer.drawCullAABB)
            {
                DebugDrawRect(
                    aabb * float4(
                        uniformBuffer.renderWidth,
                        uniformBuffer.renderHeight,
                        uniformBuffer.renderWidth,
                        uniformBuffer.renderHeight
                    ),
                    COLOR_RED
                );
            }
        }
    }

    // In the early cull pass, we are always writing draw commands (for previously
    // visible mesh primitives).
    // In the late cull pass, we are writing only draw commands for mesh primitives
    // that are visible, but were not visible in the early cull pass.
    // Therefore, if camera did not move between the 2 consecutive frames, visibleCount == 0.
    const bool shouldWriteDrawData =
        visible && ((CULL_LATE == 0) || (meshPrimitiveVisibleBuffer[drawIdx] == 0));

    const uint subgroupVisibleIdx = WavePrefixCountBits(shouldWriteDrawData);
    const uint subgroupVisibleCount = WaveActiveCountBits(shouldWriteDrawData);
    uint baseVisibleIdx;
    if (WaveIsFirstLane())
    {
        drawCountBuffer.InterlockedAdd(0, subgroupVisibleCount, baseVisibleIdx);
    }
    baseVisibleIdx = WaveReadLaneFirst(baseVisibleIdx);

    if (shouldWriteDrawData)
    {
        const uint visibleIdx = baseVisibleIdx + subgroupVisibleIdx;
        const DrawIndexedIndirectCommand drawCmd = drawCmdsBuffer1[drawIdx];

        DrawIndexedIndirectCommand writeDrawCmd;
        writeDrawCmd.indexCount = drawCmd.indexCount;
        writeDrawCmd.instanceCount = 1;
        writeDrawCmd.firstIndex = drawCmd.firstIndex;
        writeDrawCmd.vertexOffset = drawCmd.vertexOffset;
        writeDrawCmd.firstInstance = 0;

        drawCmdsBuffer2[visibleIdx] = writeDrawCmd;
        drawIndicesBuffer[visibleIdx] = drawIdx;
    }

    if (CULL_LATE == 1)
    {
        // On the next frame, this buffer will be used in the early cull pass
        // to draw mesh primitives that were visible last frame.
        // TODO: pack?
        meshPrimitiveVisibleBuffer[drawIdx] = visible ? 1 : 0;
    }
}

