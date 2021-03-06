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
#include "observer.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <chrono>
#include <map>
#include <sstream>

namespace renderer
{
    static void generateVignette(Image *dst, const Color bgColor)
    {
        const auto R = bgColor.R();
        const auto B = bgColor.B();
        const auto G = bgColor.G();

        const auto width = dst->width;
        const auto height = dst->height;

        for (uint32_t x = 0; x < width; ++x) {
            for (uint32_t y = 0; y < height; ++y) {

                const auto srcColor = dst->get(x, y);

                // Stop filling when pixel is already painted
                if (srcColor.A() != 0)
                    continue;

                const float distance = sqrtf(powf((x - width / 2.f), 2) + powf((y - height / 2.f), 2));
                const float factor = (height - distance) / height;

                Color newColor = Color(R * factor, G * factor, B * factor, 255);
                dst->set(x, y, newColor);
            }
        }
    }

    static void generateSSAA(Image *dst, const Image *src, uint8_t kernelSize = 2)
    {
        const uint8_t sq = kernelSize * kernelSize;

        dst->reset(src->width / kernelSize, src->height / kernelSize, src->format);

        for (uint32_t x = 0; x < dst->width; ++x) {
            for (uint32_t y = 0; y < dst->height; ++y) {
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
                Color newColor = Color((uint8_t)R, (uint8_t)G, (uint8_t)B, 255);
                dst->set(x, y, newColor);
            }
        }
    }

    static glm::vec3 barycentric(glm::vec3 &a, glm::vec3 &b, glm::vec3 &c, glm::vec3 &p)
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

    inline glm::mat4 getModelMatrix(const Model &model)
    {
        return glm::translate(model.translation) * glm::toMat4(model.rotation) * glm::scale(model.scale) * glm::mat4(1.f);
    }

    inline glm::mat4 getViewMatrix(const Camera &camera)
    {
        glm::vec3 translation = -camera.translation;
        translation.z = -translation.z; // Z+
        return glm::translate(translation) * glm::toMat4(camera.rotation) * glm::scale(camera.scale);
    }

    inline glm::mat4 getOrthoMatrix(float width, float height, float near, float far)
    {
        const float aspect = width / height;
        return glm::ortho(aspect, -aspect, 1.f, -1.f, near, far);
    }

    inline glm::mat4 getPerspectiveMatrix(float width, float height, float fov, float near, float far)
    {
        return glm::perspectiveFov(glm::radians(fov), width, height, near, far);
    }

    inline glm::mat4 getProjectionMatrix(uint32_t width, uint32_t height, Camera camera)
    {
        if (camera.mode == Projection::Orthographic) {
            return getOrthoMatrix((float)width, (float)height, camera.znear, camera.zfar);
        }
        return getPerspectiveMatrix((float)width, (float)height, camera.fov, camera.znear, camera.zfar);
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
        const auto &a = tri[0];
        const auto &b = tri[1];
        const auto &c = tri[2];
        return (a.x * b.y - a.y * b.x + b.x * c.y - b.y * c.x + c.x * a.y - c.y * a.x) > 0;
    }

    inline void drawBB(Shader *shader, ShaderContext &ctx, const glm::uvec4 &bbox, glm::vec3 tri[3], glm::vec3 depths)
    {
        const auto width = shader->framebuffer.width;
        const auto height = shader->framebuffer.height;

        for (auto y = bbox.y; y != bbox.w + 1; ++y) {
            for (auto x = bbox.x; x != bbox.z + 1; ++x) {
                auto p = glm::vec3(x, y, 1.f);
                glm::vec3 bcoords = barycentric(tri[0], tri[1], tri[2], p);
                if (bcoords.x >= 0.0f && bcoords.y >= 0.0f && bcoords.z >= 0.0f) {
                    const float frag_depth = glm::dot(bcoords, depths);
                    if (inBounds(x, y, width, height) && frag_depth > shader->zbuffer.at(x + y * width)) {
                        Color color(0, 0, 0, 0);
                        const auto discarded = shader->fragment(ctx, bcoords, p, backfacing(tri), color);
                        if (discarded)
                            continue;
                        shader->zbuffer[x + y * width] = frag_depth;
                        shader->framebuffer.set(x, y, color);
                    }
                }
            }
        }
    }

