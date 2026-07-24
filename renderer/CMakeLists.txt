cmake_minimum_required(VERSION 3.20)
project(InteractWallRenderer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Core Renderer
add_executable(InteractWallRenderer WIN32 
    src/core/main.cpp
    src/core/PluginLoader.cpp
)

target_link_libraries(InteractWallRenderer
    PRIVATE
    user32
    d3d11
    dxgi
)

# SolidColorTest Plugin (Temporarily disabled)
# add_library(SolidColorTest SHARED src/plugins/SolidColorTest/SolidColorTest.cpp)
# target_link_libraries(SolidColorTest PRIVATE d3d11 dxgi)
# set_target_properties(SolidColorTest PROPERTIES
#     PREFIX ""
#     LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
#     RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
# )

# Cursor Reveal Plugin
add_library(CursorReveal SHARED 
    src/plugins/cursor_reveal/CursorRevealPlugin.cpp
    src/plugins/cursor_reveal/WICLoader.cpp
)
target_link_libraries(CursorReveal PRIVATE d3d11 dxgi)
set_target_properties(CursorReveal PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release/plugins"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/plugins"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/plugins"
)

# Compile HLSL Shaders
set(SHADERS
    FullscreenVS.hlsl
    CompositePS.hlsl
    BrushPS.hlsl
    FadePS.hlsl
)

foreach(SHADER ${SHADERS})
    if (${SHADER} MATCHES ".*VS.*")
        set(PROFILE "vs_5_0")
    else()
        set(PROFILE "ps_5_0")
    endif()

    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    add_custom_command(
        TARGET CursorReveal POST_BUILD
        COMMAND fxc.exe /T ${PROFILE} /E main /Fo "$<TARGET_FILE_DIR:CursorReveal>/${SHADER_NAME}.cso" "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/cursor_reveal/Shaders/${SHADER}"
        COMMENT "Compiling ${SHADER}"
    )
endforeach()
