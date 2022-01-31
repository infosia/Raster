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

#include <glm/gtc/matrix_access.hpp>

namespace renderer
{
    struct Shader;
    struct Color;
    struct ShaderContext
    {
        ShaderContext(const ShaderContext &) = delete;
        ShaderContext &operator=(const ShaderContext &) = delete;

        glm::mat4 model{ glm::mat4(1.f) };
        glm::mat4 view{ glm::mat4(1.f) };
        glm::mat4 viewport{ glm::mat4(1.f) };
        glm::mat4 projection{ glm::mat4(1.f) };
        glm::vec4 lightPos{ glm::vec4(0.0f, 1.5f, 1.f, 0.f) };

        Image *framebuffer{ nullptr };
        std::vector<float> zbuffer;

        const glm::mat4 *jointMatrices{ nullptr };

        Color bgColor;
    };

    struct Shader
    {
        const Primitive *primitive{ nullptr };
        virtual glm::vec4 vertex(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert) = 0;
        virtual bool fragment(const ShaderContext &ctx, const glm::vec3 bar, bool backfacing, Color &color) = 0;

        glm::mat4 skinning(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert)
        {
            glm::mat4 skinMat = glm::identity<glm::mat4>();
            if (primitive->hasJoints() && ctx.jointMatrices) {
                const auto inJointIndices = primitive->joint(iface, ivert);
                const auto inJointWeights = primitive->weight(iface, ivert);
                const auto jointMatrices = ctx.jointMatrices;
                skinMat = inJointWeights.x * jointMatrices[int(inJointIndices.x)] + inJointWeights.y * jointMatrices[int(inJointIndices.y)] + inJointWeights.z * jointMatrices[int(inJointIndices.z)] + inJointWeights.w * jointMatrices[int(inJointIndices.w)];
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
            : vNormal(glm::mat3x3())
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
        glm::mat3x3 vNormal;
        glm::mat4x3 vTangent;
        glm::mat3x2 vUV;
        glm::vec3 vLightVec;
        glm::vec3 vViewVec;

        DefaultShader()
            : vNormal(glm::mat3x3())
            , vTangent(glm::mat3x3())
            , vUV(glm::mat3x2())
        {
        }

        virtual glm::vec4 vertex(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert) override
        {
            assert(primitive != nullptr);

            const auto vert = primitive->vert(iface, ivert);
            const glm::mat4 skinMat = skinning(ctx, iface, ivert);

            auto gl_Position = glm::project(vert, ctx.view * ctx.model * skinMat, ctx.projection,
                glm::vec4{ 0.0f, 0.0f, ctx.framebuffer->width, ctx.framebuffer->height });

            if (primitive->hasNormal())
                vNormal[ivert] = primitive->normal(iface, ivert) * glm::mat3(ctx.model * skinMat);

            if (primitive->hasTangent())
                vTangent[ivert] = primitive->tangent(iface, ivert) * glm::mat3(ctx.model * skinMat);

            if (primitive->hasUV())
                vUV[ivert] = primitive->uv(iface, ivert);

            glm::vec4 pos = ctx.model * glm::vec4(vert, 1.f);
            vLightVec = glm::vec3(ctx.lightPos) - glm::vec3(pos);
            vViewVec = -glm::vec3(pos);

            return glm::vec4(gl_Position, 1.f);
        }

        virtual bool fragment(const ShaderContext &ctx, const glm::vec3 bar, bool backfacing, Color &color) override
        {
            const auto UV = vUV * bar;

            const auto inNormal = vNormal * bar;
            const auto inTangent = vTangent * glm::vec4(bar, 1.f);

            if (primitive->material) {
                const auto material = primitive->material;

                // double-sided
                if (!material->doubleSided && backfacing)
                    return true;

                if (material->baseColorTexture) {
                    const auto texture = material->baseColorTexture->image;
                    auto diffuse = texture->get(UV.x * texture->width, UV.y * texture->height);
                    color.copy(diffuse);

                    if (material->alphaMode != AlphaMode::Opaque && texture->format == Image::Format::RGBA && diffuse.A() == 0)
                        return true;

                    if (!material->unlit) {
                        auto N = glm::normalize(inNormal);

                        if (primitive->hasTangent() && material->normalTexture) {
                            const auto T0 = glm::normalize(inTangent);
                            const auto T1 = T0 - glm::dot(T0, N) * N;
                            const auto B = glm::cross(N, T1);
                            const auto TBN = glm::mat3(T1, B, N);

                            const auto image = material->normalTexture->image;
                            const auto normalMap = image->get(UV.x * image->width, UV.y * image->height);
                            N = glm::normalize(TBN * normalMap.toNormal());

                            //const auto V = glm::normalize(vViewVec);
                            //const auto R = reflect(-L, N);
                            //const auto specular = std::pow(std::fmax(glm::dot(R, V), 0.f), 32.0f);
                        }

                        const auto L = glm::normalize(vLightVec);
                        const auto DF = std::fmin(1.f, std::fmax(glm::dot(N, L), 0.7f));

                        if (DF > 0) {
                            Color newColor(diffuse * DF, diffuse.A());
                            color.copy(newColor);
                        }
                    }
                }

                // alpha-cutoff
                if (material->alphaMode == AlphaMode::Mask && color.Af() < material->alphaCutOff)
                    return true;
            }

            return false;
        }
    };
}