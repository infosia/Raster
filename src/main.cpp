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

using namespace renderer;

#include "json_func.inl"
#include <CLI11.hpp>
#include <ghc/filesystem.hpp>

class Observer : public IObserver
{
public:
    Observer(bool silent, bool verbose)
        : IObserver()
        , silent(silent)
        , verbose(verbose)
    {
    }

    virtual void message(SubjectType subject, std::string message)
    {
        if (silent)
            return;

        if (!verbose && subject != SubjectType::Error && subject != SubjectType::Warning)
            return;

        std::cout << "[";
        switch (subject) {
        case SubjectType::Error:
            std::cout << "ERROR";
            break;
        case SubjectType::Warning:
            std::cout << "WARN";
            break;
        case SubjectType::Progress:
            std::cout << "INFO";
            break;
        default:
            std::cout << "INFO";
            break;
        }
        std::cout << "] ";

        std::cout << message << std::endl;
    }

    virtual void progress(float progress)
    {
        if (silent || !verbose)
            return;

        std::cout << "[INFO] Progress " << (progress * 100.f) << "%" << std::endl;
    }

private:
    bool verbose{ false };
    bool silent{ false };
};

static bool parseColor(const nlohmann::json &value, Color *color)
{
    const uint8_t SIZE = 4;

    if (value.size() < SIZE)
        return false;

    uint8_t c[SIZE];
    for (size_t i = 0; i < SIZE; ++i) {
        const auto v = value[i];
        if (!v.is_number())
            return false;

        c[i] = uint8_t(v.get<float>() * 255.f);
    }

    memcpy(color->buffer(), c, SIZE);

    return true;
}

static void parseRendering(const nlohmann::json &rendering, Scene &scene)
{
    if (!rendering.is_object())
        return;

    auto &options = scene.options;

    auto items = rendering.items();
    for (const auto &item : items) {
        auto key = item.key();
        auto value = item.value();

        if (key == "width" && value.is_number()) {
            options.width = value.get<uint32_t>();
        } else if (key == "height" && value.is_number()) {
            options.height = value.get<uint32_t>();
        } else if (key == "outline" && value.is_boolean()) {
            options.outline = value.get<bool>();
        } else if (key == "SSAA" && value.is_boolean()) {
            options.ssaa = value.get<bool>();
        } else if (key == "vignette" && value.is_boolean()) {
            options.vignette = value.get<bool>();
        } else if (key == "bgColor" && value.is_array()) {
            if (!parseColor(value, &options.background)) {
                Observable::notifyMessage(SubjectType::Error, "Unable to parse " + key);
            }
        } else if (key == "camera" && value.is_object()) {
            auto camera = value.items();
            for (const auto iter : camera) {
                auto cKey = iter.key();
                auto cValue = iter.value();
                if (cKey == "fov") {
                    json_get_float(cValue, &options.camera.fov);
                } else if (cKey == "znear") {
                    json_get_float(cValue, &options.camera.znear);
                } else if (cKey == "zfar") {
                    json_get_float(cValue, &options.camera.zfar);
                } else if (cKey == "translation") {
                    parseVec3(cValue, &options.camera.translation);
                } else if (cKey == "rotation") {
                    parseQuat(cValue, &options.camera.rotation);
                } else if (cKey == "projection") {
                    options.camera.mode = cValue.get<std::string>() == "orthographic" ? Projection::Orthographic : Projection::Perspective;
                }
            }
        } else if (key == "lights" && value.is_array()) {
            for (const auto iter : value) {
                if (iter.is_object()) {
                    Light light{};

                    auto props = iter.items();
                    for (const auto prop : props) {
                        auto lKey = prop.key();
                        if (lKey == "position") {
                            parseVec3(prop.value(), &light.position);
                        } else if (lKey == "color") {
                            parseColor(prop.value(), &light.color);
                        }
                    }

                    scene.lights.push_back(light);
                }
            }

            if (scene.lights.size() > 0) {
                scene.light = &scene.lights.at(0);
            }
        } else if (key == "model" && value.is_object()) {
            auto model = value.items();
            for (const auto iter : model) {
                auto mKey = iter.key();
                auto mValue = iter.value();
                if (mKey == "translation") {
                    parseVec3(mValue, &options.model.translation);
                } else if (mKey == "rotation") {
                    parseQuat(mValue, &options.model.rotation);
                }
            }
        }
    }
}

