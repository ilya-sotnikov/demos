cmake_minimum_required(VERSION 3.20)
project(demo)

# if(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC") AND (CMAKE_BUILD_TYPE STREQUAL "Debug"))
#     add_compile_options(/fsanitize=address)
# endif()

# TODO per target
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BUILD_SHARED_LIBS OFF)

add_subdirectory(lib/EASTL)
add_subdirectory(lib/SDL)
add_subdirectory(lib/volk)
add_subdirectory(lib/VulkanMemoryAllocator)
add_subdirectory(lib/SPIRV-Cross)
add_subdirectory(lib/meshoptimizer)

set(KTX_FEATURE_TESTS OFF)
add_subdirectory(lib/KTX-Software)

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE lib/cgltf)

add_library(imgui
    lib/imgui/imgui.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
    lib/imgui/imgui_demo.cpp
    lib/imgui/backends/imgui_impl_vulkan.cpp
    lib/imgui/backends/imgui_impl_sdl3.cpp
)
target_include_directories(imgui PUBLIC
    lib/imgui
    lib/imgui/backends
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui PUBLIC volk SDL3::SDL3)

set(SOURCES
    src/common.hpp
    src/arena.hpp
    src/arena.cpp
    src/camera.hpp
    src/camera.cpp
    src/extern.cpp
    src/extern.c
    src/pack_utils.hpp
    src/slice_arg.hpp
    src/time_meter.hpp
    src/time_meter.cpp
    src/utils.hpp
    src/utils.cpp

    src/math/hash.hpp
    src/math/math.hpp
    src/math/math_common.hpp
    src/math/mat2.hpp
    src/math/mat3.hpp
    src/math/mat4.hpp
    src/math/quat.hpp
    src/math/types.hpp
    src/math/utils.hpp
    src/math/utils.cpp
    src/math/vec2.hpp
    src/math/vec3.hpp
    src/math/vec4.hpp

    src/renderer/rhi/access_table.hpp
    src/renderer/rhi/format_table.hpp
    src/renderer/rhi/pool.hpp
    src/renderer/rhi/rhi.hpp
    src/renderer/rhi/stage_table.hpp
    src/renderer/rhi/vulkan/common.hpp
    src/renderer/rhi/vulkan/rhi.cpp
    src/renderer/rhi/vulkan/type_convert.hpp
    src/renderer/rhi/vulkan/type_convert.cpp
    src/renderer/imgui_renderer.hpp
    src/renderer/imgui_renderer.cpp
    src/renderer/renderer.hpp
    src/renderer/renderer.cpp
    src/renderer/scene.hpp
    src/renderer/scene.cpp
    src/renderer/uniform_buffer_manager.hpp
    src/renderer/uniform_buffer_manager.cpp

    src/renderer/shaders/shared_def.hlsli
    src/renderer/shaders/shared_config.hlsli
)

set(LIBS
    EASTL
    SDL3::SDL3
    volk
    cgltf
    GPUOpen::VulkanMemoryAllocator
    ktx
    imgui
    meshoptimizer
    spirv-cross-cpp
)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    src/main.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(${PROJECT_NAME}_test
    ${SOURCES}
    src/test/test.cpp
)
target_include_directories(${PROJECT_NAME}_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME}_test PRIVATE ${LIBS})

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(COMPILE_OPTIONS /W4)
    target_compile_options(${PROJECT_NAME} PRIVATE ${COMPILE_OPTIONS})
    target_compile_options(${PROJECT_NAME}_test PRIVATE ${COMPILE_OPTIONS})
else()
    set(COMPILE_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wno-missing-designated-field-initializers
    )

    set(SANITIZERS -fsanitize=address,undefined)

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(SANITIZERS ${SANITIZERS} -shared-libasan)
    endif()

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${PROJECT_NAME} PRIVATE -Og)
        target_compile_options(${PROJECT_NAME} PRIVATE ${SANITIZERS})
        target_link_options(${PROJECT_NAME} PRIVATE ${SANITIZERS})
    endif()

    target_compile_options(${PROJECT_NAME} PRIVATE ${COMPILE_OPTIONS})

    target_compile_options(${PROJECT_NAME}_test PRIVATE ${COMPILE_OPTIONS})
    target_compile_options(${PROJECT_NAME}_test PRIVATE ${SANITIZERS})
    target_link_options(${PROJECT_NAME}_test PRIVATE ${SANITIZERS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBS})

