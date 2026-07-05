cmake_minimum_required(VERSION 3.20)
project(demo)

# if(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC") AND (CMAKE_BUILD_TYPE STREQUAL "Debug"))
#     add_compile_options(/fsanitize=address)
# endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# TODO: disable some stuff.
add_subdirectory(lib/SDL)
add_subdirectory(lib/glm)

set(SOURCES
    src/camera.cpp
)

set(LIBS
    SDL3::SDL3
    glm::glm
)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    src/main.cpp
)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(COMPILE_OPTIONS /W4)
    target_compile_options(${PROJECT_NAME} PRIVATE ${COMPILE_OPTIONS})
else()
    set(COMPILE_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wno-missing-field-initializers
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

set(SHADER_SOURCE_DIRECTORY ${CMAKE_SOURCE_DIR}/src/shaders/)
set(SHADER_SOURCE_FILES
    ${SHADER_SOURCE_DIRECTORY}/grid.vert.hlsl
    ${SHADER_SOURCE_DIRECTORY}/grid.frag.hlsl
)
set(SHADER_INCLUDE_FILES
    ${SHADER_SOURCE_DIRECTORY}/common.hlsli
    ${SHADER_SOURCE_DIRECTORY}/shared_def.hlsli
    ${SHADER_SOURCE_DIRECTORY}/math.hlsli
    ${SHADER_SOURCE_DIRECTORY}/grid.hlsli
)

execute_process(COMMAND ${DXC_EXECUTABLE} --version)

foreach(shader ${SHADER_SOURCE_FILES})
    message(STATUS "Building shaders")
    get_filename_component(FILE_NAME ${shader} NAME)
    set(IR "${CMAKE_BINARY_DIR}/${FILE_NAME}.ir")
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
        OUTPUT ${IR}
        COMMAND ${DXC_EXECUTABLE}
            -spirv
            -O3 #-Zi -fspv-debug=vulkan-with-source
            -T ${SHADER_TYPE}_6_6
            -Zpc
            -E main
            -fspv-target-env=vulkan1.0
            -Fo ${IR} ${shader}
        DEPENDS ${shader} ${SHADER_INCLUDE_FILES}
    )
    list(APPEND IR_BINARY_FILES ${IR})
endforeach(shader)

add_custom_target(shaders DEPENDS ${IR_BINARY_FILES})
add_dependencies(${PROJECT_NAME} shaders)
