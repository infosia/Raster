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

#include <assert.h>

namespace renderer
{
    class Color;
    class Image
    {
    public:
        enum Format {
            GRAYSCALE = 1,
            RGB = 3,
            RGBA = 4
        };

        Image() = default;
        Image(uint32_t width, uint32_t height, Image::Format format)
            : width(width)
            , height(height)
            , format(format)
            , data(std::vector<std::uint8_t>(width * height * (uint32_t)format, 0))
        {
        }
        Image &operator=(const Image &) = delete;

        bool empty()
        {
            return data.empty();
        }

        void fill(Color &color);
        void clear()
        {
            data = std::vector<std::uint8_t>(width * height * (uint8_t)format, 0);
        }

        void reset(uint32_t w, uint32_t h, Image::Format f)
        {
            this->width = w;
            this->height = h;
            this->format = f;

            clear();
        }

        void init(uint32_t w, uint32_t h, Image::Format f, uint8_t *buffer)
        {
            this->width = w;
            this->height = h;
            this->format = f;
            this->data = std::vector<std::uint8_t>(buffer, buffer + (width * height * (uint8_t)format));
        }

        uint8_t *buffer()
        {
            return data.data();
        }

        void copy(Image &src)
        {
            assert(src.width == width && src.height == height && src.format == format);
            memcpy(data.data(), src.buffer(), (width * height * (uint8_t)format));
        }

        void set(uint32_t x, uint32_t y, Color &color);
        Color get(uint32_t x, uint32_t y) const;

        uint32_t width{ 0 };
        uint32_t height{ 0 };
        Format format{ RGBA };

    private:
        std::vector<std::uint8_t> data;
    };

    class Color
    {
    public:
        Color()
            : rgba{ 0, 0, 0, 255 }
        {
        }

        Color(const std::uint8_t *p, const Image::Format bpp)
        {
            for (int i = 0; i < bpp && i < Image::Format::RGBA; i++)
                rgba[i] = p[i];
        }

        Color(const uint8_t R, const uint8_t G, const uint8_t B, const uint8_t A)
            : rgba{ R, G, B, A }
        {
        }

        Color(const Color &src, const uint8_t A)
            : rgba{ src.R(), src.G(), src.B(), A }
        {
        }

        uint8_t *buffer()
        {
            return rgba;
        }

        void copy(Color &src)
        {
            memcpy(rgba, src.buffer(), 4);
        }

        uint8_t R() const
        {
            return rgba[0];
        }

        uint8_t G() const
        {
            return rgba[1];
        }

        uint8_t B() const
        {
            return rgba[2];
        }

        uint8_t A() const
        {
            return rgba[3];
        }

        float Af() const
        {
            return A() / 255.f;
        }

        float Rf() const
        {
            return R() / 255.f;
        }

        float Gf() const
        {
            return G() / 255.f;
        }

        float Bf() const
        {
            return B() / 255.f;
        }

        glm::vec3 toNormal() const
        {
            return glm::vec3(Rf() * 2.f - 1.f, Gf() * 2.f - 1.f, Bf() * 2.f - 1.f);
        }

        Color operator*(const float intensity) const
        {
            Color res = *this;
            const float clamped = std::fmax(0., std::fmin(intensity, 1.f));
            for (int i = 0; i < 4; i++)
                res.rgba[i] = rgba[i] * clamped;
            return res;
        }

        Color operator*(const glm::vec4 colors /* 0.f - 1.f */) const
        {
            Color res = *this;
            for (int i = 0; i < 4; i++)
                res.rgba[i] = rgba[i] * colors[i];
            return res;
        }

        Color operator+(const glm::vec4 colors /* 0.f - 1.f */) const
        {
            Color res = *this;
            for (int i = 0; i < 4; i++)
                res.rgba[i] = rgba[i] + (colors[i] * 255.f);
            return res;
        }

        Color operator+(Color &c) const
        {
            Color res = *this;
            const auto buffer = c.buffer();
            for (int i = 0; i < 4; i++)
                res.rgba[i] = std::min(255, rgba[i] + buffer[i]);
            return res;
        }

    private:
        uint8_t rgba[4]{ 0, 0, 0, 255 };
    };

    class Texture
    {
    public:
        Texture()
            : image(nullptr)
        {
        }
        Texture &operator=(const Texture &) = delete;

        Image *image;
        std::string name;
        std::string mimeType;
    };

    enum class AlphaMode {
        Opaque,
        Blend,
        Mask
    };

    struct Material
    {
        glm::vec4 baseColorFactor{};
        glm::vec4 baseColorFactor_sRGB{};
        Texture *baseColorTexture{ nullptr };
        Texture *normalTexture{ nullptr };
        AlphaMode alphaMode{ AlphaMode::Opaque };
        float alphaCutOff{ 0.f };
        float specularFactor{ 1.f };
        float metallicFactor{ 1.f };
        float roughnessFactor{ 0.f };
        bool doubleSided{ false };
        bool unlit{ false };
    };

