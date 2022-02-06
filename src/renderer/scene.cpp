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

#include "renderer/scene.h"
#include "pch.h"

namespace renderer
{
    void Image::fill(Color &color)
    {
        for (uint32_t x = 0; x < width; ++x) {
            for (uint32_t y = 0; y < height; ++y) {
                auto dst = data.data() + ((x + y * width) * format);
                // Stop filling when pixel is already painted
                if (format == Format::RGBA && dst[3] != 0)
                    continue;
                memcpy(dst, color.buffer(), format);
            }
        }
    }

    void Image::set(uint32_t x, uint32_t y, Color &c)
    {
        if (!data.size() || x < 0 || y < 0 || x >= width || y >= height)
            return;

        uint8_t *src = c.buffer();
        uint8_t *dst = data.data() + (x + y * width) * format;

        for (int i = format; i--; dst[i] = src[i])
            ;
    }

    Color Image::get(uint32_t x, uint32_t y) const
    {
        if (!data.size() || x < 0 || y < 0 || x >= width || y >= height)
            return Color();

        const uint8_t *src = data.data() + (x + y * width) * format;
        return Color(src, format);
    }
}
