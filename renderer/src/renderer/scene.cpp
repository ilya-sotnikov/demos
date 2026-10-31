#include "scene.hpp"

#include "math/mat4.hpp"
#include "math/vec3.hpp"
#include "pack_utils.hpp"
#include "utils.hpp"

#include <cgltf.h>
#include <meshoptimizer.h>

// TODO: more error handling.

static const char* cgltf_result_to_string(cgltf_result result) {
    switch (result) {
    case cgltf_result_success:
        return "success";
    case cgltf_result_data_too_short:
        return "data too short";
    case cgltf_result_unknown_format:
        return "unknown format";
    case cgltf_result_invalid_json:
        return "invalid json";
    case cgltf_result_invalid_gltf:
        return "invalid gltf";
    case cgltf_result_invalid_options:
        return "invalid options";
    case cgltf_result_file_not_found:
        return "file not found";
    case cgltf_result_io_error:
        return "io error";
    case cgltf_result_out_of_memory:
        return "out of memory";
    case cgltf_result_legacy_gltf:
        return "legacy gltf";
    case cgltf_result_max_enum:
        return "max enum (invalid enum value)";
    }

    return "unhandled enum value";
}

// Stole from niagara:
// https://github.com/zeux/niagara
static void decompose_transform(
    f32 translation[3],
    f32 rotation[4],
    f32 scale[3],
    const f32 transform[16]
) {
    DEBUG_ASSERT(translation);
    DEBUG_ASSERT(rotation);
    DEBUG_ASSERT(scale);
    DEBUG_ASSERT(transform);

    f32 m[4][4] = {};
    memcpy(m, transform, sizeof(m));

    // Extract translation from last row.
    translation[0] = m[3][0];
    translation[1] = m[3][1];
    translation[2] = m[3][2];

    // Compute determinant to determine handedness.
    const f32 det = m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

    const f32 sign = (det < 0.0f) ? -1.0f : 1.0f;

    // Recover scale from axis lengths.
    scale[0] = sqrtf(m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2]) * sign;
    scale[1] = sqrtf(m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2]) * sign;
    scale[2] = sqrtf(m[2][0] * m[2][0] + m[2][1] * m[2][1] + m[2][2] * m[2][2]) * sign;

    // Normalize axes to get a pure rotation matrix.
    const f32 rsx = (scale[0] == 0.0f) ? 0.0f : 1.0f / scale[0];
    const f32 rsy = (scale[1] == 0.0f) ? 0.0f : 1.0f / scale[1];
    const f32 rsz = (scale[2] == 0.0f) ? 0.0f : 1.0f / scale[2];

    const f32 r00 = m[0][0] * rsx, r10 = m[1][0] * rsy, r20 = m[2][0] * rsz;
    const f32 r01 = m[0][1] * rsx, r11 = m[1][1] * rsy, r21 = m[2][1] * rsz;
    const f32 r02 = m[0][2] * rsx, r12 = m[1][2] * rsy, r22 = m[2][2] * rsz;

    // "Branchless" version of Mike Day's matrix to quaternion conversion.
    const int qc = r22 < 0 ? (r00 > r11 ? 0 : 1) : (r00 < -r11 ? 2 : 3);
    const f32 qs1 = qc & 2 ? -1.0f : 1.0f;
    const f32 qs2 = qc & 1 ? -1.0f : 1.0f;
    const f32 qs3 = (qc - 1) & 2 ? -1.0f : 1.0f;

    const f32 qt = 1.0f - qs3 * r00 - qs2 * r11 - qs1 * r22;
    const f32 qs = 0.5f / sqrtf(qt);

    rotation[qc ^ 3] = qs * qt;
    rotation[qc ^ 0] = qs * (r01 + qs1 * r10);
    rotation[qc ^ 1] = qs * (r20 + qs2 * r02);
    rotation[qc ^ 2] = qs * (r12 + qs3 * r21);
}