    struct Vertex
    {
        glm::vec3 pos{};
        glm::vec3 normal{};
        glm::vec4 tangent{};
        glm::vec2 uv{};
        glm::vec4 jointIndices;
        glm::vec4 jointWeights;
    };

    class Primitive
    {
    public:
        Primitive() = default;
        Primitive &operator=(const Primitive &) = delete;

        Material *material{ nullptr };

        glm::vec3 vert(const int iface, const int ivert) const
        {
            return vertices[indices[iface * 3 + ivert]];
        }

        glm::vec2 uv(const int iface, const int ivert) const
        {
            return uvs[indices[iface * 3 + ivert]];
        }

        glm::vec3 normal(const int iface, const int ivert) const
        {
            return normals[indices[iface * 3 + ivert]];
        }

        glm::vec4 tangent(const int iface, const int ivert) const
        {
            return tangents[indices[iface * 3 + ivert]];
        }

        glm::vec4 color(const int iface, const int ivert) const
        {
            return colors[indices[iface * 3 + ivert]];
        }

        glm::vec4 joint(const int iface, const int ivert) const
        {
            return joints[indices[iface * 3 + ivert]];
        }

        glm::vec4 weight(const int iface, const int ivert) const
        {
            return weights[indices[iface * 3 + ivert]];
        }

        uint32_t numFaces() const
        {
            return indices.size() / 3;
        }

        bool hasNormal() const
        {
            return normals.size() > 0;
        }

        bool hasUV() const
        {
            return uvs.size() > 0;
        }

        bool hasColor() const
        {
            return colors.size() > 0;
        }

        bool hasTangent() const
        {
            return tangents.size() > 0;
        }

        bool hasJoints() const
        {
            return joints.size() > 0 && weights.size() > 0;
        }

        std::vector<glm::vec3> vertices;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec4> tangents;
        std::vector<glm::vec2> uvs;
        std::vector<glm::vec4> joints;
        std::vector<glm::vec4> weights;
        std::vector<glm::vec4> colors;

        std::vector<uint32_t> indices;

        glm::vec3 center;
        glm::vec3 bbmin;
        glm::vec3 bbmax;
    };

    class Mesh
    {
    public:
        Mesh() = default;
        Mesh &operator=(const Mesh &) = delete;

        std::string name;
        std::vector<Primitive> primitives;

        glm::vec3 center;
        glm::vec3 bbmin;
        glm::vec3 bbmax;
    };

    class Node;
    class Skin
    {
    public:
        Skin() = default;
        Skin &operator=(const Skin &) = delete;

        std::string name;
        std::vector<Node *> joints;
        std::vector<glm::mat4> inverseBindMatrices;
        std::vector<glm::mat4> jointMatrices;
    };

    class Node
    {
    public:
        Node() = default;
        Node &operator=(const Node &) = delete;

        Node *parent{ nullptr };
        Mesh *mesh{ nullptr };
        Skin *skin{ nullptr };
        std::vector<Node *> children;
        glm::mat4 matrix{};
        std::string name;
        bool visible{ true };

        // Used when skinning is disabled. This multiplies all matrices from parent.
        // Updated by UpdateJoints() in loader.cpp
        glm::mat4 bindMatrix{};
    };

    struct Model
    {
        glm::vec3 translation{ 0.f, 0.f, 0.f };
        glm::quat rotation{ 1.f, 0.f, 0.f, 0.f }; // w, x, y.z
        glm::vec3 scale{ 1.f, 1.f, 1.f };
    };

    struct Camera
    {
        float fov{ 30.f };
        float znear{ 0.1f };
        float zfar{ 100.f };

        glm::vec3 translation{ 0.f, 1.f, -2.f };
        glm::quat rotation{ 0.f, 0.f, 1.f, 0.f };
        glm::vec3 scale{ 1.f, 1.f, 1.f };
    };

    struct Light
    {
        Color color{ 255, 255, 255, 255 };
        glm::vec3 position{ 0.0f, 1.5f, 1.f };
    };

    struct RenderOptions
    {
        bool silent{ false };
        bool verbose{ false };
        bool ssaa{ false };

        std::string input;

        uint32_t width{ 1024 };
        uint32_t height{ 1024 };
        uint8_t ssaaKernelSize { 2 };

        Image::Format format{ Image::Format::RGB };

        Color background{ 255, 255, 255, 255 };

        Camera camera{};
        Light light{};
        Model model{};
    };

    class Scene
    {
    public:
        Scene() = default;
        Scene(const Scene &) = delete;
        Scene &operator=(const Scene &) = delete;

        glm::vec3 center;
        glm::vec3 bbmin;
        glm::vec3 bbmax;

        std::vector<Node *> children;

        std::vector<Skin> skins;
        std::vector<Image> images;
        std::vector<Texture> textures;
        std::vector<Material> materials;
        std::vector<Node> allNodes;
        std::vector<Mesh> meshes;

        RenderOptions options;
    };
}
