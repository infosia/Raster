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

        Color bgColor{ 255, 255, 255, 255 };

        // Max limit of shading color changes
        float maxShadingFactor = 0.8f;
    };

    struct Shader
    {
        Shader() = default;
        Shader(const Shader &) = delete;
        Shader &operator=(const Shader &) = delete;

        const Primitive *primitive{ nullptr };
        const glm::mat4 *jointMatrices{ nullptr };
        const std::vector<Morph> *morphs{ nullptr };
        glm::mat4 *bindMatrix{};

        Image framebuffer{};
        std::vector<float> zbuffer;

        virtual glm::vec4 vertex(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert) = 0;
        virtual bool fragment(const ShaderContext &ctx, const glm::vec3 &bar, const glm::vec3 &p, bool backfacing, Color &color) = 0;

        glm::mat4 skinning(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert)
        {
            glm::mat4 skinMat = glm::identity<glm::mat4>();
            if (primitive->hasJoints() && jointMatrices) {
                const auto inJointIndices = primitive->joint(iface, ivert);
                const auto inJointWeights = primitive->weight(iface, ivert);
                skinMat = (inJointWeights.x * jointMatrices[int(inJointIndices.x)])
                     + (inJointWeights.y * jointMatrices[int(inJointIndices.y)])
                     + (inJointWeights.z * jointMatrices[int(inJointIndices.z)])
                     + (inJointWeights.w * jointMatrices[int(inJointIndices.w)]);
            } else {
                skinMat = *bindMatrix;
            }
            return skinMat;
        }

        void morphVert(const uint32_t iface, const uint32_t ivert, glm::vec3 *vert)
        {
            // In glTF, all primitive target count under a mesh supposed to match.
            // However it tends to be omitted especially in old glTF.
            // At least wanted to make sure not to crash here.
            const auto numTargets = primitive->numTargets();
            if (morphs == nullptr || numTargets > morphs->size())
                return;

            for (size_t i = 0; i < numTargets; ++i) {
                *vert = *vert + (primitive->vertAtTarget(iface, ivert, i) * morphs->at(i).weight);
            }
        }

        void morphNormal(const uint32_t iface, const uint32_t ivert, glm::vec3 *normal)
        {
            const auto numTargets = primitive->numTargets();
            if (morphs == nullptr || numTargets > morphs->size())
                return;

            for (size_t i = 0; i < numTargets; ++i) {
                *normal = *normal + (primitive->normalAtTarget(iface, ivert, i) * morphs->at(i).weight);
            }
        }

        void morphTangent(const uint32_t iface, const uint32_t ivert, glm::vec4 *tangent)
        {
            const auto numTargets = primitive->numTargets();
            if (morphs == nullptr || numTargets > morphs->size())
                return;

            for (size_t i = 0; i < numTargets; ++i) {
                *tangent = *tangent + (primitive->tangentAtTarget(iface, ivert, i) * morphs->at(i).weight);
            }
        }
    };

    static glm::vec3 reflect(const glm::vec3 &I, const glm::vec3 &N)
    {
        return I - 2.0f * glm::dot(N, I) * N;
    }

    struct OutlineShader : Shader
    {
        OutlineShader(const OutlineShader &) = delete;
        OutlineShader &operator=(const OutlineShader &) = delete;

        glm::mat3x3 vNormal;
        glm::mat3x2 vUV;

        Color outlineColor{ 0, 0, 0, 178 }; // black with mix
        float outlineWidth{ 0.1f };

        OutlineShader()
            : vNormal()
            , vUV()
        {
        }

        virtual glm::vec4 vertex(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert) override
        {
            assert(primitive != nullptr);

            auto vertex = primitive->vert(iface, ivert);
            morphVert(iface, ivert, &vertex);

            const glm::mat4 skinMat = skinning(ctx, iface, ivert);

            if (primitive->hasNormal()) {
                auto normal = primitive->normal(iface, ivert);
                morphNormal(iface, ivert, &normal);

                vNormal[ivert] = normal * glm::mat3(ctx.model * skinMat);

                if (primitive->material && primitive->material->vrm0) {
                    const auto vrm0 = primitive->material->vrm0;
                    if (vrm0->outlineWidthMode == 0) {
                        outlineWidth = 0.f;
                    } else if (vrm0->outlineWidthMode == 2) {
                        outlineWidth = std::min(0.1f, vrm0->outlineWidth);
                    } else {
                        outlineWidth = vrm0->outlineWidth;
                    }
                }

                const auto outlineOffset = glm::normalize(normal) * 0.01f * outlineWidth;
                vertex = vertex + outlineOffset;
            }

            if (primitive->hasUV())
                vUV[ivert] = primitive->uv(iface, ivert);

            auto gl_Position = glm::project(vertex, ctx.view * ctx.model * skinMat, ctx.projection,
                glm::vec4{ 0.0f, 0.0f, framebuffer.width, framebuffer.height });

            return glm::vec4(gl_Position, 1.f);
        }

        virtual bool fragment(const ShaderContext &ctx, const glm::vec3 &bar, const glm::vec3 &p, bool backfacing, Color &color) override
        {
            if (!backfacing)
                return true;

            float outlineWidthFactor = 1.f;
            float outlineLightingMix = 1.f;

            if (primitive->material && primitive->material->vrm0) {
                const auto vrm0 = primitive->material->vrm0;
                if (vrm0->hasOutlineColor)
                    outlineColor = vrm0->outlineColor;

                if (vrm0->hasOutlineLightingMix)
                    outlineLightingMix = vrm0->outlineLightingMix;

                if (vrm0->hasOutlineWidthTexture && vrm0->outlineWidthTexture) {
                    const auto UV = vUV * bar;
                    const auto texture = vrm0->outlineWidthTexture;
                    outlineWidthFactor = texture->get(UV.x * texture->width, UV.y * texture->height).Rf();
                }
            }

            Color newColor = outlineColor * outlineWidthFactor * outlineLightingMix;
            color.copy(newColor);

            return false;
        }
    };

    struct DefaultShader : Shader
    {
        DefaultShader(const DefaultShader &) = delete;
        DefaultShader &operator=(const DefaultShader &) = delete;

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
            , vPosition()
        {
        }

        virtual glm::vec4 vertex(const ShaderContext &ctx, const uint32_t iface, const uint32_t ivert) override
        {
            assert(primitive != nullptr);

            auto vert = primitive->vert(iface, ivert);
            morphVert(iface, ivert, &vert);

            const glm::mat4 skinMat4 = ctx.model * skinning(ctx, iface, ivert);
            const glm::mat3 skinMat3 = glm::mat3(skinMat4);

            auto gl_Position = glm::project(vert, ctx.view * skinMat4, ctx.projection,
                glm::vec4{ 0.0f, 0.0f, framebuffer.width, framebuffer.height });

            if (primitive->hasNormal()) {
                auto normal = primitive->normal(iface, ivert);
                morphNormal(iface, ivert, &normal);

                vNormal[ivert] = normal * skinMat3;
            }

            if (primitive->hasTangent()) {
                auto tangent = primitive->tangent(iface, ivert);
                morphTangent(iface, ivert, &tangent);

                vTangent[ivert] = tangent * skinMat3;
            }

            if (primitive->hasColor())
                vColor[ivert] = primitive->color(iface, ivert);

            if (primitive->hasUV())
                vUV[ivert] = primitive->uv(iface, ivert);

            vPosition[ivert] = vert * skinMat3;

            return glm::vec4(gl_Position, 1.f);
        }

        inline glm::vec2 clampToEdge(const glm::vec2 &uv)
        {
            const float clampX = 1.f / (2 * primitive->material->baseColorTexture->image->width);
            const float clampY = 1.f / (2 * primitive->material->baseColorTexture->image->height);

            return glm::clamp(uv, glm::vec2(clampX, clampY), glm::vec2(1.f - clampX, 1.f - clampY));
        }

        inline glm::vec2 wrap(const glm::vec2 &uv, const Texture *texture)
        {
            const auto wrapS = texture->wrapS;
            const auto wrapT = texture->wrapT;

            // In case S and T wrappings are same
            if (wrapS == wrapT) {
                if (wrapS == WrapMode::CLAMP_TO_EDGE) {
                    return clampToEdge(uv);
                } else if (wrapS == WrapMode::MIRRORED_REPEAT) {
                    return glm::mirrorRepeat(uv);
                }
                return glm::repeat(uv);
            }

            // Bit tedious but needed to process S and T separately
            glm::vec2 ret = uv;
            if (wrapS == WrapMode::CLAMP_TO_EDGE) {
                ret.x = clampToEdge(uv).x;
            } else if (wrapS == WrapMode::MIRRORED_REPEAT) {
                ret.x = glm::mirrorRepeat(uv).x;
            } else if (wrapS == WrapMode::REPEAT) {
                ret.x = glm::repeat(uv).x;
            }

            if (wrapT == WrapMode::CLAMP_TO_EDGE) {
                ret.y = clampToEdge(uv).y;
            } else if (wrapT == WrapMode::MIRRORED_REPEAT) {
                ret.y = glm::mirrorRepeat(uv).y;
            } else if (wrapT == WrapMode::REPEAT) {
                ret.y = glm::repeat(uv).y;
            }

            return ret;
        }

        virtual bool fragment(const ShaderContext &ctx, const glm::vec3 &bar, const glm::vec3 &p, bool backfacing, Color &color) override
        {
            const auto UV = vUV * bar;

            // This shader uses single light only
            const auto light = ctx.light;

            const auto inNormal = vNormal * bar;
            const auto inTangent = vTangent * glm::vec4(bar, 1.f);
            const auto inPosition = vPosition * bar;
            const auto lightDir = glm::normalize(light->position - inPosition);
            const auto viewDir = glm::normalize(inPosition - ctx.camera.translation);
            const auto halfDir = glm::normalize(lightDir - viewDir);
            const auto inColor = vColor * glm::vec4(bar, 1.f);

            if (primitive->material) {
                const auto material = primitive->material;

                // double-sided
                if (!material->doubleSided && backfacing)
                    return true;

                if (material->emissiveTexture && material->emissiveTexture->image) {
                    const auto texture = material->emissiveTexture->image;
                    const auto wrappedUV = wrap(UV, material->emissiveTexture);
                    auto emissiveColor = texture->get(wrappedUV.x * texture->width, wrappedUV.y * texture->height);
                    emissiveColor.transparent(); // Remove alpha influence

                    Color newColor = (emissiveColor * material->emissiveFactor);
                    newColor = newColor + color;
                    color.copy(newColor);
                }

                if (material->baseColorTexture && material->baseColorTexture->image) {
                    const auto texture = material->baseColorTexture->image;
                    const auto wrappedUV = wrap(UV, material->baseColorTexture);
                    auto diffuse = texture->get(wrappedUV.x * texture->width, wrappedUV.y * texture->height);

                    if (material->alphaMode != AlphaMode::Opaque && texture->hasAlpha() && diffuse.A() == 0)
                        return true;

                    // alpha-cutoff
                    if (material->alphaMode == AlphaMode::Mask && texture->hasAlpha() && diffuse.Af() < material->alphaCutOff)
                        return true;

                    if (material->alphaMode == AlphaMode::Opaque) {
                        diffuse.opaque();
                    } else if (material->alphaMode == AlphaMode::Blend) {
                        const auto mix = framebuffer.get(p.x, p.y);
                        const auto blend = diffuse.Af();

                        // reset alpha before blending
                        diffuse.opaque();

                        auto mixColor = (mix * (1.f - blend));
                        mixColor =(diffuse * blend) + mixColor;
                        diffuse.copy(mixColor);
                    }

                    Color newColor = diffuse * material->baseColorFactor_sRGB;
                    newColor = color + newColor;
                    color.copy(newColor);
                } else {
                    // base color (gamma corrected)
                    Color newColor = color + material->baseColorFactor_sRGB;
                    color.copy(newColor);
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
            }

            // vertex color
            if (primitive->hasColor()) {
                Color newColor = color * glm::vec4(inColor, 1.f);
                color.copy(newColor);
            }

            return false;
        }
    };
}