static void load_geometry(
    eastl::vector<Vertex>& vertices,
    eastl::vector<u32>& indices,
    eastl::vector<Mesh>& meshes,
    eastl::vector<MeshPrimitive>& mesh_primitives,
    eastl::vector<rhi::DrawIndexedIndirectCommand>& draw_cmds,
    const cgltf_data* cgltf_data
) {
    DEBUG_ASSERT(vertices.empty());
    DEBUG_ASSERT(indices.empty());
    DEBUG_ASSERT(meshes.empty());
    DEBUG_ASSERT(mesh_primitives.empty());
    DEBUG_ASSERT(draw_cmds.empty());
    DEBUG_ASSERT(cgltf_data);

    eastl::vector<f32> tmp;
    eastl::vector<Vertex> prim_vertices;
    eastl::vector<u32> prim_indices;
    eastl::vector<Vec3> prim_positions;
    eastl::vector<u32> remap;

    for (cgltf_size mi = 0; mi < cgltf_data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = cgltf_data->meshes[mi];

        meshes.push_back({draw_cmds.size(), mesh.primitives_count});

        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            ASSERT(prim.type == cgltf_primitive_type_triangles);
            ASSERT(prim.indices);

            const size_t vertex_count = vertices.size();

            const cgltf_size prim_vertex_count = prim.attributes[0].data->count;

            prim_vertices.resize(prim_vertex_count);
            prim_positions.resize(prim_vertex_count);

            tmp.resize(prim_vertex_count * 4);

            if (const cgltf_accessor* const pos =
                    cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0))
            {
                ASSERT(cgltf_num_components(pos->type) == 3);
                const cgltf_size size =
                    cgltf_accessor_unpack_floats(pos, tmp.data(), prim_vertex_count * 3);
                ASSERT(size == prim_vertex_count * 3);

                for (cgltf_size vi = 0; vi < prim_vertex_count; ++vi) {
                    prim_vertices[vi].px = meshopt_quantizeHalf(tmp[vi * 3 + 0]);
                    prim_vertices[vi].py = meshopt_quantizeHalf(tmp[vi * 3 + 1]);
                    prim_vertices[vi].pz = meshopt_quantizeHalf(tmp[vi * 3 + 2]);

                    prim_positions[vi] = {tmp[vi * 3 + 0], tmp[vi * 3 + 1], tmp[vi * 3 + 2]};
                }
            }

            if (const cgltf_accessor* const norm =
                    cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0))
            {
                ASSERT(cgltf_num_components(norm->type) == 3);
                const cgltf_size size =
                    cgltf_accessor_unpack_floats(norm, tmp.data(), prim_vertex_count * 3);
                ASSERT(size == prim_vertex_count * 3);

                for (cgltf_size ni = 0; ni < prim_vertex_count; ++ni) {
                    const Vec3 normal = {tmp[ni * 3 + 0], tmp[ni * 3 + 1], tmp[ni * 3 + 2]};
                    const Vec2 packed = pack_normal_octahedral(normal);
                    prim_vertices[ni].normal = pack_float2_to_rg8snorm(packed);
                }
            }

            if (const cgltf_accessor* const tangent =
                    cgltf_find_accessor(&prim, cgltf_attribute_type_tangent, 0))
            {
                ASSERT(cgltf_num_components(tangent->type) == 4);
                const cgltf_size size =
                    cgltf_accessor_unpack_floats(tangent, tmp.data(), prim_vertex_count * 4);
                ASSERT(size == prim_vertex_count * 4);

                for (cgltf_size ti = 0; ti < prim_vertex_count; ++ti) {
                    const Vec3 t = {tmp[ti * 4 + 0], tmp[ti * 4 + 1], tmp[ti * 4 + 2]};
                    const u32 bitangent_sign = tmp[ti * 4 + 3] == 1.0f ? 1 : 0;

                    prim_vertices[ti].tangent =
                        u32((bitangent_sign << 31)
                            | pack_float2_to_rg8snorm(pack_normal_octahedral(t)));
                }
            }

            if (const cgltf_accessor* const tex =
                    cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 0))
            {
                ASSERT(cgltf_num_components(tex->type) == 2);
                const cgltf_size size =
                    cgltf_accessor_unpack_floats(tex, tmp.data(), prim_vertex_count * 2);
                ASSERT(size == prim_vertex_count * 2);

                for (cgltf_size ti = 0; ti < prim_vertex_count; ++ti) {
                    prim_vertices[ti].u = meshopt_quantizeHalf(tmp[ti * 2 + 0]);
                    prim_vertices[ti].v = meshopt_quantizeHalf(tmp[ti * 2 + 1]);
                }
            }

            const size_t index_count = indices.size();
            const size_t prim_index_count = prim.indices->count;

            prim_indices.resize(prim_index_count);
            const cgltf_size size = cgltf_accessor_unpack_indices(
                prim.indices,
                prim_indices.data(),
                sizeof(u32),
                prim_index_count
            );
            ASSERT(size == prim_index_count);

            remap.resize(prim_vertex_count);
            const size_t unique_vertex_count = meshopt_generateVertexRemap(
                remap.data(),
                prim_indices.data(),
                prim_index_count,
                prim_vertices.data(),
                prim_vertex_count,
                sizeof(prim_vertices[0])
            );

            meshopt_remapVertexBuffer(
                prim_vertices.data(),
                prim_vertices.data(),
                prim_vertex_count,
                sizeof(prim_vertices[0]),
                remap.data()
            );
            meshopt_remapIndexBuffer(
                prim_indices.data(),
                prim_indices.data(),
                prim_index_count,
                remap.data()
            );

            prim_vertices.resize(unique_vertex_count);

            meshopt_optimizeVertexCache(
                prim_indices.data(),
                prim_indices.data(),
                prim_index_count,
                unique_vertex_count
            );
            meshopt_optimizeVertexFetch(
                prim_vertices.data(),
                prim_indices.data(),
                prim_index_count,
                prim_vertices.data(),
                unique_vertex_count,
                sizeof(prim_vertices[0])
            );

            indices.resize(index_count + prim_index_count);
            memcpy(indices.data() + index_count, prim_indices.data(), VEC_SIZE_BYTES(prim_indices));

            vertices.resize(vertex_count + unique_vertex_count);
            memcpy(
                vertices.data() + vertex_count,
                prim_vertices.data(),
                VEC_SIZE_BYTES(prim_vertices)
            );

            const rhi::DrawIndexedIndirectCommand draw_cmd = {
                .index_count = u32(prim_index_count),
                .instance_count = 1,
                .first_index = u32(index_count),
                .vertex_offset = i32(vertex_count),
                .first_instance = 0,
            };
            draw_cmds.push_back(draw_cmd);

            const MeshPrimitive mesh_primitive = {
                .vertex_count = unique_vertex_count,
                .mesh_idx = mi,
                .material = prim.material,
            };

            mesh_primitives.push_back(mesh_primitive);
        }
    }

    for (size_t pi = 0; pi < mesh_primitives.size(); ++pi) {
        Vec3 sphere_center{};

        const size_t vertex_offset = size_t(draw_cmds[pi].vertex_offset);

        for (size_t vi = 0; vi < mesh_primitives[pi].vertex_count; ++vi) {
            // TODO: implement a better algorithm.
            sphere_center += Vec3{
                meshopt_dequantizeHalf(vertices[vertex_offset + vi].px),
                meshopt_dequantizeHalf(vertices[vertex_offset + vi].py),
                meshopt_dequantizeHalf(vertices[vertex_offset + vi].pz)
            };
        }

        sphere_center /= float(mesh_primitives[pi].vertex_count);

        mesh_primitives[pi].sphere_center = sphere_center;
    }

    for (size_t pi = 0; pi < mesh_primitives.size(); ++pi) {
        f32 sphere_radius = 0.0f;

        const size_t vertex_offset = size_t(draw_cmds[pi].vertex_offset);

        for (size_t vi = 0; vi < mesh_primitives[pi].vertex_count; ++vi) {
            // TODO: implement a better algorithm.
            const Vec3 v = Vec3{
                meshopt_dequantizeHalf(vertices[vertex_offset + vi].px),
                meshopt_dequantizeHalf(vertices[vertex_offset + vi].py),
                meshopt_dequantizeHalf(vertices[vertex_offset + vi].pz)
            };
            sphere_radius =
                math::max(sphere_radius, math::magnitude(mesh_primitives[pi].sphere_center - v));
        }

        mesh_primitives[pi].sphere_radius = sphere_radius;
    }
}

