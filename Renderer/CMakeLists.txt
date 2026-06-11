cmake_minimum_required(VERSION 3.20)
project(Demo)

# if(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC") AND (CMAKE_BUILD_TYPE STREQUAL "Debug"))
#     add_compile_options(/fsanitize=address)
# endif()

# TODO per target
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BUILD_SHARED_LIBS OFF)

add_subdirectory(Lib/SDL)
add_subdirectory(Lib/volk)
add_subdirectory(Lib/VulkanMemoryAllocator)
add_subdirectory(Lib/SPIRV-Cross)
add_subdirectory(Lib/meshoptimizer)

set(KTX_FEATURE_TESTS OFF)
add_subdirectory(Lib/KTX-Software)

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE Lib/cgltf)

add_library(imgui
    Lib/imgui/imgui.cpp
    Lib/imgui/imgui_draw.cpp
    Lib/imgui/imgui_tables.cpp
    Lib/imgui/imgui_widgets.cpp
    Lib/imgui/imgui_demo.cpp
    Lib/imgui/backends/imgui_impl_vulkan.cpp
    Lib/imgui/backends/imgui_impl_sdl3.cpp
)
target_include_directories(imgui PUBLIC
    Lib/imgui
    Lib/imgui/backends
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui PUBLIC volk SDL3::SDL3)

set(SOURCES
    Source/Arena.cpp
    Source/Utils.cpp
    Source/Math/Utils.cpp
    Source/TimeMeter.cpp
    Source/Camera.cpp
    Source/Renderer/RHI/Vulkan/RHI.cpp
    Source/Renderer/RHI/Vulkan/TypeConvert.cpp
    Source/Renderer/Renderer.cpp
    Source/Renderer/Scene.cpp
    Source/Renderer/ImguiRenderer.cpp
    Source/Extern.cpp
    Source/Extern.c

    Source/Renderer/Shaders/SharedDef.hlsli
    Source/Renderer/Shaders/SharedConfig.hlsli
)

set(LIBS
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
    Source/Main.cpp
)

add_executable(${PROJECT_NAME}Test
    ${SOURCES}
    Source/Test/Test.cpp
)
target_include_directories(${PROJECT_NAME}Test PRIVATE Source)
target_link_libraries(${PROJECT_NAME}Test PRIVATE ${LIBS})

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(COMPILE_OPTIONS /W4)
    target_compile_options(${PROJECT_NAME} PRIVATE ${COMPILE_OPTIONS})
    target_compile_options(${PROJECT_NAME}Test PRIVATE ${COMPILE_OPTIONS})
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

    target_compile_options(${PROJECT_NAME}Test PRIVATE ${COMPILE_OPTIONS})
    target_compile_options(${PROJECT_NAME}Test PRIVATE ${SANITIZERS})
    target_link_options(${PROJECT_NAME}Test PRIVATE ${SANITIZERS})
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

set(SHADER_SOURCE_DIRECTORY ${CMAKE_SOURCE_DIR}/Source/Renderer/Shaders/)
set(SHADER_SOURCE_FILES
    ${SHADER_SOURCE_DIRECTORY}/CullEarly.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/CullLate.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/ShadowCull.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/DebugGradError.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/DebugGradError.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/Shadow.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/Shadow.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/Fullscreen.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/Fullscreen.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/Imgui.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/Imgui.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/DepthViewQuarterRes.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/SSAO.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/BlurSSAO.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/UpsampleSSAO.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/Fog.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/BlurFog.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/VisibilityRender.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/ForwardRender.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/ForwardRender.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/TaaResolve.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/VisibilityBuffer.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/VisibilityBuffer.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/DepthReduce.comp.hlsl
    ${SHADER_SOURCE_DIRECTORY}/DebugDrawRect.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/DebugDrawRect.frag.hlsl
    ${SHADER_SOURCE_DIRECTORY}/DebugDrawFillCmd.comp.hlsl
)
set(SHADER_INCLUDE_FILES
    ${SHADER_SOURCE_DIRECTORY}/Cull.comp.hlsli
    ${SHADER_SOURCE_DIRECTORY}/Common.hlsli
    ${SHADER_SOURCE_DIRECTORY}/DebugGradError.hlsli
    ${SHADER_SOURCE_DIRECTORY}/Shadow.hlsli
    ${SHADER_SOURCE_DIRECTORY}/ForwardRender.hlsli
    ${SHADER_SOURCE_DIRECTORY}/Fullscreen.hlsli
    ${SHADER_SOURCE_DIRECTORY}/Imgui.hlsli
    ${SHADER_SOURCE_DIRECTORY}/Math.hlsli
    ${SHADER_SOURCE_DIRECTORY}/PBR.hlsli
    ${SHADER_SOURCE_DIRECTORY}/SharedConfig.hlsli
    ${SHADER_SOURCE_DIRECTORY}/SharedDef.hlsli
    ${SHADER_SOURCE_DIRECTORY}/VisibilityBuffer.hlsli
    ${SHADER_SOURCE_DIRECTORY}/Sky.hlsli
    ${SHADER_SOURCE_DIRECTORY}/CalcShadow.hlsli
    ${SHADER_SOURCE_DIRECTORY}/DebugDrawRect.hlsli
    ${SHADER_SOURCE_DIRECTORY}/DebugDraw.hlsli
)

execute_process(COMMAND ${DXC_EXECUTABLE} --version)

foreach(shader ${SHADER_SOURCE_FILES})
    message(STATUS "Building shaders")
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
            -E Main
            -fspv-target-env=vulkan1.3
            -fspv-preserve-bindings
            #-fspv-extension=SPV_EXT_mesh_shader
            -fspv-extension=SPV_EXT_descriptor_indexing
            -Fo ${SPIRV} ${shader}
        DEPENDS ${shader} ${SHADER_INCLUDE_FILES}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(shader)

add_custom_target(Shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(${PROJECT_NAME} Shaders)
