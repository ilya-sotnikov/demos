# \[WIP\] Renderer

 GPU-driven renderer (Vulkan 1.3).

**WARNING for Windows users**: totally untested on windows and MSVC, expect issues.

## Preview

![](./Preview/Sponza.png)

## Features

- PBR (metallic workflow, no IBL)
- normal mapping
- hardware raytraced hard directional shadow
- HDR (HDR -> SDR)
- GPU frustum culling
- bindless (buffer device address, descriptor indexing)
- indirect drawing

## Build and run

Dependencies (included as git submodules):

- SDL
- volk
- VulkanMemoryAllocator
- imgui
- cgltf
- KTX-Software

You also need [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) (not bundled).

Building SDL may require [additional dependencies](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md#build-dependencies) depending on your OS.

```
git submodule init
git submodule update
cmake -B Build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build Build
```

To run, you need an [Intel Sponza]() asset, download it and put it in the `Assets` directory.

You also need to convert png textures to ktx2, using `ktx` utility from [KTX-Software](https://github.com/KhronosGroup/KTX-Software):

```
git clone https://github.com/KhronosGroup/KTX-Software.git
cd KTX-Software
cmake . -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DKTX_FEATURE_TOOLS=ON
cmake --build build
```

After building and installing (install somewhere in the `PATH`, put `libktx.so` there too):

```
cd Assets
./toktx_intel_sponza.sh
```

This will take a while.

## Controls

- WASD/ZX -- position control, Z/X are down/up
- Escape -- close
- . -- toggle fullscreen
- u -- toggle UI
- m -- toggle mouse

## Resources

- [Niagara renderer, Arseny Kapoulkine](https://github.com/zeux/niagara)
- [Physics and Math of Shading, Naty Hoffman](https://blog.selfshadow.com/publications/s2013-shading-course/hoffman/s2013_pbs_physics_math_notes.pdf)
- [Building a Simple Engine, Lighting implementation, The Khronos Group](https://github.com/KhronosGroup/Vulkan-Tutorial/blob/main/en/Building_a_Simple_Engine/Lighting_Materials/04_lighting_implementation.adoc)
- [Real Shading in Unreal Engine 4, Brian Karis](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf)
- [Frustum planes from the projection matrix, fgiesen](https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/)
- [Vulkan examples, Sascha Willems](https://github.com/SaschaWillems/Vulkan) (VulkanUIOverlay)
- [The Art Of Packing Data, redorav](https://www.elopezr.com/the-art-of-packing-data/)
- [A Fast and Robust Method for Avoiding Self-Intersection, Carsten Wächter and Nikolaus Binder](https://link.springer.com/content/pdf/10.1007/978-1-4842-4427-2_6)
- [Building an Orthonormal Basis, Revisited, Tom Duff, James Burgess, Per Christensen, Christophe Hery, Andrew Kensler, Max Liani, and Ryusuke Villemin](https://www.jcgt.org/published/0006/01/01/)
- [Reversed-Z in OpenGL, nlguillemot](https://nlguillemot.wordpress.com/2016/12/07/reversed-z-in-opengl/)
- [PCG Random Number Generation for C, Melissa O'Neill](https://github.com/imneme/pcg-c-basic/blob/master/pcg_basic.c)
- [KTX2 Texture Compression, Alejandro Juan Pérez](https://evergine.com/ktx2-texture-compression/)