static void load_materials(
    eastl::vector<DrawData>& draw_data,
    eastl::vector<Material>& materials,
    Vec3& sun_direction_world,
    const eastl::vector<Mesh>& meshes,
    const eastl::vector<MeshPrimitive>& mesh_primitives,
    const cgltf_data* cgltf_data
) {
    DEBUG_ASSERT(draw_data.empty());
    DEBUG_ASSERT(materials.empty());
    DEBUG_ASSERT(!meshes.empty());
    DEBUG_ASSERT(!mesh_primitives.empty());
    DEBUG_ASSERT(cgltf_data);

    const u32 material_offset = 1;

    for (cgltf_size ni = 0; ni < cgltf_data->nodes_count; ++ni) {
        const cgltf_node& node = cgltf_data->nodes[ni];

        if (node.mesh) {
            f32 local_to_world[16]{};
            cgltf_node_transform_world(&node, local_to_world);

            const Mesh mesh = meshes[cgltf_mesh_index(cgltf_data, node.mesh)];

            for (size_t pi = 0; pi < mesh.primitive_count; ++pi) {
                const cgltf_material* const material =
                    mesh_primitives[mesh.primitive_idx + pi].material;
                DrawData d{};

                f32 scale[3]{};
                decompose_transform(d.position.val, d.orientation.val, scale, local_to_world);
                ASSERT(math::almost_equal(scale[0], scale[1]));
                ASSERT(math::almost_equal(scale[0], scale[2]));
                ASSERT(scale[0] > 0.0f);
                d.scale = math::max(scale[0], math::max(scale[1], scale[2]));

                if (material) {
                    d.material_idx =
                        material_offset + u32(cgltf_material_index(cgltf_data, material));
                    d.render_pass_flags = material->alpha_mode == cgltf_alpha_mode_opaque
                        ? RENDER_PASS_OPAQUE_BIT
                        : RENDER_PASS_TRANSLUCENT_BIT;
                }
                d.sphere_center = mesh_primitives[mesh.primitive_idx + pi].sphere_center;
                d.sphere_radius = mesh_primitives[mesh.primitive_idx + pi].sphere_radius;
                draw_data.push_back(d);
            }
        }

        if (node.light && (node.light->type == cgltf_light_type_directional)) {
            Mat4 local_to_world{};
            cgltf_node_transform_world(&node, &local_to_world.col[0].val[0]);

            // From KHR_lights_punctual:
            // For light types that have a direction (directional and spot lights),
            // the light's direction is defined as the 3-vector (0.0, 0.0, -1.0)
            // and the rotation of the node orients the light accordingly.
            sun_direction_world =
                -Vec3{local_to_world(0, 2), local_to_world(1, 2), local_to_world(2, 2)};
        }
    }

    materials.resize(1); // Dummy index.

    for (cgltf_size i = 0; i < cgltf_data->materials_count; ++i) {
        const cgltf_material& mat = cgltf_data->materials[i];

        Material material{};

        if (mat.has_pbr_metallic_roughness) {
            if (mat.pbr_metallic_roughness.base_color_texture.texture) {
                material.albedo_tex_idx = u32(cgltf_texture_index(
                    cgltf_data,
                    mat.pbr_metallic_roughness.base_color_texture.texture
                ));
            }

            COPY_ARRAY_TO_ARRAY(
                material.albedo_factor.val,
                mat.pbr_metallic_roughness.base_color_factor
            );

            if (mat.pbr_metallic_roughness.metallic_roughness_texture.texture) {
                material.metallic_roughness_tex_idx = u32(cgltf_texture_index(
                    cgltf_data,
                    mat.pbr_metallic_roughness.metallic_roughness_texture.texture
                ));
            }

            material.metallic_factor = mat.pbr_metallic_roughness.metallic_factor;
            material.roughness_factor = mat.pbr_metallic_roughness.roughness_factor;
        }

        if (mat.normal_texture.texture) {
            material.normal_tex_idx =
                u32(cgltf_texture_index(cgltf_data, mat.normal_texture.texture));
        }

        materials.push_back(material);
    }
}

