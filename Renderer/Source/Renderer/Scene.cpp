#include "Scene.hpp"

#include "../Utils.hpp"
#include "../Math/Vec3.hpp"
#include "../Math/Mat4.hpp"
#include "../PackUtils.hpp"

#include <cgltf.h>
#include <meshoptimizer.h>

// TODO: more error handling.

static const char* cgltf_result_to_string(cgltf_result result)
{
    switch (result)
    {
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
static void DecomposeTransform(
    f32 translation[3],
    f32 rotation[4],
    f32 scale[3],
    const f32 transform[16]
)
{
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

static void LoadGeometry(
    std::vector<Vertex>& vertices,
    std::vector<u32>& indices,
    std::vector<Mesh>& meshes,
    std::vector<MeshPrimitive>& meshPrimitives,
    std::vector<VkDrawIndexedIndirectCommand>& drawCmds,
    const cgltf_data* cgltfData
)
{
    DEBUG_ASSERT(vertices.empty());
    DEBUG_ASSERT(indices.empty());
    DEBUG_ASSERT(meshes.empty());
    DEBUG_ASSERT(meshPrimitives.empty());
    DEBUG_ASSERT(drawCmds.empty());
    DEBUG_ASSERT(cgltfData);

    std::vector<f32> tmp;
    std::vector<Vertex> primVertices;
    std::vector<u32> primIndices;
    std::vector<Vec3> primPositions;
    std::vector<u32> remap;

    for (cgltf_size mi = 0; mi < cgltfData->meshes_count; ++mi)
    {
        const cgltf_mesh& mesh = cgltfData->meshes[mi];

        meshes.push_back({drawCmds.size(), mesh.primitives_count});

        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
        {
            const cgltf_primitive& prim = mesh.primitives[pi];
            ASSERT(prim.type == cgltf_primitive_type_triangles);
            ASSERT(prim.indices);

            const size_t vertexCount = vertices.size();

            const cgltf_size primVertexCount = prim.attributes[0].data->count;

            primVertices.resize(primVertexCount);
            primPositions.resize(primVertexCount);

            tmp.resize(primVertexCount * 4);

            if (const cgltf_accessor* const pos
                = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0))
            {
                ASSERT(cgltf_num_components(pos->type) == 3);
                const cgltf_size size
                    = cgltf_accessor_unpack_floats(pos, tmp.data(), primVertexCount * 3);
                ASSERT(size == primVertexCount * 3);

                for (cgltf_size vi = 0; vi < primVertexCount; ++vi)
                {
                    primVertices[vi].px = meshopt_quantizeHalf(tmp[vi * 3 + 0]);
                    primVertices[vi].py = meshopt_quantizeHalf(tmp[vi * 3 + 1]);
                    primVertices[vi].pz = meshopt_quantizeHalf(tmp[vi * 3 + 2]);

                    primPositions[vi] = {tmp[vi * 3 + 0], tmp[vi * 3 + 1], tmp[vi * 3 + 2]};
                }
            }

            if (const cgltf_accessor* const norm
                = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0))
            {
                ASSERT(cgltf_num_components(norm->type) == 3);
                const cgltf_size size
                    = cgltf_accessor_unpack_floats(norm, tmp.data(), primVertexCount * 3);
                ASSERT(size == primVertexCount * 3);

                for (cgltf_size ni = 0; ni < primVertexCount; ++ni)
                {
                    const Vec2 packed = PackNormalOctahedral(
                        Vec3{
                            tmp[ni * 3 + 0],
                            tmp[ni * 3 + 1],
                            tmp[ni * 3 + 2],
                        }
                    );
                    primVertices[ni].nx = meshopt_quantizeHalf(packed.X());
                    primVertices[ni].ny = meshopt_quantizeHalf(packed.Y());
                }
            }

            if (const cgltf_accessor* const tex
                = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 0))
            {
                ASSERT(cgltf_num_components(tex->type) == 2);
                const cgltf_size size
                    = cgltf_accessor_unpack_floats(tex, tmp.data(), primVertexCount * 2);
                ASSERT(size == primVertexCount * 2);

                for (cgltf_size ti = 0; ti < primVertexCount; ++ti)
                {
                    primVertices[ti].u = meshopt_quantizeHalf(tmp[ti * 2 + 0]);
                    primVertices[ti].v = meshopt_quantizeHalf(tmp[ti * 2 + 1]);
                }
            }

            const size_t indexCount = indices.size();
            const size_t primIndexCount = prim.indices->count;

            primIndices.resize(primIndexCount);
            const cgltf_size size = cgltf_accessor_unpack_indices(
                prim.indices,
                primIndices.data(),
                sizeof(u32),
                primIndexCount
            );
            ASSERT(size == primIndexCount);

            remap.resize(primVertexCount);
            const size_t uniqueVertexCount = meshopt_generateVertexRemap(
                remap.data(),
                primIndices.data(),
                primIndexCount,
                primVertices.data(),
                primVertexCount,
                sizeof(primVertices[0])
            );

            meshopt_remapVertexBuffer(
                primVertices.data(),
                primVertices.data(),
                primVertexCount,
                sizeof(primVertices[0]),
                remap.data()
            );
            meshopt_remapIndexBuffer(
                primIndices.data(),
                primIndices.data(),
                primIndexCount,
                remap.data()
            );

            primVertices.resize(uniqueVertexCount);

            meshopt_optimizeVertexCache(
                primIndices.data(),
                primIndices.data(),
                primIndexCount,
                uniqueVertexCount
            );
            meshopt_optimizeVertexFetch(
                primVertices.data(),
                primIndices.data(),
                primIndexCount,
                primVertices.data(),
                uniqueVertexCount,
                sizeof(primVertices[0])
            );

            indices.resize(indexCount + primIndexCount);
            memcpy(indices.data() + indexCount, primIndices.data(), VEC_SIZE_BYTES(primIndices));

            vertices.resize(vertexCount + uniqueVertexCount);
            memcpy(
                vertices.data() + vertexCount,
                primVertices.data(),
                VEC_SIZE_BYTES(primVertices)
            );

            VkDrawIndexedIndirectCommand drawCmd{};
            drawCmd.indexCount = u32(primIndexCount);
            drawCmd.instanceCount = 1;
            drawCmd.firstIndex = u32(indexCount);
            drawCmd.vertexOffset = i32(vertexCount);
            drawCmd.firstInstance = 0;
            drawCmds.push_back(drawCmd);

            MeshPrimitive meshPrimitive{};
            meshPrimitive.vertexCount = uniqueVertexCount;
            meshPrimitive.meshIdx = mi;
            meshPrimitive.material = prim.material;

            meshPrimitives.push_back(meshPrimitive);
        }
    }

    for (size_t pi = 0; pi < meshPrimitives.size(); ++pi)
    {
        Vec3 sphereCenter{};

        const size_t vertexOffset = size_t(drawCmds[pi].vertexOffset);

        for (size_t vi = 0; vi < meshPrimitives[pi].vertexCount; ++vi)
        {
            // TODO: implement a better algorithm.
            sphereCenter += Vec3{
                meshopt_dequantizeHalf(vertices[vertexOffset + vi].px),
                meshopt_dequantizeHalf(vertices[vertexOffset + vi].py),
                meshopt_dequantizeHalf(vertices[vertexOffset + vi].pz)
            };
        }

        sphereCenter /= float(meshPrimitives[pi].vertexCount);

        meshPrimitives[pi].sphereCenter = sphereCenter;
    }

    for (size_t pi = 0; pi < meshPrimitives.size(); ++pi)
    {
        f32 sphereRadius = 0.0f;

        const size_t vertexOffset = size_t(drawCmds[pi].vertexOffset);

        for (size_t vi = 0; vi < meshPrimitives[pi].vertexCount; ++vi)
        {
            // TODO: implement a better algorithm.
            const Vec3 v = Vec3{
                meshopt_dequantizeHalf(vertices[vertexOffset + vi].px),
                meshopt_dequantizeHalf(vertices[vertexOffset + vi].py),
                meshopt_dequantizeHalf(vertices[vertexOffset + vi].pz)
            };
            sphereRadius = Max(sphereRadius, Magnitude(meshPrimitives[pi].sphereCenter - v));
        }

        meshPrimitives[pi].sphereRadius = sphereRadius;
    }
}

