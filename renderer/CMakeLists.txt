cmake_minimum_required(VERSION 3.20)
project(InteractWallRenderer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Core Renderer
add_executable(InteractWallRenderer WIN32 
    src/core/main.cpp
    src/core/PluginLoader.cpp
    src/power/PowerManager.cpp
    src/quality/QualityManager.cpp
    src/ipc/IPCServer.cpp
)

target_link_libraries(InteractWallRenderer
    PRIVATE
    user32
    d3d11
    dxgi
    dwmapi
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
        COMMAND fxc.exe /T ${PROFILE} /E main /Fo "$<TARGET_FILE_DIR:CursorReveal>/CR_${SHADER_NAME}.cso" "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/cursor_reveal/Shaders/${SHADER}"
        COMMENT "Compiling ${SHADER}"
    )
endforeach()

# Depth Parallax Plugin
add_library(DepthParallax SHARED 
    src/plugins/depth_parallax/DepthParallaxPlugin.cpp
    src/plugins/cursor_reveal/WICLoader.cpp
)
target_link_libraries(DepthParallax PRIVATE d3d11 dxgi)
set_target_properties(DepthParallax PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release/plugins"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/plugins"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/plugins"
)

# Compile HLSL Shaders for DepthParallax
set(DP_SHADERS
    FullscreenVS.hlsl
    DepthParallaxPS.hlsl
)

foreach(SHADER ${DP_SHADERS})
    if (${SHADER} MATCHES ".*VS.*")
        set(PROFILE "vs_5_0")
    else()
        set(PROFILE "ps_5_0")
    endif()

    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    add_custom_command(
        TARGET DepthParallax POST_BUILD
        COMMAND fxc.exe /T ${PROFILE} /E main /Fo "$<TARGET_FILE_DIR:DepthParallax>/DP_${SHADER_NAME}.cso" "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/depth_parallax/Shaders/${SHADER}"
        COMMENT "Compiling DepthParallax ${SHADER}"
    )
endforeach()# Gravity Lens Plugin
add_library(GravityLens SHARED 
    src/plugins/gravity_lens/GravityLensPlugin.cpp
    src/plugins/cursor_reveal/WICLoader.cpp
)
target_link_libraries(GravityLens PRIVATE d3d11 dxgi)
set_target_properties(GravityLens PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release/plugins"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/plugins"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/plugins"
)

# Compile HLSL Shaders for GravityLens
set(GL_SHADERS
    FullscreenVS.hlsl
    SimPS.hlsl
    CompositePS.hlsl
)

foreach(SHADER ${GL_SHADERS})
    if (${SHADER} MATCHES ".*VS.*")
        set(PROFILE "vs_5_0")
    else()
        set(PROFILE "ps_5_0")
    endif()

    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    add_custom_command(
        TARGET GravityLens POST_BUILD
        COMMAND fxc.exe /T ${PROFILE} /E main /Fo "$<TARGET_FILE_DIR:GravityLens>/GL_${SHADER_NAME}.cso" "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/gravity_lens/Shaders/${SHADER}"
        COMMENT "Compiling GravityLens ${SHADER}"
    )
endforeach()

# Stone Press Plugin
add_library(StonePress SHARED 
    src/plugins/stone_press/StonePressPlugin.cpp
    src/plugins/cursor_reveal/WICLoader.cpp
)
target_link_libraries(StonePress PRIVATE d3d11 dxgi)
set_target_properties(StonePress PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release/plugins"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/plugins"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/plugins"
)

# Compile HLSL Shaders for StonePress
set(SP_SHADERS
    FullscreenVS.hlsl
    SimPS.hlsl
    CompositePS.hlsl
)

foreach(SHADER ${SP_SHADERS})
    if (${SHADER} MATCHES ".*VS.*")
        set(PROFILE "vs_5_0")
    else()
        set(PROFILE "ps_5_0")
    endif()

    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    add_custom_command(
        TARGET StonePress POST_BUILD
        COMMAND fxc.exe /T ${PROFILE} /E main /Fo "$<TARGET_FILE_DIR:StonePress>/SP_${SHADER_NAME}.cso" "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/stone_press/Shaders/${SHADER}"
        COMMENT "Compiling StonePress ${SHADER}"
    )
endforeach()

