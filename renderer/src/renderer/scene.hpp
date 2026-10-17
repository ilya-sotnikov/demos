#pragma once

#include "common.hpp"
#include "math/types.hpp"
#include "renderer/rhi/rhi.hpp"
#include "renderer/shaders/shared_def.hlsli"

#include <string>
#include <vector>

struct Mesh {
    size_t primitive_idx;
    size_t primitive_count;
};

struct cgltf_material;

struct MeshPrimitive {
    Vec3 sphere_center;
    f32 sphere_radius;

    size_t vertex_count;
    size_t mesh_idx;

    cgltf_material* material;
};

bool load_scene(
    std::vector<Vertex>& vertices,
    std::vector<u32>& indices,
    std::vector<MeshPrimitive>& mesh_primitives,
    std::vector<rhi::DrawIndexedIndirectCommand>& draw_cmds,
    std::vector<DrawData>& draw_data,
    std::vector<Material>& materials,
    std::vector<std::string>& texture_paths,
    Vec3& sun_direction_world,
    const std::string& gltf_path
);
