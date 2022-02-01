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

#include "renderer/render.h"
#include "pch.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

namespace renderer
{
    struct TRS
    {
        glm::vec3 translation{ 0.f, 0.f, 0.f };
        glm::quat rotation{ 1.f, 0.f, 0.f, 0.f };
        glm::vec3 scale{ 1.f, 1.f, 1.f };
    };

    static void generateSSAA(Image *dst, const Image *src, uint8_t kernelSize = 2)
    {
        const uint8_t sq = kernelSize * kernelSize;

        dst->reset(src->width / kernelSize, src->height / kernelSize, src->format);

        for (int x = 0; x < dst->width; ++x) {
            for (int y = 0; y < dst->height; ++y) {
                uint32_t xx = x * kernelSize;
                uint32_t yy = y * kernelSize;
                uint32_t R = 0, G = 0, B = 0;
                for (int i = 0; i < kernelSize; ++i) {
                    for (int j = 0; j < kernelSize; ++j) {
                        Color c = src->get(xx + i, yy + j);
                        R += c.R();
                        G += c.G();
                        B += c.B();
                    }
                }
                R /= (float)sq;
                G /= (float)sq;
                B /= (float)sq;
                Color color((uint8_t)R, (uint8_t)G, (uint8_t)B, 1);
                dst->set(x, y, color);
            }
        }
    }

    static glm::vec3 barycentric(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 p)
    {
        const auto v0 = b - a;
        const auto v1 = c - a;
        const float denom = 1.0f / (v0.x * v1.y - v1.x * v0.y);

        const auto v2 = p - a;
        const float v = (v2.x * v1.y - v1.x * v2.y) * denom;
        const float w = (v0.x * v2.y - v2.x * v0.y) * denom;
        const float u = 1.0f - v - w;
        return glm::vec3(u, v, w);
    }

    inline bool inBounds(int x, int y, int width, int height)
    {
        return (0 <= x && x < width) && (0 <= y && y < height);
    }

    inline bool isInTriangle(const glm::vec3 tri[3], int width, int height)
    {
        return inBounds(tri[0].x, tri[0].y, width, height)
            || inBounds(tri[1].x, tri[1].y, width, height)
            || inBounds(tri[2].x, tri[2].y, width, height);
    }

    inline glm::mat4 getModelMatrix(const TRS &transform)
    {
        return glm::translate(transform.translation) * glm::toMat4(transform.rotation) * glm::scale(transform.scale) * glm::mat4(1.f);
    }

    inline glm::mat4 getViewMatrix(const TRS &transform)
    {
        return glm::translate(-transform.translation) * glm::toMat4(transform.rotation) * glm::scale(transform.scale);
    }

    inline glm::mat4 getProjectionMatrix(uint32_t width, uint32_t height, float fov, float near, float far)
    {
        return glm::perspectiveFov(glm::radians(fov), (float)width, (float)height, near, far);
    }

    inline glm::quat lookAt(const glm::vec3 &from, const glm::vec3 &to, const glm::vec3 &up = glm::vec3(0, 1, 0))
    {
        return glm::quatLookAt(glm::normalize(to - from), up);
    }

    inline glm::uvec4 bb(const glm::vec3 tri[3], const int width, const int height)
    {
        int left = std::min(tri[0].x, std::min(tri[1].x, tri[2].x));
        int right = std::max(tri[0].x, std::max(tri[1].x, tri[2].x));
        int bottom = std::min(tri[0].y, std::min(tri[1].y, tri[2].y));
        int top = std::max(tri[0].y, std::max(tri[1].y, tri[2].y));

        left = std::max(left, 0);
        right = std::min(right, width - 1);
        bottom = std::max(bottom, 0);
        top = std::min(top, height - 1);

        return glm::uvec4{ left, bottom, right, top };
    }

    inline bool backfacing(const glm::vec3 tri[3])
    {
        const auto a = tri[0];
        const auto b = tri[1];
        const auto c = tri[2];
        return (a.x * b.y - a.y * b.x + b.x * c.y - b.y * c.x + c.x * a.y - c.y * a.x) > 0;
    }

    static glm::mat4 *getJointMatrices(const Node *node)
    {
        if (node->skin)
            return node->skin->jointMatrices.data();
        if (node->parent)
            return getJointMatrices(node->parent);
        return nullptr;
    }

