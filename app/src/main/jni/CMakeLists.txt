cmake_minimum_required(VERSION 3.18.1)
project("builder")

add_library(builder SHARED
    main.cpp
    ImGui/imgui.cpp
    ImGui/imgui_draw.cpp
    ImGui/imgui_widgets.cpp
    ImGui/imgui_tables.cpp
    ImGui/imgui_demo.cpp
    ImGui/backends/imgui_impl_android.cpp
    ImGui/backends/imgui_impl_opengl3.cpp)

find_library(log-lib log)

target_include_directories(builder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGui
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/backends)

target_link_libraries(builder android EGL GLESv2 ${log-lib})