static void LoadMaterials(
    std::vector<DrawData>& drawData,
    std::vector<Material>& materials,
    Vec3& sunDirectionWorld,
    const std::vector<Mesh>& meshes,
    const std::vector<MeshPrimitive>& meshPrimitives,
    const cgltf_data* cgltfData
)
{
    DEBUG_ASSERT(drawData.empty());
    DEBUG_ASSERT(materials.empty());
    DEBUG_ASSERT(!meshes.empty());
    DEBUG_ASSERT(!meshPrimitives.empty());
    DEBUG_ASSERT(cgltfData);

    const u32 materialOffset = 1;

    for (cgltf_size ni = 0; ni < cgltfData->nodes_count; ++ni)
    {
        const cgltf_node& node = cgltfData->nodes[ni];

        if (node.mesh)
        {
            f32 localToWorld[16]{};
            cgltf_node_transform_world(&node, localToWorld);

            const Mesh mesh = meshes[cgltf_mesh_index(cgltfData, node.mesh)];

            for (size_t pi = 0; pi < mesh.primitiveCount; ++pi)
            {
                const cgltf_material* const material
                    = meshPrimitives[mesh.primitiveIdx + pi].material;
                DrawData d{};

                f32 scale[3]{};
                DecomposeTransform(d.position.val, d.orientation.val, scale, localToWorld);
                ASSERT(AlmostEqual(scale[0], scale[1]));
                ASSERT(AlmostEqual(scale[0], scale[2]));
                ASSERT(scale[0] > 0.0f);
                d.scale = Max(scale[0], Max(scale[1], scale[2]));

                if (material)
                {
                    d.materialIdx = materialOffset + u32(cgltf_material_index(cgltfData, material));
                    d.renderPassFlags = material->alpha_mode == cgltf_alpha_mode_opaque
                        ? RENDER_PASS_OPAQUE_BIT
                        : RENDER_PASS_TRANSLUCENT_BIT;
                }
                d.sphereCenter = meshPrimitives[mesh.primitiveIdx + pi].sphereCenter;
                d.sphereRadius = meshPrimitives[mesh.primitiveIdx + pi].sphereRadius;
                drawData.push_back(d);
            }
        }

        if (node.light && (node.light->type == cgltf_light_type_directional))
        {
            Mat4 localToWorld{};
            cgltf_node_transform_world(&node, &localToWorld.col[0].val[0]);

            // From KHR_lights_punctual:
            // For light types that have a direction (directional and spot lights),
            // the light's direction is defined as the 3-vector (0.0, 0.0, -1.0)
            // and the rotation of the node orients the light accordingly.
            sunDirectionWorld = -Vec3{localToWorld(0, 2), localToWorld(1, 2), localToWorld(2, 2)};
        }
    }

    materials.resize(1); // Dummy index.

    for (cgltf_size i = 0; i < cgltfData->materials_count; ++i)
    {
        const cgltf_material& mat = cgltfData->materials[i];

        Material material{};

        if (mat.has_pbr_metallic_roughness)
        {
            if (mat.pbr_metallic_roughness.base_color_texture.texture)
            {
                material.albedoTexIdx = u32(cgltf_texture_index(
                    cgltfData,
                    mat.pbr_metallic_roughness.base_color_texture.texture
                ));
            }

            COPY_ARRAY_TO_ARRAY(
                material.albedoFactor.val,
                mat.pbr_metallic_roughness.base_color_factor
            );

            if (mat.pbr_metallic_roughness.metallic_roughness_texture.texture)
            {
                material.metallicRoughnessTexIdx = u32(cgltf_texture_index(
                    cgltfData,
                    mat.pbr_metallic_roughness.metallic_roughness_texture.texture
                ));
            }

            material.metallicFactor = mat.pbr_metallic_roughness.metallic_factor;
            material.roughnessFactor = mat.pbr_metallic_roughness.roughness_factor;
        }

        if (mat.normal_texture.texture)
        {
            material.normalTexIdx = u32(cgltf_texture_index(cgltfData, mat.normal_texture.texture));
        }

        materials.push_back(material);
    }
}

