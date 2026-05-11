#pragma once

#include "../Math/Types.hpp"
#include "RendererCommon.hpp"
#include "Shaders/SharedDef.hlsli"

#include <vector>
#include <string>

struct Mesh
{
    size_t primitiveIdx;
    size_t primitiveCount;
};

struct cgltf_material;

struct MeshPrimitive
{
    Vec3 sphereCenter;
    f32 sphereRadius;

    size_t vertexCount;
    size_t meshIdx;

    cgltf_material* material;
};

bool LoadScene(
    std::vector<Vertex>& vertices,
    std::vector<u32>& indices,
    std::vector<MeshPrimitive>& meshPrimitives,
    std::vector<VkDrawIndexedIndirectCommand>& drawCmds,
    std::vector<DrawData>& drawData,
    std::vector<Material>& materials,
    std::vector<std::string>& texturePaths,
    Vec3& sunDirectionWorld,
    const std::string& gltfPath
);