    struct RenderOp
    {
        RenderOptions *options;
        Shader *shader;
        ShaderContext *ctx;
        Node *node;
        Primitive *primitive;
    };

    static void queue(RenderOptions &options, Shader *shader, ShaderContext &ctx, Node *node, std::map<uint32_t, std::vector<RenderOp>> *renderQueue)
    {
        if (node->mesh) {
            for (auto &primitive : node->mesh->primitives) {
                RenderOp op{ &options, shader, &ctx, node, &primitive };
                if (primitive.material && primitive.material->vrm0) {
                    const auto vrm0 = primitive.material->vrm0;
                    const auto iter = renderQueue->find(vrm0->renderQueue);
                    if (iter == renderQueue->end()) {
                        std::vector<RenderOp> queue{ op };
                        renderQueue->emplace(vrm0->renderQueue, queue);
                    } else {
                        iter->second.push_back(op);
                    }
                } else {
                    const auto iter = renderQueue->find(0);
                    if (iter == renderQueue->end()) {
                        std::vector<RenderOp> queue{ op };
                        renderQueue->emplace(0, queue);
                    } else {
                        iter->second.push_back(op);
                    }
                }
            }
        }

        for (const auto child : node->children) {
            queue(options, shader, ctx, child, renderQueue);
        }
    }

    static void draw(RenderOp *op)
    {
        const auto node = op->node;
        const auto shader = op->shader;

        auto &ctx = *op->ctx;

        if (node->skin)
            shader->jointMatrices = node->skin->jointMatrices.data();
        else
            shader->bindMatrix = &node->bindMatrix;

        if (node->mesh) {
            shader->morphs = &node->mesh->morphs;
            shader->primitive = op->primitive;
            const uint32_t num_faces = op->primitive->numFaces();
            for (uint32_t i = 0; i < num_faces; i++) {
                glm::vec3 tri[3] = {
                    shader->vertex(ctx, i, 0),
                    shader->vertex(ctx, i, 1),
                    shader->vertex(ctx, i, 2),
                };
                const glm::vec3 depths(tri[0].z, tri[1].z, tri[2].z);
                if (isInTriangle(tri, shader->framebuffer.width, shader->framebuffer.height)) {
                    drawBB(shader, ctx, bb(tri, shader->framebuffer.width, shader->framebuffer.height), tri, depths);
                }
            }
        }
    }