static void LoadTexturePaths(
    std::vector<std::string>& texturePaths,
    const std::string& gltfPath,
    const cgltf_data* cgltfData
)
{
    DEBUG_ASSERT(texturePaths.empty());
    DEBUG_ASSERT(!gltfPath.empty());
    DEBUG_ASSERT(cgltfData);

    std::string basePath;
    const size_t lastSeparatorPos = gltfPath.find_last_of("/\\");
    if (lastSeparatorPos == std::string::npos)
    {
        basePath = "";
    }
    else
    {
        basePath = gltfPath.substr(0, lastSeparatorPos + 1);
    }

    for (cgltf_size i = 0; i < cgltfData->textures_count; ++i)
    {
        const cgltf_texture& tex = cgltfData->textures[i];
        ASSERT(tex.image);

        const cgltf_image& image = *tex.image;
        ASSERT(image.uri);

        std::string uri{image.uri};
        uri.resize(cgltf_decode_uri(uri.data()));

        const std::string::size_type dotPos = uri.find_last_of('.');
        if (dotPos != std::string::npos)
        {
            uri.replace(dotPos, uri.size() - dotPos, ".ktx2");
        }

        texturePaths.push_back(basePath + uri);
    }
}

// Stole some GLTF loading logic from niagara.
// https://github.com/zeux/niagara
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
)
{
    DEBUG_ASSERT(vertices.empty());
    DEBUG_ASSERT(indices.empty());
    DEBUG_ASSERT(meshPrimitives.empty());
    DEBUG_ASSERT(drawCmds.empty());
    DEBUG_ASSERT(drawData.empty());
    DEBUG_ASSERT(materials.empty());
    DEBUG_ASSERT(texturePaths.empty());
    DEBUG_ASSERT(!gltfPath.empty());

    cgltf_options options{};
    cgltf_data* cgltfData{};
    cgltf_result cgltfResult = cgltf_parse_file(&options, gltfPath.c_str(), &cgltfData);
    if (cgltfResult != cgltf_result_success)
    {
        fprintf(stderr, "gltf loading failed: %s\n", cgltf_result_to_string(cgltfResult));
        return false;
    }
    DEFER(cgltf_free(cgltfData));

    cgltfResult = cgltf_load_buffers(&options, cgltfData, gltfPath.c_str());
    if (cgltfResult != cgltf_result_success)
    {
        fprintf(stderr, "gltf buffer loading failed: %s\n", cgltf_result_to_string(cgltfResult));
        return false;
    }

    cgltfResult = cgltf_validate(cgltfData);
    if (cgltfResult != cgltf_result_success)
    {
        fprintf(stderr, "gltf validation failed: %s\n", cgltf_result_to_string(cgltfResult));
        return false;
    }

    std::vector<Mesh> meshes;

    LoadGeometry(vertices, indices, meshes, meshPrimitives, drawCmds, cgltfData);

    DEBUG_ASSERT(meshPrimitives.size() == drawCmds.size());

    LoadMaterials(drawData, materials, sunDirectionWorld, meshes, meshPrimitives, cgltfData);

    LoadTexturePaths(texturePaths, gltfPath, cgltfData);

    // TODO: scene cache.

    return true;
}
