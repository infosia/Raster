#pragma once

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
#include "pch.h"

namespace renderer
{
    struct Shader;
    class Color;
    struct ShaderContext
    {
        ShaderContext() = default;
        ShaderContext(const ShaderContext &) = delete;
        ShaderContext &operator=(const ShaderContext &) = delete;

        glm::mat4 model{};
        glm::mat4 view{};
        glm::mat4 viewport{};
        glm::mat4 projection{};

        Camera camera{};
        Light *light{ nullptr };

        Image *framebuffer{ nullptr };
        std::vector<float> zbuffer;

        Color bgColor{ 255, 255, 255, 255 };

        // Max limit of shading color changes
        float maxShadingFactor = 0.8f;
    };

    struct Shader
    {
        const Primitive *primitive{ nullptr };
        const glm::mat4 *jointMatrices{ nullptr };
        glm::mat4 bindMatrix;

        virtual glm::vec4 vertex(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert) = 0;
        virtual bool fragment(const ShaderContext &ctx, const glm::vec3 bar, bool backfacing, Color &color) = 0;

        glm::mat4 skinning(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert)
        {
            glm::mat4 skinMat = glm::identity<glm::mat4>();
            if (primitive->hasJoints() && jointMatrices) {
                const auto inJointIndices = primitive->joint(iface, ivert);
                const auto inJointWeights = primitive->weight(iface, ivert);
                skinMat = inJointWeights.x * jointMatrices[int(inJointIndices.x)] + inJointWeights.y * jointMatrices[int(inJointIndices.y)] + inJointWeights.z * jointMatrices[int(inJointIndices.z)] + inJointWeights.w * jointMatrices[int(inJointIndices.w)];
            } else {
                skinMat = bindMatrix;
            }
            return skinMat;
        }
    };

    static glm::vec3 reflect(const glm::vec3 &I, const glm::vec3 &N)
    {
        return I - 2.0f * glm::dot(N, I) * N;
    }

    struct OutlineShader : Shader
    {
        glm::mat3x3 vNormal;

        OutlineShader()
            : vNormal()
        {
        }

        virtual glm::vec4 vertex(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert) override
        {
            assert(primitive != nullptr);

            auto vertex = primitive->vert(iface, ivert);
            const glm::mat4 skinMat = skinning(ctx, iface, ivert);

            if (primitive->hasNormal()) {
                vNormal[ivert] = primitive->normal(iface, ivert) * glm::mat3(ctx.model * skinMat);

                const auto normal = glm::normalize(primitive->normal(iface, ivert));
                const auto outlineOffset = normal * 0.002f;
                vertex = vertex + outlineOffset;
            }

            auto gl_Position = glm::project(vertex, ctx.view * ctx.model * skinMat, ctx.projection,
                glm::vec4{ 0.0f, 0.0f, ctx.framebuffer->width, ctx.framebuffer->height });

            return glm::vec4(gl_Position, 1.f);
        }

        virtual bool fragment(const ShaderContext &ctx, const glm::vec3 bar, bool backfacing, Color &color) override
        {
            if (!backfacing)
                return true;

            Color outlineColor(50, 50, 50, 255);
            color.copy(outlineColor);

            return false;
        }
    };

    struct DefaultShader : Shader
    {
        glm::mat4x3 vColor;
        glm::mat3x3 vPosition;
        glm::mat3x3 vNormal;
        glm::mat4x3 vTangent;
        glm::mat3x2 vUV;

        DefaultShader()
            : vNormal()
            , vTangent()
            , vColor()
            , vUV()
        {
        }

        virtual glm::vec4 vertex(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert) override
        {
            assert(primitive != nullptr);

            const auto vert = primitive->vert(iface, ivert);
            const glm::mat4 skinMat4 = ctx.model * skinning(ctx, iface, ivert);
            const glm::mat3 skinMat3 = glm::mat3(skinMat4);

            auto gl_Position = glm::project(vert, ctx.view * skinMat4, ctx.projection,
                glm::vec4{ 0.0f, 0.0f, ctx.framebuffer->width, ctx.framebuffer->height });

            if (primitive->hasNormal())
                vNormal[ivert] = primitive->normal(iface, ivert) * skinMat3;

            if (primitive->hasTangent())
                vTangent[ivert] = primitive->tangent(iface, ivert) * skinMat3;

            if (primitive->hasColor())
                vColor[ivert] = primitive->color(iface, ivert);

            if (primitive->hasUV())
                vUV[ivert] = primitive->uv(iface, ivert);

            vPosition[ivert] = vert * skinMat3;

            return glm::vec4(gl_Position, 1.f);
        }

        virtual bool fragment(const ShaderContext &ctx, const glm::vec3 bar, bool backfacing, Color &color) override
        {
            const auto UV = vUV * bar;

            // This shader uses single light only
            const auto light = ctx.light;

            const auto inNormal = vNormal * bar;
            const auto inTangent = vTangent * glm::vec4(bar, 1.f);
            const auto inPosition = vPosition * bar;
            const auto lightDir = glm::normalize(light->position - inPosition);
            const auto viewDir = glm::normalize(ctx.camera.translation - inPosition);
            const auto halfDir = glm::normalize(lightDir - viewDir);
            const auto inColor = vColor * glm::vec4(bar, 1.f);

            if (primitive->material) {
                const auto material = primitive->material;

                // double-sided
                if (!material->doubleSided && backfacing)
                    return true;

                if (material->baseColorTexture) {
                    const auto texture = material->baseColorTexture->image;
                    auto diffuse = texture->get(UV.x * texture->width, UV.y * texture->height);

                    if (material->alphaMode != AlphaMode::Opaque && texture->format == Image::Format::RGBA && diffuse.A() == 0)
                        return true;

                    color.copy(color + diffuse);
                } else {
                    // base color (gamma corrected)
                    color.copy(color + material->baseColorFactor_sRGB);
                }

                if (!material->unlit) {
                    auto N = glm::normalize(inNormal);
                    const auto L = glm::normalize(lightDir);
                    float specular = 0.f;
                    float shininess = 16.f;

                    if (primitive->hasTangent() && material->normalTexture) {
                        const auto T0 = glm::normalize(inTangent);
                        const auto T1 = T0 - glm::dot(T0, N) * N;
                        const auto B = glm::cross(N, T1);
                        const auto TBN = glm::mat3(T1, B, N);

                        const auto image = material->normalTexture->image;
                        const auto normalMap = image->get(UV.x * image->width, UV.y * image->height);
                        N = glm::normalize(TBN * normalMap.toNormal());
                    }

                    // Blinn-Phong
                    specular = std::fmin(std::pow(std::fmax(glm::dot(halfDir, N), 0.f), shininess), ctx.maxShadingFactor);

                    // Phong
                    //specular = std::pow(std::fmax(glm::dot(reflect(L, N), viewDir), 0.f), shininess);

                    const auto shadingFactor = std::fmin(1.f, std::fmax(glm::dot(N, L), ctx.maxShadingFactor));
                    auto specularColor = light->color * specular * material->specularFactor * (material->metallicFactor - material->roughnessFactor);

                    if (shadingFactor > 0) {
                        Color newColor(color * shadingFactor + specularColor, color.A());
                        color.copy(newColor);
                    }
                }

                // alpha-cutoff
                if (material->alphaMode == AlphaMode::Mask && color.Af() < material->alphaCutOff)
                    return true;
            }

            if (primitive->hasColor()) {
                color.copy(color * glm::vec4(inColor, 1.f));
            }

            return false;
        }
    };
}
