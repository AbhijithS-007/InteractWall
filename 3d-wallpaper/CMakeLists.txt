cmake_minimum_required(VERSION 3.20)
project(Scene3DViewer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(Scene3DViewer WIN32
    src/main.cpp
    src/GLBLoader.cpp
    src/SceneLoader.cpp
    src/TextureLoader.cpp
    src/IBLPreprocessor.cpp
)
add_executable(dbg_catcher dbg_catcher.cpp)

target_include_directories(Scene3DViewer PRIVATE libs)

target_link_libraries(Scene3DViewer PRIVATE
    user32
    d3d11
    dxgi
    d3dcompiler
)

# Compile HLSL Shaders
add_custom_command(
    TARGET Scene3DViewer POST_BUILD
    COMMAND fxc.exe /T vs_5_0 /E VSMain /Fo "$<TARGET_FILE_DIR:Scene3DViewer>/ModelVS.cso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ModelVS.hlsl"
    COMMENT "Compiling ModelVS.hlsl"
)

add_custom_command(
    TARGET Scene3DViewer POST_BUILD
    COMMAND fxc.exe /T ps_5_0 /E PSMain /Fo "$<TARGET_FILE_DIR:Scene3DViewer>/ModelPS.cso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ModelPS.hlsl"
    COMMENT "Compiling ModelPS.hlsl"
)

add_custom_command(
    TARGET Scene3DViewer POST_BUILD
    COMMAND fxc.exe /T vs_5_0 /E VSMain /Fo "$<TARGET_FILE_DIR:Scene3DViewer>/QuadVS.cso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/QuadVS.hlsl"
    COMMENT "Compiling QuadVS.hlsl"
)

add_custom_command(
    TARGET Scene3DViewer POST_BUILD
    COMMAND fxc.exe /T ps_5_0 /E PSMain /Fo "$<TARGET_FILE_DIR:Scene3DViewer>/QuadPS.cso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/QuadPS.hlsl"
    COMMENT "Compiling QuadPS.hlsl"
)

add_custom_command(
    TARGET Scene3DViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Scene3DViewer>/envmaps/cache"
    COMMENT "Creating envmaps/cache directory"
)

add_custom_command(
    TARGET Scene3DViewer POST_BUILD
    COMMAND fxc.exe /T cs_5_0 /E CSMain /Fo "$<TARGET_FILE_DIR:Scene3DViewer>/EquirectToCube.cso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/EquirectToCube.hlsl"
    COMMENT "Compiling EquirectToCube.hlsl"
)

add_custom_command(
    TARGET Scene3DViewer POST_BUILD
    COMMAND fxc.exe /T cs_5_0 /E CSMain /Fo "$<TARGET_FILE_DIR:Scene3DViewer>/IrradianceConvolve.cso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/IrradianceConvolve.hlsl"
    COMMENT "Compiling IrradianceConvolve.hlsl"
)

add_custom_command(
    TARGET Scene3DViewer POST_BUILD
    COMMAND fxc.exe /T cs_5_0 /E CSMain /Fo "$<TARGET_FILE_DIR:Scene3DViewer>/PrefilterSpecular.cso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/PrefilterSpecular.hlsl"
    COMMENT "Compiling PrefilterSpecular.hlsl"
)