    static void draw(const RenderOptions &options, Shader *shader, ShaderContext &ctx, Node *node)
    {
        if (node->skin)
            shader->jointMatrices = node->skin->jointMatrices.data();
        else
            shader->bindMatrix = &node->bindMatrix;

        if (node->mesh) {
            Observable::notifyMessage(SubjectType::Info, "Rendering " + node->name);

            shader->morphs = &node->mesh->morphs;

            for (const auto &primitive : node->mesh->primitives) {
                shader->primitive = &primitive;
                const uint32_t num_faces = primitive.numFaces();
                for (uint32_t i = 0; i < num_faces; i++) {
                    glm::vec3 tri[3] = {
                        shader->vertex(ctx, i, 0),
                        shader->vertex(ctx, i, 1),
                        shader->vertex(ctx, i, 2),
                    };
                    const glm::vec3 depths(tri[0].z, tri[1].z, tri[2].z);
                    if (isInTriangle(tri, shader->framebuffer.width, shader->framebuffer.height)) {
                        drawBB(shader, ctx, bb(tri, shader->framebuffer.width, shader->framebuffer.height), tri, depths);
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
        Observable::notifyProgress(0.0f);

        const auto start = std::chrono::system_clock::now();

        ShaderContext ctx;

        RenderOptions &options = scene.options;

        const uint32_t width = options.width * (options.ssaa ? options.ssaaKernelSize : 1);
        const uint32_t height = options.height * (options.ssaa ? options.ssaaKernelSize : 1);

        Camera &camera = options.camera;

        ctx.projection = getProjectionMatrix(width, height, camera);
        ctx.view = getViewMatrix(camera);
        ctx.model = getModelMatrix(options.model);
        ctx.bgColor = options.background;
        ctx.camera = camera;
        ctx.light = scene.light;

        auto zbuffer = std::vector<float>(width * height, std::numeric_limits<float>::min());

        framebuffer.reset(width, height, options.format);

        DefaultShader standard;
        OutlineShader outline;

        std::vector<Shader *> shaders{ &standard };

        if (options.outline) {
            shaders.push_back(&outline);
        }
        Observable::notifyProgress(0.1f);

        std::vector<std::map<uint32_t, std::vector<RenderOp>>> renderQueues;
        renderQueues.resize(shaders.size());

#pragma omp parallel for
        for (int i = 0; i < shaders.size(); ++i) {
            const auto shader = shaders.at(i);

            shader->zbuffer = std::vector<float>(width * height, std::numeric_limits<float>::min());
            shader->framebuffer.reset(width, height, options.format);

            for (auto node : scene.children) {
                queue(options, shader, ctx, node, &renderQueues.at(i));
            }
        }
        Observable::notifyProgress(0.2f);

#pragma omp parallel for
        for (int i = 0; i < shaders.size(); ++i) {
            const auto shader = shaders.at(i);
            auto &renderQueue = renderQueues.at(i);
            for (auto &queue : renderQueue) {
                std::stringstream ss;
                ss << "RenderQueue " << queue.first;
                Observable::notifyMessage(SubjectType::Info, ss.str());
                auto &ops = queue.second;

                // z sort for alpha blending
                std::sort(ops.begin(), ops.end(), [](RenderOp a, RenderOp b) {
                    return a.primitive->center.z < b.primitive->center.z;
                });

                for (auto &op : ops) {
                    draw(&op);
                }
            }
        }

        Observable::notifyProgress(0.7f);

        auto dst = framebuffer.buffer();
        const auto stride = options.format;
        for (uint32_t i = 0; i < zbuffer.size(); ++i) {
            for (auto shader : shaders) {
                const auto src = shader->framebuffer.buffer();
                const auto current = i * stride;
                const auto dstDepth = zbuffer.at(i);
                const auto srcDepth = shader->zbuffer.at(i);
                if (dstDepth < srcDepth) {
                    zbuffer[i] = srcDepth;

                    // mix color when alpha is color is set (Used by outline for now)
                    if (stride == Image::Format::RGBA && (src + current)[3] != 255) {
                        const auto srcColor = Color(src + current, stride);
                        const auto dstColor = Color(dst + current, stride);
                        const auto alpha = srcColor.Af();
                        Color dstAColor = dstColor * (1.f - alpha);
                        Color srcAColor = srcColor * alpha;
                        auto mixed = dstAColor + srcAColor;
                        memcpy(dst + current, mixed.buffer(), stride);
                    } else {
                        memcpy(dst + current, src + current, stride);
                    }
                }
            }
        }
        Observable::notifyProgress(0.8f);

        if (options.vignette) {
            Observable::notifyMessage(SubjectType::Info, "Generating Vignette");
            generateVignette(&framebuffer, ctx.bgColor);
        } else {
            // Fill the background anywhere pixel alpha equals zero
            framebuffer.fill(ctx.bgColor);
        }

        Observable::notifyProgress(0.9f);

        if (options.ssaa) {
            Observable::notifyMessage(SubjectType::Info, "Generating SSAA");

            Image tmp(options.width, options.height, options.format);
            generateSSAA(&tmp, &framebuffer, options.ssaaKernelSize);
            framebuffer.reset(options.width, options.height, options.format);
            framebuffer.copy(tmp);
        }

        const auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
        Observable::notifyMessage(SubjectType::Info, "Rendering done in " + std::to_string(msec) + " msec");
        Observable::notifyProgress(1.f);

        return true;
    }
}
