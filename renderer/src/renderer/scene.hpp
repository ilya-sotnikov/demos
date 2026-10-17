#pragma once

#include "common.hpp"
#include "math/types.hpp"
#include "renderer/rhi/rhi.hpp"
#include "renderer/shaders/shared_def.hlsli"

#include <EASTL/vector.h>
#include <EASTL/string.h>

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
    eastl::vector<Vertex>& vertices,
    eastl::vector<u32>& indices,
    eastl::vector<MeshPrimitive>& mesh_primitives,
    eastl::vector<rhi::DrawIndexedIndirectCommand>& draw_cmds,
    eastl::vector<DrawData>& draw_data,
    eastl::vector<Material>& materials,
    eastl::vector<eastl::string>& texture_paths,
    Vec3& sun_direction_world,
    const eastl::string& gltf_path
);