if(NOT DEFINED DXC_EXECUTABLE)
    if(UNIX)
        if(DEFINED ENV{VULKAN_SDK})
            set(DXC_EXECUTABLE "$ENV{VULKAN_SDK}/bin/dxc")
        else()
            set(DXC_EXECUTABLE "dxc")
        endif()
    else()
        set(DXC_EXECUTABLE "$ENV{VULKAN_SDK}/Bin/dxc.exe")
    endif()
endif()

set(SHADER_SOURCE_DIRECTORY ${CMAKE_SOURCE_DIR}/src/renderer/shaders/)
set(SHADER_SOURCE_FILES
    ${SHADER_SOURCE_DIRECTORY}/cull_early.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/cull_late.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/shadow_cull.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/debug_grad_error.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/debug_grad_error.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/shadow.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/shadow.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/fullscreen.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/fullscreen.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/imgui.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/imgui.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/depth_view_quarter_res.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/ssao.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/blur_ssao.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/upsample_ssao.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/fog.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/blur_fog.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/visibility_render.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/taa_resolve.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/visibility_buffer.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/visibility_buffer.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/depth_reduce.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/debug_draw_rect.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/debug_draw_rect.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/debug_draw_fill_cmd.comp.hlsl
)
set(SHADER_INCLUDE_FILES
    ${SHADER_SOURCE_DIRECTORY}/cull.comp.hlsli
    ${SHADER_SOURCE_DIRECTORY}/common.hlsli
    ${SHADER_SOURCE_DIRECTORY}/debug_grad_error.hlsli
    ${SHADER_SOURCE_DIRECTORY}/shadow.hlsli
    ${SHADER_SOURCE_DIRECTORY}/fullscreen.hlsli
    ${SHADER_SOURCE_DIRECTORY}/imgui.hlsli
    ${SHADER_SOURCE_DIRECTORY}/math.hlsli
    ${SHADER_SOURCE_DIRECTORY}/pbr.hlsli
    ${SHADER_SOURCE_DIRECTORY}/shared_config.hlsli
    ${SHADER_SOURCE_DIRECTORY}/shared_def.hlsli
    ${SHADER_SOURCE_DIRECTORY}/visibility_buffer.hlsli
    ${SHADER_SOURCE_DIRECTORY}/sky.hlsli
    ${SHADER_SOURCE_DIRECTORY}/calc_shadow.hlsli
    ${SHADER_SOURCE_DIRECTORY}/debug_draw_rect.hlsli
    ${SHADER_SOURCE_DIRECTORY}/debug_draw.hlsli
)

execute_process(COMMAND ${DXC_EXECUTABLE} --version)

foreach(shader ${SHADER_SOURCE_FILES})
    message(STATUS "building shaders")
    get_filename_component(FILE_NAME ${shader} NAME)
    set(SPIRV "${CMAKE_BINARY_DIR}/${FILE_NAME}.spv")
    cmake_path(GET shader EXTENSION FILE_EXTENSION)

    if("${FILE_EXTENSION}" STREQUAL ".vert.hlsl")
        set(SHADER_TYPE vs)
    elseif("${FILE_EXTENSION}" STREQUAL ".frag.hlsl")
        set(SHADER_TYPE ps)
    elseif("${FILE_EXTENSION}" STREQUAL ".comp.hlsl")
        set(SHADER_TYPE cs)
    elseif("${FILE_EXTENSION}" STREQUAL ".mesh.hlsl")
        set(SHADER_TYPE ms)
    elseif("${FILE_EXTENSION}" STREQUAL ".task.hlsl")
        set(SHADER_TYPE as)
    else()
        message(FATAL_ERROR "unknown shader extension ${shader}")
    endif()

    message(STATUS ${shader})

    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${DXC_EXECUTABLE}
            -spirv
            -O3 #-Zi -fspv-debug=vulkan-with-source
            -T ${SHADER_TYPE}_6_6
            -fvk-use-scalar-layout
            -enable-16bit-types
            -Zpc
            -E main
            -fspv-target-env=vulkan1.3
            -fspv-preserve-bindings
            #-fspv-extension=SPV_EXT_mesh_shader
            -fspv-extension=SPV_EXT_descriptor_indexing
            -Fo ${SPIRV} ${shader}
        DEPENDS ${shader} ${SHADER_INCLUDE_FILES}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(shader)

add_custom_target(shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(${PROJECT_NAME} shaders)
