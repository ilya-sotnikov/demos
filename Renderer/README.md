# Renderer

GPU-driven renderer (Vulkan 1.4).

I've learned a lot from [niagara](https://github.com/zeux/niagara) (Vulkan renderer by Arseny Kapoulkine) and recommend checking out his streams and blog posts, it's a gold mine of knowledge about graphics, Vulkan and optimizations.

## TODO

So far there are some bugs in Slang (such as [this](https://github.com/shader-slang/slang/issues/7019) or [that](https://github.com/shader-slang/slang/issues/10123)).

- if mesh shader support doesn't improve in Slang, switch to HLSL (dxc)
- use mesh shaders to generate triangle ids for a visibility buffer
- meshlet frustum, backface, occlusion (Hi-Z) culling

Also, task shaders are very slow on RDNA 3 (and probably others), see the comment from the [Alan Wake 2 presentation by Erik Jansson](https://youtu.be/EtX7WnFhxtQ):

> One of the reasons we didn't use amplification shaders was due to them not performing great across all GPU hardware. Culling in compute shaders followed by ExecuteIndirect into mesh shaders seemed to perform optimally across all of the hardware platforms we were targeting. There was a great stream by Arseny Kapoulkine just two weeks ago about this actually: "niagara: Meshlets unchained" here on YouTube, where he shows why amplification/task shaders perform poorly on AMD RDNA 3 at least. He then implemented something similar to what we show in this presentation and got a massive performance boost, the frame time went from 9.26ms with amplification shader to just 1.44ms with compute shader and ExecuteIndirect approach!

[Link to the Arseny Kapoulkine's stream](https://www.youtube.com/live/zROUBE5pLuI).

## Preview

![](./Preview/Sponza.png)

## Features

- visibility buffer -> "forward" rendering
- async compute
- GPU frustum culling
- GPU 2-pass Hi-Z occlusion culling
- PBR (metallic workflow, no IBL)
- normal mapping
- HDR (HDR -> SDR)
- TAA
- SSAO
- stable cascaded shadow maps (CSM) with soft shadows (PCF with adaptive sampling)
- fog (participating media)
- very simplified atmospheric scattering
- bindless textures (descriptor indexing)
- indirect drawing
- SPIR-V reflection to manage push descriptors

## Frame overview (passes)

- (G) is graphics (vert/frag)
- (C) is compute (comp)
- branches represent async compute
- debug passes are not shown

```
CullEarly (C)
  |
VisBufEarly (G)
  |
DepthReduce (C)
  |
CullLate (C)
  |
VisBufLate (G)
  |
  +-------------------------+
  |                         |
CsmCull (C)               DepthViewQuarterRes (C)
  |                         |
CSM (G)                   SSAO (C)
  |                         |
  +--------------------+  BlurX (C)
  |                    |    |
Fog (C)                |  BlurY (C)
  |                    |    |
BlurX (C)              |  Upsample (C)
  |                    |    |
BlurY (C)              +----+
  |
Render (C)
  |
TaaResolve (C)
  |
Fullscreen (G)
```

## Build and run

Dependencies (included as git submodules):

- SDL
- volk
- VulkanMemoryAllocator
- SPIRV-Cross
- imgui
- cgltf
- KTX-Software
- meshoptimizer

You also need [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) (not bundled).

Building SDL may require [additional dependencies](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md#build-dependencies) depending on your OS.

```
git submodule init
git submodule update
cmake -B Build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build Build
```

To run, you need the [Intel Sponza](https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html) asset, download it and put it in the `Assets` directory.

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
- r -- reload shaders (after recompiling with CMake)
- . -- toggle fullscreen
- u -- toggle UI
- m -- toggle mouse
- c -- freeze camera to visualize culling
- 1, 2, ... -- switch between render modes
