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
#include "raster.h"

#include <CLI11.hpp>
#include <json.hpp>

#include "json_func.inl"

using namespace renderer;

int main(int argc, char **argv)
{
    CLI::App app{ "Raster: Software rasterizer for glTF models" };

    std::string input;
    app.add_option("-i,--input", input, "Input file name")->check(CLI::ExistingFile)->required();

    std::string output;
    app.add_option("-o,--output", output, "Output file name")->required();

    std::string config;
    app.add_option("-c,--config", output, "Config JSON file name");

    bool verbose = false;
    app.add_flag("-v,--verbose", verbose, "Verbose log");

    bool silent = false;
    app.add_flag("-s,--silient", silent, "Disable log");

    CLI11_PARSE(app, argc, argv);

    Scene scene;

    scene.options.input = input;
    scene.options.verbose = verbose;
    scene.options.silent = silent;

    if (!config.empty()) {
        nlohmann::json configJson;
        if (json_parse(config, &configJson, scene.options.silent)) {

        } else {
            return 1;
        }
    }

    if (!loadGLTF(input, scene)) {
        return 1;
    }

    Image outputImage;
    if (!render(scene, outputImage)) {
        return 1;
    }

    if (!save(output, outputImage)) {
        return 1;
    }

    return 0;
}
