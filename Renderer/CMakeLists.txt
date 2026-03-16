cmake_minimum_required(VERSION 3.20)
project(Demo)

# TODO per target
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BUILD_SHARED_LIBS OFF)

add_subdirectory(Lib/SDL)
add_subdirectory(Lib/volk)
add_subdirectory(Lib/VulkanMemoryAllocator)
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
    Source/Renderer/Vulkan.cpp
    Source/Renderer/Renderer.cpp
    Source/Renderer/Scene.cpp
    Source/Renderer/ImguiRenderer.cpp
    Source/Extern.cpp
    Source/Extern.c

    Source/Renderer/Shaders/SharedDef.slang
    Source/Renderer/Shaders/SharedConfig.slang
)

set(LIBS
    SDL3::SDL3
    volk
    cgltf
    GPUOpen::VulkanMemoryAllocator
    ktx
    imgui
    meshoptimizer
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

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    message(WARNING "Totally untested on windows and MSVC. Expect issues.")
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
        -fno-exceptions
        -fno-rtti
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

set(SLANGC_EXECUTABLE slangc)

set(SHADER_SOURCE_DIRECTORY ${CMAKE_SOURCE_DIR}/Source/Renderer/Shaders/)
set(SHADER_SOURCE_FILES
    ${SHADER_SOURCE_DIRECTORY}/DepthPrepass.slang
    ${SHADER_SOURCE_DIRECTORY}/Cull.slang
    ${SHADER_SOURCE_DIRECTORY}/Renderer.slang
    ${SHADER_SOURCE_DIRECTORY}/Shadow.slang
    ${SHADER_SOURCE_DIRECTORY}/ImguiRenderer.slang
    ${SHADER_SOURCE_DIRECTORY}/TaaResolve.slang
)
set(SHADER_INCLUDE_FILES
    ${SHADER_SOURCE_DIRECTORY}/SharedDef.slang
    ${SHADER_SOURCE_DIRECTORY}/SharedConfig.slang
    ${SHADER_SOURCE_DIRECTORY}/Common.slang
    ${SHADER_SOURCE_DIRECTORY}/Math.slang
    ${SHADER_SOURCE_DIRECTORY}/PBR.slang
)

foreach(shader ${SHADER_SOURCE_FILES})
    message(STATUS "Building shaders")
    get_filename_component(FILE_NAME ${shader} NAME)
    set(SPIRV "${CMAKE_BINARY_DIR}/${FILE_NAME}.spv")
    message(STATUS ${shader})
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${SLANGC_EXECUTABLE} ${shader} -O3 -target spirv -profile sm_6_6 -matrix-layout-column-major -fvk-use-entrypoint-name -fvk-use-scalar-layout -o ${SPIRV}
        DEPENDS ${shader} ${SHADER_INCLUDE_FILES}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(shader)

add_custom_target(Shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(${PROJECT_NAME} Shaders)