static void load_texture_paths(
    eastl::vector<eastl::string>& texture_paths,
    const eastl::string& gltf_path,
    const cgltf_data* cgltf_data
) {
    DEBUG_ASSERT(texture_paths.empty());
    DEBUG_ASSERT(!gltf_path.empty());
    DEBUG_ASSERT(cgltf_data);

    eastl::string base_path;
    const size_t last_separator_pos = gltf_path.find_last_of("/\\");
    if (last_separator_pos == eastl::string::npos) {
        base_path = "";
    } else {
        base_path = gltf_path.substr(0, last_separator_pos + 1);
    }

    for (cgltf_size i = 0; i < cgltf_data->textures_count; ++i) {
        const cgltf_texture& tex = cgltf_data->textures[i];
        ASSERT(tex.image);

        const cgltf_image& image = *tex.image;
        ASSERT(image.uri);

        eastl::string uri{image.uri};
        uri.resize(cgltf_decode_uri(uri.data()));

        const eastl::string::size_type dot_pos = uri.find_last_of('.');
        if (dot_pos != eastl::string::npos) {
            uri.replace(dot_pos, uri.size() - dot_pos, ".ktx2");
        }

        texture_paths.push_back(base_path + uri);
    }
}

// Stole some GLTF loading logic from niagara.
// https://github.com/zeux/niagara
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
) {
    DEBUG_ASSERT(vertices.empty());
    DEBUG_ASSERT(indices.empty());
    DEBUG_ASSERT(mesh_primitives.empty());
    DEBUG_ASSERT(draw_cmds.empty());
    DEBUG_ASSERT(draw_data.empty());
    DEBUG_ASSERT(materials.empty());
    DEBUG_ASSERT(texture_paths.empty());
    DEBUG_ASSERT(!gltf_path.empty());

    cgltf_options options{};
    cgltf_data* cgltf_data{};
    cgltf_result cgltf_result = cgltf_parse_file(&options, gltf_path.c_str(), &cgltf_data);
    if (cgltf_result != cgltf_result_success) {
        fprintf(stderr, "gltf loading failed: %s\n", cgltf_result_to_string(cgltf_result));
        return false;
    }
    DEFER(cgltf_free(cgltf_data));

    cgltf_result = cgltf_load_buffers(&options, cgltf_data, gltf_path.c_str());
    if (cgltf_result != cgltf_result_success) {
        fprintf(stderr, "gltf buffer loading failed: %s\n", cgltf_result_to_string(cgltf_result));
        return false;
    }

    cgltf_result = cgltf_validate(cgltf_data);
    if (cgltf_result != cgltf_result_success) {
        fprintf(stderr, "gltf validation failed: %s\n", cgltf_result_to_string(cgltf_result));
        return false;
    }

    eastl::vector<Mesh> meshes;

    load_geometry(vertices, indices, meshes, mesh_primitives, draw_cmds, cgltf_data);

    DEBUG_ASSERT(mesh_primitives.size() == draw_cmds.size());

    load_materials(draw_data, materials, sun_direction_world, meshes, mesh_primitives, cgltf_data);

    load_texture_paths(texture_paths, gltf_path, cgltf_data);

    // TODO: scene cache.

    return true;
}
