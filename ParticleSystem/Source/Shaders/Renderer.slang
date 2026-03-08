#include "Common.slang"
#include "Math.slang"

struct ParticleVertexOutput
{
    float4 positionClip : SV_Position;
    nointerpolation uint color;
};

// NOTE: on my GPU (Nvidia Blackwell) rendering particles as points is 19% faster
// than generating a billboard in a vertex/mesh shader.
[shader("vertex")]
void ParticleVertexMain(
    uint vertexId : SV_VertexID,
    out float pointSize : SV_PointSize,
    out ParticleVertexOutput output
)
{
    const uint particleIdx = aliveBuffer1[vertexId];

    const float4 positionDistToForceFieldInv = positionDistToForceFieldInvBuffer[particleIdx];
    const float radius = radiusBuffer[particleIdx];

    const Emitter emitter = emitterBuffer[emitterIdxBuffer[particleIdx]];

    output.positionClip =
        mul(uniformBuffer.worldToClip, float4(positionDistToForceFieldInv.xyz, 1.0));

    // TODO: check min/max supported size and clamp.
    pointSize = uniformBuffer.swapchainHeight * uniformBuffer.viewToClip[1][1]
                * radius / output.positionClip.w;

    const float timeLeft = timeLeftBuffer[particleIdx];
    const float lifeInv = emitter.lifeInv;
    const float distToForceFieldInv = positionDistToForceFieldInv.w;
    float3 color = emitter.color;

    const float fadeOut = timeLeft * lifeInv;
    const float fadeIn = min(1.0, (1.0 - fadeOut) * 5.0);

    color.x += distToForceFieldInv;

    // Reinhard tone mapping.
    const float3 toneMappedColor = color / (color + float3(1.0));

    output.color = PackFloat4ToRGBA8Unorm(float4(toneMappedColor, fadeIn * fadeOut));
}

[shader("fragment")]
float4 ParticleFragmentMain(
    ParticleVertexOutput input,
    float2 pointCoord : SV_PointCoord
) : SV_Target
{
    float4 color = UnpackRGBA8UnormToFloat4(input.color);

    // [0.0, sqrt(0.5^2 + 0.5^2) == 0.707]
    const float distToCenter = distance(pointCoord, 0.5);
    // 0.707 is at vertices of a quad, 0.5 in the middle of edges.
    // To make a circle, we'll map dist == 0.0 to 1.0 and dist == 0.5 to 0.0,
    // if intensity = k * dist + b, then k = -2.0, b = 1.0.
    const float intensity = -2.0 * distToCenter + 1.0;

    color.a *= intensity;

    if (color.a < 0.1)
    {
        discard;
    }

    return color;
}

struct FullscreenVertexOutput
{
    float2 uv;
    float4 position : SV_Position;
};

[shader("vertex")]
void FullscreenVertexMain(uint vertexId : SV_VertexID, out FullscreenVertexOutput output)
{
    // Fullscreen triangle.
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

[shader("fragment")]
float4 FullscreenFragmentMain(FullscreenVertexOutput input) : SV_Target
{
    return renderImageSampler.Sample(input.uv);
}
