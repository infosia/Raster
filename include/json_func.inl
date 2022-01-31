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
#include <vector>
#include <string>

static bool json_get_bool(nlohmann::json object, std::string name)
{
    if (object.is_object()) {
        auto& value = object[name];
        if (value.is_boolean()) {
            return value.get<bool>();
        }
    }
    return false;
}

static std::vector<std::string> json_get_string_items(std::string name, nlohmann::json obj)
{
    const auto& items_obj = obj[name];
    std::vector<std::string> items;
    if (items_obj.is_array()) {
        for (const auto& item : items_obj) {
            items.push_back(item.get<std::string>());
        }
    }
    return items;
}

static bool json_parse(std::string json_file, nlohmann::json *json, bool silent)
{
    std::ifstream f(json_file, std::ios::in);
    if (f.fail()) {
        return false;
    }

    try {
        f >> *json;
    } catch (nlohmann::json::parse_error &e) {
        if (!silent) {
            std::cout << "[ERROR] Unable to parse " << json_file << std::endl;
            std::cout << "        " << e.what() << std::endl;
        }
        return false;
    }

    return true;
}
