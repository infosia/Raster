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

static bool json_get_float(nlohmann::json value, float *number)
{
    if (value.is_number()) {
        *number = value.get<float>();
        return true;
    }
    return false;
}

static bool json_parse(std::string json_file, nlohmann::json *json)
{
    std::ifstream f(json_file, std::ios::in);
    if (f.fail()) {
        Observable::notifyMessage(SubjectType::Error, "Unable to find " + json_file);
        return false;
    }

    try {
        f >> *json;
    } catch (nlohmann::json::parse_error &e) {
        Observable::notifyMessage(SubjectType::Error, "Unable to parse " + json_file + "\n\t" + e.what());
        return false;
    }

    return true;
}

static bool parseVec3(const nlohmann::json &value, glm::vec3 *vec)
{
    const uint8_t SIZE = 3;

    if (value.size() < SIZE)
        return false;

    glm::vec3 out;
    for (size_t i = 0; i < SIZE; ++i) {
        const auto v = value[i];
        if (!v.is_number())
            return false;
        out[i] = v.get<float>();
    }
    *vec = out;

    return true;
}

static bool parseQuat(const nlohmann::json &value, glm::quat *quat)
{
    const uint8_t SIZE = 4;

    if (value.size() < SIZE)
        return false;

    const auto x = value[0];
    const auto y = value[1];
    const auto z = value[2];
    const auto w = value[3];

    if (x.is_number() && y.is_number() && z.is_number() && w.is_number()) {
        *quat = glm::quat(w.get<float>(), x.get<float>(), y.get<float>(), z.get<float>());
        return true;
    }

    return false;
}
