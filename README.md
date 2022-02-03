# :diamond_shape_with_a_dot_inside: Raster

A software renderer which does not require any GPU resources. Works without Vulkan, OpenGL nor Metal, written in C++11. Currently only supports [glTF](https://www.khronos.org/gltf/) binary (`.glb`) and [VRM](https://vrm.dev/en/) (`.vrm`) models as input, and PNG image format for output.

**[[[Work In Progress]]]**

## Features

- [x] glTF (.glb) as input
- [x] VRM (.vrm) as input
- [x] PNG output
- [x] Shader in C++
- [x] Backface culling
- [x] Vertex skinning
- [x] Tangent space normal mapping
- [x] Orbital camera control
- [x] Inverted hull outline
- [x] Blinn-Phong reflection
- [ ] Vertex colors
- [ ] Morph targets
- [ ] MToon shading
- [ ] SSAA (anti-alias)

## Usage (Standalone renderer)

```
> Raster --input INPUT.glb --output OUTPUT.png
```

### Options

* `--verbose`: Verbose log output
* `--input`: Input file name
* `--output` Output file name
* `--config`: Configuration file (JSON)

## Usage (As a library)

Raster is built to be integrated with applications of your choice as an external library. Raster has easy to use API that enables you to view 3D models as an image, without any GPU dependencies such as Vulkan and OpenGL.

```c++
#include "raster.h"

using namespace renderer; // namespace for Raster

int main(int argc, char **argv)
{
    Scene scene;

    scene.options.input = "model.glb"; // input 3D model
    scene.options.verbose = false; // verbose log output
    scene.options.silient = false; // silent log output

    // Read from glTF
    if (!loadGLTF(input, scene)) {
        return 1;
    }

    // Render
    Image outputImage;
    if (!render(scene, outputImage)) {
        return 1;
    }

    // Save to image
    if (!save("model.png", outputImage)) {
        return 1;
    }

    return 0;
}
```

## License

* Available to anybody free of charge, under the terms of MIT License (see LICENSE).

## Building

You need [CMake](https://cmake.org/download/) in order to build this. On Windows you also need Visual Studio with C++ environment installed.
Once you have CMake installed run cmake like this:


```
> mkdir build; cd build
> cmake ..
```

