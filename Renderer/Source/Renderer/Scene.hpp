#pragma once

#include "../Math/Types.hpp"
#include "RendererCommon.hpp"
#include "Shaders/SharedDef.hlsli"

#include <vector>
#include <string>

bool LoadScene(
    std::vector<Vertex>& vertices,
    std::vector<u32>& indices,
    std::vector<VkDrawIndexedIndirectCommand>& drawCmds,
    std::vector<DrawData>& drawData,
    std::vector<Material>& materials,
    std::vector<std::string>& texturePaths,
    Vec3& sunDirectionWorld,
    const std::string& gltfPath
);
