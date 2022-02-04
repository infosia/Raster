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

#include <ghc/filesystem.hpp>

namespace fs = ghc::filesystem;

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

    RenderOptions &options = scene.options;

    options.input = input;
    options.verbose = verbose;
    options.silent = silent;

    // Enable SSAA (Anti-Alias)
    options.ssaa = true;

    //
    // default settings (overridden by config JSON if specified)
    //

    // Output image size
    options.width = 1024;
    options.height = 1024;

    // Model rotation
    const fs::path inputPath = input;
    if (inputPath.extension() == ".vrm") {
        options.model.rotation = glm::quat(-0.259, 0, 0.966, 0);
    } else {
        options.model.rotation = glm::quat(0.966, 0, 0.259, 0);
    }

    // Light color (used by reflection)
    options.light.color = Color(206, 74, 0, 255);

    //
    // Config JSON
    //
    if (!config.empty()) {
        nlohmann::json configJson;
        if (json_parse(config, &configJson, options.silent)) {

        } else {
            return 1;
        }
    }

    if (!loadGLTF(input, scene)) {
        return 1;
    }

    //
    // Move camera position to center of the scene (x & y axis), and far enough (body height * 2.5) from the bounding box (z-axis)
    // This differs among models and needs to be adjusted depending on the model forms.
    //
    options.camera.translation = glm::vec3(scene.center.x, scene.center.y, -(scene.bbmax.y * 2.5f));
    //options.camera.translation = glm::vec3(0.f, 1.f, -4.5f);
    //options.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.f, .5f, 1.f) - glm::vec3(0.f, 0.f, 0.f)), glm::vec3(0.f, 1.f, 0.f));

    Image outputImage;
    if (!render(scene, outputImage)) {
        return 1;
    }

    if (!save(output, outputImage)) {
        return 1;
    }

    return 0;
}
