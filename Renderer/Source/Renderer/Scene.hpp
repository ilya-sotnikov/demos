#pragma once

#include "../Math/Types.hpp"
#include "RendererCommon.hpp"
#include "Shaders/SharedDef.hlsli"

#include <vector>
#include <string>

bool LoadScene(
    std::vector<Vertex>& vertices,
    std::vector<DrawData>& drawData,
    std::vector<Material>& materials,
    std::vector<u32>& meshletVertices,
    std::vector<u8>& meshletTriangles,
    std::vector<Meshlet>& meshlets,
    std::vector<MeshTaskCommand>& meshTaskCommands,
    std::vector<std::string>& texturePaths,
    Vec3& sunDirectionWorld,
    const std::string& gltfPath
);
