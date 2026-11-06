cmake_minimum_required(VERSION 3.22)
project(PluginsBuild LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED PLUGIN_NAME)
    message(FATAL_ERROR "PLUGIN_NAME is required")
endif()

add_compile_definitions(IMGUI_API=)
add_compile_definitions(CASIOEMU_PLUGIN_API=1)

set(IMGUI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp"
)

if(NOT ANDROID) 
    set(WIND_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/wind/pch.cpp")
else()
    set(WIND_SOURCES "")
endif()

add_library(${PLUGIN_NAME} SHARED
    "plugins/${PLUGIN_NAME}.cpp"
    ${IMGUI_SOURCES}
    ${WIND_SOURCES}
)

target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/wind"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
)

if(ANDROID)
    target_link_libraries(${PLUGIN_NAME} PRIVATE log GLESv2)
else()
    target_compile_definitions(${PLUGIN_NAME} PRIVATE _USRDLL CASIOEMU_PLUGIN_API)

    if(WIN32)
        target_compile_definitions(${PLUGIN_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    
    target_link_libraries(${PLUGIN_NAME} PRIVATE
        imm32
        winmm
        version
    )
    
    target_link_options(${PLUGIN_NAME} PRIVATE "-static-libgcc" "-static-libstdc++" "-static" "-Wl,--unresolved-symbols=ignore-all")
endif()