static void parseConfig(nlohmann::json &json, Scene &scene, std::string extension)
{
    std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

    // rendering settings
    const auto rendering = json["rendering"];

    if (!rendering.is_object()) {
        Observable::notifyMessage(SubjectType::Error, "Unable to parse 'rendering' configuration");
        return;
    }

    // Common rendering settings
    parseRendering(rendering["common"], scene);

    // Extension-specific settings
    auto items = rendering.items();
    for (const auto &item : items) {
        auto key = "." + item.key();
        auto value = item.value();
        std::transform(key.begin(), key.end(), key.begin(), ::toupper);
        if (key == extension) {
            parseRendering(value, scene);
        }
    }
}

int main(int argc, char **argv)
{
    CLI::App app{ "Raster: Software rasterizer for glTF models" };

    std::string input;
    app.add_option("-i,--input", input, "Input file name")->check(CLI::ExistingFile)->required();

    std::string output;
    app.add_option("-o,--output", output, "Output file name")->required();

    std::string config;
    app.add_option("-c,--config", config, "Config JSON file name");

    bool verbose = false;
    app.add_flag("-v,--verbose", verbose, "Verbose log");

    bool silent = false;
    app.add_flag("-s,--silient", silent, "Disable log");

    bool enableSSAA = false;
    app.add_flag("-a,--ssaa", enableSSAA, "Enable Anti-Alias (SSAA)");

    bool enableOutline = false;
    app.add_flag("-l,--outline", enableOutline, "Enable outline");

    bool enableVignette = false;
    app.add_flag("-g,--vignette", enableVignette, "Enable vignette effect");

    CLI11_PARSE(app, argc, argv);

    Observer observer(silent, verbose);
    Observable::subscribe(&observer);

    Scene scene;

    RenderOptions &options = scene.options;

    options.input = input;

    //
    // default settings (overridden by config JSON if specified)
    //

    // Enable SSAA (Anti-Alias)
    // This will likely double memory consumption
    options.ssaa = enableSSAA;

    // Enable outline
    // This will likely double rendering time
    options.outline = enableOutline;

    // Enable vignette effect (dark corners)
    options.vignette = enableVignette;

    // Output image size
    // Bigger number means longer rendering time & more memory consumption
    options.width = 512;
    options.height = 512;

    // Model rotation
    const ghc::filesystem::path inputPath = input;
    const auto extension = inputPath.extension().string();
    if (extension == ".vrm") {
        options.model.rotation = glm::quat(-0.259, 0, 0.966, 0);
    } else {
        options.model.rotation = glm::quat(0.966, 0, 0.259, 0);
    }

    //
    // Config JSON - See examples/raster-config.json for details.
    // Default settings will be overridden when specified in config JSON
    //
    if (!config.empty()) {
        nlohmann::json configJson;
        if (json_parse(config, &configJson)) {
            parseConfig(configJson, scene, extension);
        } else {
            Observable::notifyMessage(SubjectType::Error, "Unable to parse " + config);
            return 1;
        }
    }

    if (!loadGLTF(input, scene)) {
        return 1;
    }

    //
    // Node transformation
    //
    for (auto &node : scene.allNodes) {
        if (node.name == "J_Bip_L_UpperArm" || node.name == "mixamorig:LeftArm" || node.name == "LeftArm") {
            node.matrix *= glm::toMat4(glm::quat(0.924, 0, 0, 0.383));
        } else if (node.name == "J_Bip_R_UpperArm" || node.name == "mixamorig:RightArm" || node.name == "RightArm") {
            node.matrix *= glm::toMat4(glm::quat(-0.924, 0, 0, 0.383));
        }
    }

    // update joint matrix
    update(scene);

    // Morph weight
    for (auto &mesh : scene.meshes) {
        if (mesh.morphs.size() > 4) {
            auto morph = &mesh.morphs.at(2);
            morph->weight = 1.f;
        }
    }

    //
    // Move camera position to center of the scene (x & y axis), and far enough (body height * 2.5) from the bounding box (z axis)
    // This differs among models and needs to be adjusted depending on the model forms.
    //
    options.camera.translation = glm::vec3(0.f, 1.f, 4.f);
    //options.camera.translation = glm::vec3(0.f, 1.0f, 15.f);
    //options.camera.translation = glm::vec3(scene.center.x, scene.center.y, (scene.bbmax.y * 2.0f));
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
