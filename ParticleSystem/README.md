# Particle system

3D GPU compute particle system (Vulkan 1.3).

**WARNING** for Windows users: totally untested on windows and MSVC, expect issues.

## Preview

![](./Preview/Particles.gif)

## Build and run

Dependencies (bundled):
- volk
- SDL3

You also need [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) (not bundled).

Building SDL3 may require [additional dependencies](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md#build-dependencies) depending on your OS.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build
cd build
./demo
```

## Controls

- WASD/ZX -- position control, Z/X are down/up, you can choose a body from the menu to control it instead
- Escape -- close
- . -- toggle fullscreen
- r -- restart simulation

## Resources

- [GPU-based particle simulation, turanszkij](https://wickedengine.net/2017/11/gpu-based-particle-simulation/0) (compute shaders are heavily based on this)
- [GPU Particle Force Field Tutorial, MirzaBeig](https://www.reddit.com/r/Unity3D/comments/a9nc5o/gpu_particle_force_field_tutorial_link_to_full/) (demo idea)
