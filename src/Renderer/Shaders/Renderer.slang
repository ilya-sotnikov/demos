#include "Common.slang"
#include "SceneCommon.slang"

[[vk::binding(4)]]
Sampler2DArrayShadow shadowMapSampler;

[[vk::binding(5)]]
Sampler3D shadowJitterSampler;

struct VertexOutput
{
    float4 positionScreen : SV_Position;
    float3 normalWorld;
    float3 normalView;
    float3 positionWorld;
    float3 positionView;
    float3 positionsShadow[RENDERER_SHADOW_MAP_CASCADE_COUNT];
    nointerpolation uint drawId;
};

[shader("vertex")]
VertexOutput vertexMain(
    uint vertexId : SV_VertexID,
    uint vertexOffset : SV_StartVertexLocation,
    uint drawId : SV_DrawIndex,
)
{
    const Vertex vertex = vertices[vertexOffset + vertexId];
    const DrawData data = drawData[drawId];

    VertexOutput output;

    const float4 positionWorld = mul(data.localToWorld, float4(vertex.position, 1.0));

    output.positionWorld = positionWorld.xyz;
    output.positionView = mul(uniformData.worldToView, positionWorld).xyz;
    if (uniformData.perspectiveChosen == 0)
    {
        output.positionScreen = mul(uniformData.worldToClip, positionWorld);
    }
    else
    {
        output.positionScreen = mul(
            uniformData.shadow.worldToClip[uniformData.perspectiveChosen - 1],
            positionWorld
        );
        // To avoid double flipping, since we flip Y both in the cascade projection matrix
        // (to correctly fill shadow map in shadow pass) and in the Vulkan viewport.
        output.positionScreen.y *= -1.0;
    }

    output.normalWorld = mul(data.localToWorldNormal, vertex.normal);
    output.normalView = mul(uniformData.worldToView, float4(output.normalWorld, 0.0)).xyz;

    output.drawId = drawId;

    const float SHADOW_NORMAL_OFFSET = 3.0;
    const float SHADOW_CONSTANT_OFFSET = 0.00002;
    const float normalOffsetScale =
        saturate(1.0 - dot(-uniformData.lightDirectionView, normalize(output.normalView)))
        * SHADOW_NORMAL_OFFSET;
    const float3 scaledNormalWorld = normalize(output.normalWorld.xyz) * normalOffsetScale;

    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        const float4 shadowOffset = float4(
            scaledNormalWorld * uniformData.shadow.texelSizes[i],
            0.0
        );
        float4 shadowCoord = mul(uniformData.shadow.worldToClip[i], positionWorld + shadowOffset);
        shadowCoord.z += SHADOW_CONSTANT_OFFSET / uniformData.shadow.texelSizes[i];
        shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

        output.positionsShadow[i] = shadowCoord.xyz;
    }

    return output;
}

static float SampleShadow(float3 positionShadow, uint cascadeIndex)
{
    return shadowMapSampler.SampleCmp(float3(positionShadow.xy, cascadeIndex), positionShadow.z).r;
}

