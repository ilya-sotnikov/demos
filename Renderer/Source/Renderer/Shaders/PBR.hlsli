#pragma once

// https://blog.selfshadow.com/publications/s2013-shading-course/hoffman/s2013_pbs_physics_math_notes.pdf
// https://github.com/KhronosGroup/Vulkan-Tutorial/blob/main/en/Building_a_Simple_Engine/Lighting_Materials/04_lighting_implementation.adoc
// https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf

float NormalDistributionGGX(float dotNormalHalf, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float dotNormalHalf2 = dotNormalHalf * dotNormalHalf;

    const float nom = a2;
    float denom = (dotNormalHalf2 * (a2 - 1.0) + 1.0);
    denom = M_PIf * denom * denom;

    return nom / denom;
}

float GeometrySmith(float dotNormalView, float dotNormalLight, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;

    const float ggx1 = dotNormalView / (dotNormalView * (1.0 - k) + k);
    const float ggx2 = dotNormalLight / (dotNormalLight * (1.0 - k) + k);

    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 fresnel0)
{
    return fresnel0 + (1.0 - fresnel0) * pow(1.0 - cosTheta, 5.0);
}
