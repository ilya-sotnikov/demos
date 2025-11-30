cmake_minimum_required(VERSION 3.20)
project(demo)

# TODO per target
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)

add_subdirectory(lib/SDL3)
add_subdirectory(lib/volk)
add_subdirectory(lib/imgui)

set(SOURCES
    src/Math/Utils.cpp
    src/Renderer/Vulkan.cpp
    src/Renderer/Meshes.cpp
    src/Renderer/Renderer.cpp
    src/Renderer/ImGuiRenderer.cpp
    src/Physics/Geometry.cpp
    src/Physics/MassProperties.cpp
    src/Physics/Collide.cpp
    src/Physics/World.cpp
    src/Physics/GJK.cpp
    src/Utils.cpp
    src/Camera.cpp
    src/TimeMeter.cpp
    src/Arena.cpp
)

set(LIBS
    Vulkan::Vulkan
    SDL3::SDL3
    volk
    imgui
)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    src/Main.cpp
)

add_executable(${PROJECT_NAME}_test
    ${SOURCES}
    src/Test/Test.cpp
)
target_include_directories(${PROJECT_NAME}_test PRIVATE src)
target_link_libraries(${PROJECT_NAME}_test PRIVATE ${LIBS})

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    message(WARNING "Totally untested on windows and MSVC. Expect issues.")
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
        -fno-exceptions
        -fno-rtti
    )
    set(SANITIZERS -fsanitize=address,undefined )
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

set(SLANGC_EXECUTABLE slangc)

set(SHADER_SOURCE_DIRECTORY ${CMAKE_SOURCE_DIR}/src/Renderer/Shaders/)
set(SHADER_SOURCE_FILES
    ${SHADER_SOURCE_DIRECTORY}/Renderer.slang
    ${SHADER_SOURCE_DIRECTORY}/LineRenderer.slang
    ${SHADER_SOURCE_DIRECTORY}/ShadowPass.slang
    ${SHADER_SOURCE_DIRECTORY}/ImGuiRenderer.slang
)
set(SHADER_INCLUDE_FILES
    ${SHADER_SOURCE_DIRECTORY}/Common.slang
    ${SHADER_SOURCE_DIRECTORY}/SceneCommon.slang
)

foreach(shader ${SHADER_SOURCE_FILES})
    message(STATUS "Building shaders")
    get_filename_component(FILE_NAME ${shader} NAME)
    set(SPIRV "${CMAKE_BINARY_DIR}/${FILE_NAME}.spv")
    message(STATUS ${shader})
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${SLANGC_EXECUTABLE} ${shader} -O3 -target spirv -profile spirv_1_3 -matrix-layout-column-major -fvk-use-scalar-layout -emit-spirv-directly -o ${SPIRV}
        DEPENDS ${shader} ${SHADER_INCLUDE_FILES}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(shader)

add_custom_target(Shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(${PROJECT_NAME} Shaders)

target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBS})