// GPU gems 2, Chapter 17, Efficient Soft-Edged Shadows Using Pixel Shader Branching, Yury Uralsky
// https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-17-efficient-soft-edged-shadows-using
static float CalculateShadows(
    float3 positionShadow,
    float4 positionScreen,
    float3 normalView,
    float diffuse,
    int cascadeIndex
)
{
    float shadow = 0.0;
    if (uniformData.enableShadowPcf == 1)
    {
        // By reducing the PCF kernel size for distant cascades we can get
        // achieve seamless shadows without blending.
        // TODO: try to find a function that calculates these coefficients,
        // it should at least depend on shadow map texel sizes.
        const int PCF_SAMPLES_COUNT =
            RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_U *
            RENDERER_SHADOW_MAP_JITTER_OFFSETS_SAMPLES_V;
        const float PCF_KERNEL_CASCADE_SCALES[] = {1.0, 0.45, 0.20, 0.10};
        const float PCF_KERNEL_SCALE = 3.0; // Blurriness.
        const float OFFSET_SCALE =
            PCF_KERNEL_SCALE * PCF_KERNEL_CASCADE_SCALES[cascadeIndex]
            / RENDERER_SHADOW_MAP_DIMENSIONS;

        float3 jitterCoord =
            float3(positionScreen.xy / RENDERER_SHADOW_MAP_JITTER_OFFSETS_SIZE, 0.0);
        float3 shadowCoord = positionShadow;

        for (int i = 0; i < 4; ++i)
        {
            const float4 offset = shadowJitterSampler.Sample(jitterCoord);
            jitterCoord.z += 1.0f / (PCF_SAMPLES_COUNT / 2.0);

            shadowCoord.xy = offset.xy * OFFSET_SCALE + positionShadow.xy;
            shadow += SampleShadow(shadowCoord, cascadeIndex) / 8.0;

            shadowCoord.xy = offset.zw * OFFSET_SCALE + positionShadow.xy;
            shadow += SampleShadow(shadowCoord, cascadeIndex) / 8.0;
        }

        // Skip expensive shadow map filtering if either diffuse component is zero
        // or all test samples are 0.0 or 1.0 (completely in umbra or unshadowed).
        if ((diffuse * (shadow - 1.0) * shadow) != 0.0)
        {
            // Probably in the penumbra.

            shadow /= 8.0;
            for (int i = 0; i < PCF_SAMPLES_COUNT / 2 - 4; ++i)
            {
                const float4 offset = shadowJitterSampler.Sample(jitterCoord);
                jitterCoord.z += 1.0f / (PCF_SAMPLES_COUNT / 2.0);

                shadowCoord.xy = offset.xy * OFFSET_SCALE + positionShadow.xy;
                shadow += SampleShadow(shadowCoord, cascadeIndex) / PCF_SAMPLES_COUNT;

                shadowCoord.xy = offset.zw * OFFSET_SCALE + positionShadow.xy;
                shadow += SampleShadow(shadowCoord, cascadeIndex) / PCF_SAMPLES_COUNT;
            }
        }
    }
    else
    {
        shadow = SampleShadow(positionShadow, cascadeIndex);
    }

    return shadow;
}

[shader("fragment")]
float4 fragmentMain(
    VertexOutput input,
) : SV_Target
{
    const float AMBIENT_STRENGTH = 0.3;
    const float DIFFUSE_STRENGTH = 1.0;

    const DrawData data = drawData[input.drawId];

    // return float4(data.color, 1.0);

    const float3 normalView = normalize(input.normalView);

    const float ambient = AMBIENT_STRENGTH;

    const float diffuse =
        max(dot(-uniformData.lightDirectionView, normalView), 0.0) * DIFFUSE_STRENGTH;

    // We are choosing cascade not by split distances, but by determining
    // the smallest cascade that contains this fragment.
    int cascadeIndex = -1;
    for (int i = 0; i < RENDERER_SHADOW_MAP_CASCADE_COUNT; ++i)
    {
        // Leaving a small room for PCF kernel, to avoid sampling outside the cascade.
        const float MAX_VALUE = 0.99;

        const float3 coord = input.positionsShadow[i];
        const float minCoord = min(coord.x, min(coord.y, coord.z));
        const float maxCoord = max(coord.x, max(coord.y, coord.z));

        if ((minCoord >= 0.0) && (maxCoord <= MAX_VALUE))
        {
            cascadeIndex = i;
            break;
        }
    }

    const float shadow = cascadeIndex == -1 ? 1.0 :
        CalculateShadows(
            input.positionsShadow[cascadeIndex],
            input.positionScreen,
            normalView,
            diffuse.r,
            cascadeIndex
        );

    float3 color =
        (ambient + diffuse * shadow) * uniformData.lightColor * data.color;

    if (uniformData.enableShadowCascadesColor == 1)
    {
        const float cascadeColorAmbient = 0.90;
        switch (cascadeIndex)
        {
        case 0:
            color *= float3(1.0, cascadeColorAmbient, cascadeColorAmbient);
            break;
        case 1:
            color *= float3(cascadeColorAmbient, 1.0, cascadeColorAmbient);
            break;
        case 2:
            color *= float3(cascadeColorAmbient, cascadeColorAmbient, 1.0);
            break;
        case 3:
            color *= float3(1.0, 1.0, cascadeColorAmbient);
            break;
        }
    }

    if (uniformData.enableShadowTexelColoring == 1 && cascadeIndex != -1)
    {
        const float sumOfTexelIndices =
            floor(input.positionsShadow[cascadeIndex].x * RENDERER_SHADOW_MAP_DIMENSIONS) +
            floor(input.positionsShadow[cascadeIndex].y * RENDERER_SHADOW_MAP_DIMENSIONS);
        color *= fmod(sumOfTexelIndices, 2.0);
    }

    // Remap color to [0, 1].
    color = color / (color + float3(1.0));

    // To avoid double gamma-correcting (since we are rendering to a SRGB target).
    // NOTE: pow(color, 2.2) is an approximation.
    color = pow(color, 2.2);

    return float4(color, 1.0);
}
