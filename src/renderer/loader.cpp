/* distributed under MIT license:
 * 
 * Copyright (c) 2022 Kota Iguchi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "renderer/loader.h"
#include "pch.h"
#include "renderer/scene.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <codecvt>
#include <mikktspace.h>
#include <set>
#include <stb_image.h>

namespace renderer
{
    typedef struct smikktspace_data_t
    {
        cgltf_size vertex_count;
        cgltf_float *normals;
        cgltf_float *vertices;
        cgltf_float *texcoord;
        uint8_t *buffer;
    } smikktspace_data_t;

    static int tm_mikk_getNumFaces(const SMikkTSpaceContext *pContext)
    {
        smikktspace_data_t *data = (smikktspace_data_t *)pContext->m_pUserData;
        return (int)data->vertex_count;
    }

    static int tm_mikk_getNumVerticesOfFace(const SMikkTSpaceContext *pContext, const int iFace)
    {
        return 3;
    }

    static void tm_mikk_getPosition(const SMikkTSpaceContext *pContext, float *fvPosOut, const int iFace, const int iVert)
    {
        smikktspace_data_t *data = (smikktspace_data_t *)pContext->m_pUserData;
        memcpy(fvPosOut, (data->vertices + (iFace * 3)), 3 * sizeof(float));
    }

    static void tm_mikk_getNormal(const SMikkTSpaceContext *pContext, float *fvNormOut, const int iFace, const int iVert)
    {
        smikktspace_data_t *data = (smikktspace_data_t *)pContext->m_pUserData;
        memcpy(fvNormOut, (data->normals + (iFace * 3)), 3 * sizeof(float));
    }

    static void tm_mikk_getTexCoord(const SMikkTSpaceContext *pContext, float *fvTexcOut, const int iFace, const int iVert)
    {
        smikktspace_data_t *data = (smikktspace_data_t *)pContext->m_pUserData;
        memcpy(fvTexcOut, (data->texcoord + (iFace * 2)), 2 * sizeof(float));
    }

    static void tm_mikk_setTSpace(const SMikkTSpaceContext *pContext, const float fvTangent[], const float fvBiTangent[], const float fMagS, const float fMagT,
        const tbool bIsOrientationPreserving, const int iFace, const int iVert)
    {
        smikktspace_data_t *data = (smikktspace_data_t *)pContext->m_pUserData;
        cgltf_float *buffer = (cgltf_float *)data->buffer;
        memcpy(buffer + (iFace * 4), fvTangent, 3 * sizeof(float));

        glm::vec3 tangent = { fvTangent[0], fvTangent[1], fvTangent[2] };
        glm::vec3 normal = { data->normals[iFace * 3], data->normals[iFace * 3 + 1], data->normals[iFace * 3 + 2] };
        glm::vec3 bitangent = { fvBiTangent[0], fvBiTangent[1], fvBiTangent[2] };

        cgltf_float bi = glm::dot(glm::cross(normal, tangent), bitangent) > 0.f ? 1.f : -1.f;
        memcpy(buffer + (iFace * 4) + 3, &bi, sizeof(float));
    }

    static SMikkTSpaceInterface tangent_interface = {
        tm_mikk_getNumFaces,
        tm_mikk_getNumVerticesOfFace,
        tm_mikk_getNormal,
        tm_mikk_getPosition,
        tm_mikk_getTexCoord,
        nullptr,
        tm_mikk_setTSpace
    };

    static bool checkNodeHierarchy(cgltf_node *node, std::set<cgltf_node *> parents)
    {
        // node should not point one of its parent
        // it causes infinite loop
        if (parents.count(node) > 0)
            return false;

        parents.emplace(node);
        for (cgltf_size i = 0; i < node->children_count; ++i) {
            const auto child = node->children[i];
            // parent of the child is not right
            if (child->parent != node)
                return false;

            if (!checkNodeHierarchy(child, parents))
                return false;
        }
        return true;
    }

    static cgltf_result ValidateGLTF(cgltf_data *data, RenderOptions &options)
    {
        const auto result = cgltf_validate(data);
        if (result != cgltf_result_success)
            return result;

        if (data->scene == nullptr) {
            if (!options.silent)
                std::cout << "[ERROR] No scene found in glTF. Nothing to render." << std::endl;
            return cgltf_result_invalid_gltf;
        }

        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
            std::set<cgltf_node *> parents;
            if (!checkNodeHierarchy(data->scene->nodes[i], parents)) {
                if (!options.silent)
                    std::cout << "[ERROR] Invaid node hierarchy found in glTF." << std::endl;
                return cgltf_result_invalid_gltf;
            }
        }

        return cgltf_result_success;
    }

    static bool LoadTexture(cgltf_data *data, cgltf_texture *ctexture, Scene *scene, Texture *texture, Image *image)
    {
        assert(texture->image == nullptr);

        texture->image = image;

        if (ctexture->image == nullptr || ctexture->image->buffer_view == nullptr)
            return false;

        if (ctexture->image->name)
            texture->name = ctexture->image->name;
        else if (ctexture->name)
            texture->name = ctexture->name;

        if (ctexture->image->mime_type)
            texture->mimeType = ctexture->image->mime_type;

        const auto buffer_view = ctexture->image->buffer_view;
        const auto src_buffer = (uint8_t *)buffer_view->buffer->data + buffer_view->offset;

        int w, h, n;
        const auto image_data = stbi_load_from_memory(src_buffer, buffer_view->size, &w, &h, &n, 0);

        if (image_data == nullptr)
            return false;

        texture->image->init(w, h, (Image::Format)n, image_data);

        stbi_image_free(image_data);

        return true;
    }

    inline AlphaMode getAlphaMode(cgltf_alpha_mode &mode)
    {
        switch (mode) {
        case cgltf_alpha_mode_blend:
            return AlphaMode::Blend;
        case cgltf_alpha_mode_mask:
            return AlphaMode::Mask;
        default:
            return AlphaMode::Opaque;
        }
    }

    static void LoadMaterial(cgltf_data *data, cgltf_material *cmat, Scene *scene, Material *material)
    {
        if (cmat->has_pbr_metallic_roughness) {
            material->baseColorFactor = glm::make_vec4(cmat->pbr_metallic_roughness.base_color_factor);
            material->baseColorFactor_sRGB = glm::convertLinearToSRGB(material->baseColorFactor);
            const auto ctexture = cmat->pbr_metallic_roughness.base_color_texture.texture;

            material->metallicFactor = cmat->pbr_metallic_roughness.metallic_factor;
            material->roughnessFactor = cmat->pbr_metallic_roughness.roughness_factor;

            // Assuming textures are already loaded
            if (ctexture && scene->textures.size() > 0)
                material->baseColorTexture = &scene->textures.at(ctexture - data->textures);
        }

        if (cmat->normal_texture.texture != nullptr) {
            const auto ctexture = cmat->normal_texture.texture;

            // Assuming textures are already loaded
            if (ctexture && scene->textures.size() > 0)
                material->normalTexture = &scene->textures.at(ctexture - data->textures);
        }

        if (cmat->has_specular) {
            material->specularFactor = cmat->specular.specular_factor;
        }

        material->doubleSided = cmat->double_sided;
        material->alphaMode = getAlphaMode(cmat->alpha_mode);
        material->alphaCutOff = cmat->alpha_cutoff;
        material->unlit = cmat->unlit;
    }

    inline glm::vec3 min(const glm::vec3 &v1, const glm::vec3 &v2)
    {
        return glm::vec3{ std::min<float>(v1.x, v2.x), std::min<float>(v1.y, v2.y), std::min<float>(v1.z, v2.z) };
    }

    inline glm::vec3 max(const glm::vec3 &v1, const glm::vec3 &v2)
    {
        return glm::vec3{ std::max<float>(v1.x, v2.x), std::max<float>(v1.y, v2.y), std::max<float>(v1.z, v2.z) };
    }

    static void LoadPrimitive(cgltf_data *cdata, cgltf_primitive *cprim, Scene *scene, Primitive *primitive)
    {
        // Assuming materials are already loaded
        if (cprim->material && scene->materials.size() > 0)
            primitive->material = &scene->materials.at(cprim->material - cdata->materials);

        const auto indices = cprim->indices;
        if (indices == nullptr || indices->count == 0) {
            if (!scene->options.silent)
                std::cout << "[ERROR] primitive indice should not be null" << std::endl;
            return;
        }

        const auto num_indices = indices->count;
        const cgltf_accessor *acc_POS = nullptr;
        const cgltf_accessor *acc_NRM = nullptr;
        const cgltf_accessor *acc_TEX = nullptr;
        const cgltf_accessor *acc_TGT = nullptr;
        const cgltf_accessor *acc_JOINTS = nullptr;
        const cgltf_accessor *acc_WEIGHTS = nullptr;
        const cgltf_accessor *acc_COLOR = nullptr;

        for (cgltf_size i = 0; i < cprim->attributes_count; ++i) {
            const auto attr = &cprim->attributes[i];
            if (attr->type == cgltf_attribute_type_position) {
                acc_POS = attr->data;
            } else if (attr->type == cgltf_attribute_type_normal) {
                acc_NRM = attr->data;
            } else if (attr->type == cgltf_attribute_type_texcoord) {
                acc_TEX = attr->data;
            } else if (attr->type == cgltf_attribute_type_tangent) {
                acc_TGT = attr->data;
            } else if (attr->type == cgltf_attribute_type_joints) {
                acc_JOINTS = attr->data;
            } else if (attr->type == cgltf_attribute_type_weights) {
                acc_WEIGHTS = attr->data;
            } else if (attr->type == cgltf_attribute_type_color) {
                acc_COLOR = attr->data;
            }
        }

        if (acc_POS == nullptr || acc_POS->count == 0) {
            if (!scene->options.silent)
                std::cout << "[ERROR] primitive vertices should not be null" << std::endl;
            return;
        }

        cgltf_float *vertices_data = nullptr;
        if (acc_POS) {
            const auto num_components = cgltf_num_components(acc_POS->type);
            const auto unpack_count = acc_POS->count * num_components;
            assert(num_components == 3);
            vertices_data = (cgltf_float *)malloc(unpack_count * sizeof(cgltf_float));
            cgltf_accessor_unpack_floats(acc_POS, vertices_data, unpack_count);

            primitive->bbmin = glm::vec3(std::numeric_limits<float>::max());
            primitive->bbmax = glm::vec3(std::numeric_limits<float>::min());

            primitive->vertices.resize(acc_POS->count);
            for (cgltf_size i = 0; i < acc_POS->count; ++i) {
                primitive->vertices[i] = glm::make_vec3(vertices_data + (i * 3));
                primitive->bbmin = renderer::min(primitive->bbmin, primitive->vertices[i]);
                primitive->bbmax = renderer::max(primitive->bbmax, primitive->vertices[i]);
            }
            primitive->center = (primitive->bbmin + primitive->bbmax) / 2.f;
        }

        cgltf_float *normals_data = nullptr;
        if (acc_NRM) {
            assert(acc_POS->count == acc_NRM->count);
            const auto num_components = cgltf_num_components(acc_NRM->type);
            const auto unpack_count_n = acc_NRM->count * num_components;
            assert(num_components == 3);
            normals_data = (cgltf_float *)malloc(unpack_count_n * sizeof(cgltf_float));
            cgltf_accessor_unpack_floats(acc_NRM, normals_data, unpack_count_n);

            primitive->normals.resize(acc_NRM->count);
            for (cgltf_size i = 0; i < acc_NRM->count; ++i) {
                primitive->normals[i] = glm::make_vec3(normals_data + (i * 3));
            }
        }

        cgltf_float *texcoords_data = nullptr;
        if (acc_TEX) {
            assert(acc_POS->count == acc_TEX->count);
            const auto num_components = cgltf_num_components(acc_TEX->type);
            const auto unpack_count_n = acc_TEX->count * num_components;
            assert(num_components == 2);
            texcoords_data = (cgltf_float *)malloc(unpack_count_n * sizeof(cgltf_float));
            cgltf_accessor_unpack_floats(acc_TEX, texcoords_data, unpack_count_n);

            primitive->uvs.resize(acc_TEX->count);
            for (cgltf_size i = 0; i < acc_TEX->count; ++i) {
                primitive->uvs[i] = glm::make_vec2(texcoords_data + (i * 2));
            }
        }

        if (acc_WEIGHTS && acc_JOINTS) {
            assert(acc_POS->count == acc_JOINTS->count);
            assert(acc_POS->count == acc_WEIGHTS->count);

            primitive->joints.resize(acc_JOINTS->count);
            for (cgltf_size i = 0; i < acc_JOINTS->count; ++i) {
                cgltf_uint index[4];
                if (cgltf_accessor_read_uint(acc_JOINTS, i, index, 4)) {
                    primitive->joints[i] = glm::make_vec4(index);
                }
            }

            cgltf_size unpack_count = acc_WEIGHTS->count * 4;
            cgltf_float *weights_data = (cgltf_float *)calloc(unpack_count, sizeof(cgltf_float));
            cgltf_accessor_unpack_floats(acc_WEIGHTS, weights_data, unpack_count);

            primitive->weights.resize(acc_WEIGHTS->count);
            for (cgltf_size i = 0; i < acc_WEIGHTS->count; ++i) {
                primitive->weights[i] = glm::make_vec4(weights_data + (i * 4));
            }

            free(weights_data);
        }

        if (acc_TGT) {
            assert(acc_POS->count == acc_TGT->count);
            const auto num_components = cgltf_num_components(acc_TGT->type);
            const auto unpack_count = acc_TGT->count * num_components;
            assert(num_components == 4);
            auto *tgt = (cgltf_float *)malloc(unpack_count * sizeof(cgltf_float));
            cgltf_accessor_unpack_floats(acc_TGT, tgt, unpack_count);

            primitive->tangents.resize(acc_TGT->count);
            for (cgltf_size i = 0; i < acc_TGT->count; ++i) {
                primitive->tangents[i] = glm::make_vec4(tgt + (i * 4));
            }

            free(tgt);
        } else if (vertices_data && normals_data && texcoords_data) {
            cgltf_size num_vertices = acc_POS->count;
            auto *tgt = (cgltf_float *)calloc(1, num_vertices * 4 * sizeof(cgltf_float));
            smikktspace_data_t mikk_data = {};
            mikk_data.normals = normals_data;
            mikk_data.vertices = vertices_data;
            mikk_data.texcoord = texcoords_data;
            mikk_data.vertex_count = num_vertices;
            mikk_data.buffer = (uint8_t *)tgt;

            SMikkTSpaceContext mikk_ctx = { &tangent_interface, &mikk_data };
            genTangSpaceDefault(&mikk_ctx);

            primitive->tangents.resize(num_vertices);
            for (cgltf_size i = 0; i < num_vertices; ++i) {
                primitive->tangents[i] = glm::make_vec4(tgt + (i * 4));
            }

            free(tgt);
        }

        if (acc_COLOR) {
            assert(acc_POS->count == acc_COLOR->count);
            const auto num_components = cgltf_num_components(acc_COLOR->type);
            const auto unpack_count = acc_COLOR->count * num_components;
            assert(num_components == 4);
            auto *colors = (cgltf_float *)malloc(unpack_count * sizeof(cgltf_float));
            cgltf_accessor_unpack_floats(acc_COLOR, colors, unpack_count);

            primitive->colors.resize(acc_COLOR->count);
            for (cgltf_size i = 0; i < acc_COLOR->count; ++i) {
                primitive->colors[i] = glm::make_vec4(colors + (i * 4));
            }

            free(colors);
        }

        if (vertices_data)
            free(vertices_data);
        if (normals_data)
            free(normals_data);
        if (texcoords_data)
            free(texcoords_data);

        primitive->indices.resize(num_indices);
        for (cgltf_size i = 0; i < num_indices; ++i) {
            primitive->indices[i] = cgltf_accessor_read_index(indices, i);
        }
    }

    static glm::mat4 getNodeMatrix(const Node *node)
    {
        auto m = node->matrix;
        auto *parent = node->parent;

        // Avoid potential infinite loop
        // I think 64 nest is already too much
        std::uint8_t cnt = 0;
        while (parent != nullptr) {
            if (++cnt > 64)
                break;
            m = parent->matrix * m;
            parent = parent->parent;
        }

        return m;
    }

    static void UpdateJoints(Node *node)
    {
        node->bindMatrix = getNodeMatrix(node);
        if (node->skin) {
            const auto inverseTransform = glm::inverse(node->bindMatrix);
            const auto skin = node->skin;
            const auto numJoints = skin->joints.size();
            for (size_t i = 0; i < numJoints; ++i) {
                skin->jointMatrices[i] = getNodeMatrix(skin->joints[i]) * skin->inverseBindMatrices[i];
            }
        }

        for (auto child : node->children) {
            UpdateJoints(child);
        }
    }

    static void LoadSkin(cgltf_data *cdata, cgltf_skin *cskin, Scene *scene, Skin *skin)
    {
        if (cskin->name) {
            skin->name = cskin->name;
        }

        const auto num_matrices = cskin->inverse_bind_matrices->count;
        const auto num_components = cgltf_num_components(cskin->inverse_bind_matrices->type);
        const auto unpack_count = num_matrices * num_components;
        assert(num_components == 16);
        auto *ibm = (cgltf_float *)malloc(unpack_count * sizeof(cgltf_float));
        cgltf_accessor_unpack_floats(cskin->inverse_bind_matrices, ibm, unpack_count);

        assert(cskin->joints_count == num_matrices);

        // Assuming Nodes are already loaded
        skin->joints.resize(cskin->joints_count);
        skin->jointMatrices.resize(cskin->joints_count);
        skin->inverseBindMatrices.resize(num_matrices);
        for (cgltf_size i = 0; i < cskin->joints_count; ++i) {
            const auto nodeIndex = cskin->joints[i] - cdata->nodes;
            skin->joints[i] = &scene->allNodes[nodeIndex];
            skin->joints[i]->skin = skin;
            skin->inverseBindMatrices[i] = glm::make_mat4(ibm + (i * 16));
            skin->jointMatrices[i] = glm::identity<glm::mat4>();
        }

        free(ibm);
    }

    static void LoadMesh(cgltf_data *cdata, cgltf_mesh *cmesh, Scene *scene, Mesh *mesh)
    {
        if (cmesh->name) {
            mesh->name = cmesh->name;
        }

        mesh->bbmin = glm::vec3(std::numeric_limits<float>::max());
        mesh->bbmax = glm::vec3(std::numeric_limits<float>::min());
        mesh->primitives.resize(cmesh->primitives_count);
        for (cgltf_size i = 0; i < cmesh->primitives_count; ++i) {
            const auto primitive = &mesh->primitives.at(i);
            LoadPrimitive(cdata, &cmesh->primitives[i], scene, primitive);
            mesh->bbmin = renderer::min(mesh->bbmin, primitive->bbmin);
            mesh->bbmax = renderer::max(mesh->bbmax, primitive->bbmax);
        }
        mesh->center = (mesh->bbmax + mesh->bbmin) / 2.f;
    }

    static void LoadNode(cgltf_data *cdata, cgltf_node *cnode, Scene *scene, Node *node)
    {
        if (cnode->name) {
            node->name = cnode->name;
        }

        // Assuming meshes are already loaded
        if (cnode->mesh && scene->meshes.size() > 0)
            node->mesh = &scene->meshes.at(cnode->mesh - cdata->meshes);

        node->matrix = glm::mat4(1.0f);
        if (cnode->has_translation) {
            node->matrix = glm::translate(node->matrix, glm::vec3(glm::make_vec3(cnode->translation)));
        }
        if (cnode->has_rotation) {
            glm::quat q = glm::make_quat(cnode->rotation);
            node->matrix *= glm::mat4(q);
        }
        if (cnode->has_scale) {
            node->matrix = glm::scale(node->matrix, glm::vec3(glm::make_vec3(cnode->scale)));
        }
        if (cnode->has_matrix) {
            node->matrix = glm::make_mat4x4(cnode->matrix);
        }

        // Skin references will be updated from UpdateSkin
        // But wanted to make sure skin reference is set even when there's no joint references to it
        if (cnode->skin) {
            node->skin = &scene->skins.at(cnode->skin - cdata->skins);
        }
    }

    // Setting up node hierarchy in the scene
    static void LoadNodeInScene(cgltf_data *cdata, cgltf_node *cnode, Scene *scene, Node *node, Node *parent)
    {
        node->parent = parent;
        node->children.resize(cnode->children_count);
        for (cgltf_size i = 0; i < node->children.size(); ++i) {
            const auto cChild = cnode->children[i];
            const auto child = &scene->allNodes.at(cChild - cdata->nodes);
            node->children[i] = child;
            LoadNodeInScene(cdata, cChild, scene, child, node);
        }
    }

#ifdef WIN32
    static cgltf_result wchar_file_read(const struct cgltf_memory_options *memory_options, const struct cgltf_file_options *file_options, const std::wstring path, cgltf_size *size, void **data)
    {
        (void)file_options;
        void *(*memory_alloc)(void *, cgltf_size) = memory_options->alloc ? memory_options->alloc : &cgltf_default_alloc;
        void (*memory_free)(void *, void *) = memory_options->free ? memory_options->free : &cgltf_default_free;

        FILE *file = _wfopen(path.c_str(), L"rb");

        if (!file) {
            return cgltf_result_file_not_found;
        }

        cgltf_size file_size = size ? *size : 0;

        if (file_size == 0) {
            fseek(file, 0, SEEK_END);

            long length = ftell(file);
            if (length < 0) {
                fclose(file);
                return cgltf_result_io_error;
            }

            fseek(file, 0, SEEK_SET);
            file_size = (cgltf_size)length;
        }

        char *file_data = (char *)memory_alloc(memory_options->user_data, file_size);
        if (!file_data) {
            fclose(file);
            return cgltf_result_out_of_memory;
        }

        cgltf_size read_size = fread(file_data, 1, file_size, file);

        fclose(file);

        if (read_size != file_size) {
            memory_free(memory_options->user_data, file_data);
            return cgltf_result_io_error;
        }

        if (size) {
            *size = file_size;
        }
        if (data) {
            *data = file_data;
        }

        return cgltf_result_success;
    }

    static cgltf_result wstring_file_read(const struct cgltf_memory_options *memory_options, const struct cgltf_file_options *file_options, const char *path, cgltf_size *size, void **data)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return wchar_file_read(memory_options, file_options, converter.from_bytes(path), size, data);
    }
#endif // ifdef WIN32

    static bool LoadScene(cgltf_data *data, Scene &scene)
    {
        const auto start = std::chrono::system_clock::now();

        if (scene.options.verbose)
            std::cout << "[INFO] Loading scene...";

        scene.images.resize(data->textures_count);
        scene.textures.resize(data->textures_count);
#pragma omp parallel for
        for (int i = 0; i < (int)data->textures_count; ++i) {
            LoadTexture(data, &data->textures[i], &scene, &scene.textures.at(i), &scene.images.at(i));
        }

        scene.materials.resize(data->materials_count);
        for (cgltf_size i = 0; i < data->materials_count; ++i) {
            LoadMaterial(data, &data->materials[i], &scene, &scene.materials.at(i));
        }

        scene.bbmin = glm::vec3(std::numeric_limits<float>::max());
        scene.bbmax = glm::vec3(std::numeric_limits<float>::min());
        scene.meshes.resize(data->meshes_count);
        for (cgltf_size i = 0; i < data->meshes_count; ++i) {
            const auto mesh = &scene.meshes.at(i);
            LoadMesh(data, &data->meshes[i], &scene, mesh);
            scene.bbmin = renderer::min(scene.bbmin, mesh->bbmin);
            scene.bbmax = renderer::max(scene.bbmax, mesh->bbmax);
        }
        scene.center = (scene.bbmin + scene.bbmax) / 2.f;

        scene.skins.resize(data->skins_count);
        scene.allNodes.resize(data->nodes_count);
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            LoadNode(data, &data->nodes[i], &scene, &scene.allNodes.at(i));
        }

        // Update node hierarchy in the scene
        for (cgltf_size i = 0; data->scene && i < data->scene->nodes_count; ++i) {
            const auto cnode = data->scene->nodes[i];
            const auto node = &scene.allNodes.at(cnode - data->nodes);
            LoadNodeInScene(data, cnode, &scene, node, nullptr);
            scene.children.push_back(node);
        }

        for (cgltf_size i = 0; i < data->skins_count; ++i) {
            LoadSkin(data, &data->skins[i], &scene, &scene.skins.at(i));
        }

        // Update joint matrix
        for (const auto node : scene.children) {
            UpdateJoints(node);
        }

        if (scene.options.verbose) {
            const auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
            std::cout << "done in " << msec << " msec" << std::endl;
        }

        return true;
    }

    bool loadGLTF(std::string filename, Scene &scene)
    {
        cgltf_options gltf_options = {};
        cgltf_data *data = {};

#ifdef WIN32
        // enable multibyte file name
        gltf_options.file.read = &wstring_file_read;
#endif

        auto result = cgltf_parse_file(&gltf_options, filename.c_str(), &data);
        if (result != cgltf_result_success) {
            if (!scene.options.silent)
                std::cout << "[ERROR] Failed to parse " << filename << std::endl;
            return false;
        }

        result = cgltf_load_buffers(&gltf_options, data, filename.c_str());
        if (result != cgltf_result_success) {
            if (!scene.options.silent)
                std::cout << "[ERROR] Failed to load buffers from " << filename << std::endl;
            return false;
        }

        result = ValidateGLTF(data, scene.options);
        if (result != cgltf_result_success) {
            if (!scene.options.silent)
                std::cout << "[ERROR] Failed to validate " << filename << std::endl;
            return false;
        }

        LoadScene(data, scene);

        cgltf_free(data);

        return true;
    }
}
