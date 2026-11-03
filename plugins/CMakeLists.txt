cmake_minimum_required(VERSION 3.22)
project(PluginsBuild LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(IMGUI_API=)
add_compile_definitions(CASIOEMU_PLUGIN_API=1)

set(IMGUI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp"
)

file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.cpp"
)

add_library(Plugins SHARED
    ${PLUGIN_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(Plugins PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
)

if(ANDROID)
    target_link_libraries(Plugins PRIVATE log GLESv2)
endif()