    inline void drawBB(Shader *shader, ShaderContext &ctx, glm::uvec4 &bbox, const glm::vec3 tri[3], glm::vec3 depths)
    {
        for (auto y = bbox.y; y != bbox.w + 1; ++y) {
            for (auto x = bbox.x; x != bbox.z + 1; ++x) {
                glm::vec3 bcoords = barycentric(tri[0], tri[1], tri[2], glm::vec3{ x, y, 1.f });
                if (bcoords.x >= 0.0f && bcoords.y >= 0.0f && bcoords.z >= 0.0f) {
                    const float frag_depth = glm::dot(bcoords, depths);
                    if (inBounds(x, y, ctx.framebuffer->width, ctx.framebuffer->height) && frag_depth > ctx.zbuffer.at(x + y * ctx.framebuffer->width)) {
                        Color color(255, 255, 255, 255);
                        const auto discarded = shader->fragment(ctx, bcoords, backfacing(tri), color);
                        if (discarded)
                            continue;
                        ctx.zbuffer[x + y * ctx.framebuffer->width] = frag_depth;
                        ctx.framebuffer->set(x, y, color);
                    }
                }
            }
        }
    }

    static void draw(const RenderOptions &options, Shader *shader, ShaderContext &ctx, const Node *node)
    {
        if (node->skin)
            ctx.jointMatrices = getJointMatrices(node);

        if (node->mesh) {
            if (options.verbose && !node->name.empty())
                std::cout << "[INFO] Rendering " << node->name << std::endl;

            for (const auto primitive : node->mesh->primitives) {
                shader->primitive = &primitive;
                const uint32_t num_faces = primitive.numFaces();
                for (int i = 0; i < num_faces; i++) {
                    glm::vec3 tri[3] = {
                        shader->vertex(ctx, i, 0),
                        shader->vertex(ctx, i, 1),
                        shader->vertex(ctx, i, 2),
                    };
                    const glm::vec3 depths(tri[0].z, tri[1].z, tri[2].z);
                    if (isInTriangle(tri, ctx.framebuffer->width, ctx.framebuffer->height)) {
                        drawBB(shader, ctx, bb(tri, ctx.framebuffer->width, ctx.framebuffer->height), tri, depths);
                    }
                }
            }
        }

        for (const auto child : node->children) {
            draw(options, shader, ctx, child);
        }
    }

    bool save(std::string filename, Image &framebuffer)
    {
        return stbi_write_png(filename.c_str(), framebuffer.width, framebuffer.height, framebuffer.format, framebuffer.buffer(), 0) != 0;
    }

    bool render(Scene &scene, Image &framebuffer)
    {
        ShaderContext ctx = {};

        ctx.framebuffer = &framebuffer;

        const uint32_t width = scene.options.width;
        const uint32_t height = scene.options.height;

        TRS camera;
        camera.translation = glm::vec3(0.f, 0.8f, -3.5f);
        camera.rotation = lookAt(glm::vec3(0, 0, -1), glm::vec3(0, 0, 0));
        camera.scale = glm::vec3(1.f, 1.f, 1.f);

        TRS model;
        model.translation = glm::vec3(0.f, 0.f, 0.f);
        //model.rotation = glm::quat(-0.259, 0, 0.966, 0); /// VRM
        model.rotation = glm::quat(0.966, 0, 0.259, 0);
        model.scale = glm::vec3(1.f, 1.f, 1.f);

        ctx.projection = getProjectionMatrix(width, height, 30.f, 0.1f, 100.0f);
        ctx.view = getViewMatrix(camera);
        ctx.model = getModelMatrix(model);
        ctx.bgColor = Color(255, 255, 255, 255);
        ctx.cameraPos = camera.translation;

        ctx.zbuffer = std::vector<float>(width * height, std::numeric_limits<float>::min());
        ctx.framebuffer->reset(width, height, Image::Format::RGB);
        ctx.framebuffer->fill(ctx.bgColor);

        DefaultShader standard;
        OutlineShader outline;

        std::vector<Shader *> shaders{ &standard, &outline };

        for (auto node : scene.children) {
            for (auto shader : shaders) {
                draw(scene.options, shader, ctx, node);
            }
        }

        return true;
    }
}