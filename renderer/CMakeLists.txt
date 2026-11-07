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
add_subdirectory(lib/meshoptimizer)

set(SPIRV_REFLECT_STATIC_LIB ON)
add_subdirectory(lib/SPIRV-Reflect)

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
    src/temp_allocator.hpp
    src/temp_allocator.cpp
    src/containers.hpp
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

    src/renderer/rhi/pool.hpp
    src/renderer/rhi/rhi.hpp
    src/renderer/rhi/vulkan/common.hpp
    src/renderer/rhi/vulkan/rhi.cpp
    src/renderer/imgui_renderer.hpp
    src/renderer/imgui_renderer.cpp
    src/renderer/renderer.hpp
    src/renderer/renderer.cpp
    src/renderer/scene.hpp
    src/renderer/scene.cpp
    src/renderer/uniform_buffer_manager.hpp
    src/renderer/uniform_buffer_manager.cpp

    src/renderer/shaders/shared_def.h.slang
    src/renderer/shaders/shared_config.h.slang
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
    spirv-reflect-static
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

set(SLANGC_EXECUTABLE slangc)

set(SHADER_SOURCE_DIRECTORY ${CMAKE_SOURCE_DIR}/src/renderer/shaders/)
set(SHADER_SOURCE_FILES
    ${SHADER_SOURCE_DIRECTORY}/cull_early.slang
    ${SHADER_SOURCE_DIRECTORY}/cull_late.slang
    ${SHADER_SOURCE_DIRECTORY}/shadow_cull.slang
    ${SHADER_SOURCE_DIRECTORY}/shadow.slang
    ${SHADER_SOURCE_DIRECTORY}/fullscreen.slang
    ${SHADER_SOURCE_DIRECTORY}/imgui.slang
    ${SHADER_SOURCE_DIRECTORY}/depth_view_quarter_res.slang
    ${SHADER_SOURCE_DIRECTORY}/ssao.slang
    ${SHADER_SOURCE_DIRECTORY}/blur_ssao.slang
    ${SHADER_SOURCE_DIRECTORY}/upsample_ssao.slang
    ${SHADER_SOURCE_DIRECTORY}/fog.slang
    ${SHADER_SOURCE_DIRECTORY}/blur_fog.slang
    ${SHADER_SOURCE_DIRECTORY}/visibility_render.slang
    ${SHADER_SOURCE_DIRECTORY}/taa_resolve.slang
    ${SHADER_SOURCE_DIRECTORY}/visibility_buffer.slang
    ${SHADER_SOURCE_DIRECTORY}/depth_reduce.slang
    ${SHADER_SOURCE_DIRECTORY}/debug_draw_rect.slang
    ${SHADER_SOURCE_DIRECTORY}/debug_draw_fill_cmd.slang
)
set(SHADER_INCLUDE_FILES
    ${SHADER_SOURCE_DIRECTORY}/cull.h.slang
    ${SHADER_SOURCE_DIRECTORY}/common.h.slang
    ${SHADER_SOURCE_DIRECTORY}/math.h.slang
    ${SHADER_SOURCE_DIRECTORY}/pbr.h.slang
    ${SHADER_SOURCE_DIRECTORY}/shared_config.h.slang
    ${SHADER_SOURCE_DIRECTORY}/shared_def.h.slang
    ${SHADER_SOURCE_DIRECTORY}/sky.h.slang
    ${SHADER_SOURCE_DIRECTORY}/calc_shadow.h.slang
    ${SHADER_SOURCE_DIRECTORY}/debug_draw.h.slang
)

execute_process(COMMAND ${DXC_EXECUTABLE} --version)

foreach(shader ${SHADER_SOURCE_FILES})
    message(STATUS "building shaders")
    get_filename_component(FILE_NAME ${shader} NAME)
    set(SPIRV "${CMAKE_BINARY_DIR}/${FILE_NAME}.spv")
    cmake_path(GET shader EXTENSION FILE_EXTENSION)

    message(STATUS ${shader})

    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${SLANGC_EXECUTABLE} ${shader}
            -Wall -Wextra
            -O3
            -target spirv
            -profile sm_6_6
            -matrix-layout-column-major
            -fvk-use-entrypoint-name
            -fvk-use-scalar-layout
            -unscoped-enum
            -o ${SPIRV}
        DEPENDS ${shader} ${SHADER_INCLUDE_FILES}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(shader)

add_custom_target(shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(${PROJECT_NAME} shaders)
