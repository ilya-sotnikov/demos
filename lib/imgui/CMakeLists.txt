add_library(imgui
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui_demo.cpp
    backends/imgui_impl_vulkan.cpp
    backends/imgui_impl_sdl3.cpp
)
target_include_directories(imgui PUBLIC
    .
    backends
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui PUBLIC volk SDL3::SDL3)
