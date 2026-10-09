cmake_minimum_required(VERSION 3.24)
project(CompositorWindows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets)

add_library(compositor_core STATIC
    src/core/BlendMode.cpp
    src/core/Types.cpp
    src/document/Adjustments.cpp
    src/document/Hierarchy.cpp
    src/document/Manifest.cpp
    src/document/ManifestValidation.cpp
    src/io/ManifestCodec.cpp
    src/io/SwiftJson.cpp
    src/render/BlendOps.cpp
)
target_include_directories(compositor_core PUBLIC src third_party)
target_compile_options(compositor_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_library(compositor_io STATIC
    src/io/ImageAssets.cpp
    src/io/ProjectStore.cpp
)
target_link_libraries(compositor_io PUBLIC compositor_core Qt6::Gui)
target_compile_options(compositor_io PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_library(compositor_render STATIC
    src/render/Raster.cpp
    src/render/Resample.cpp
    src/render/LayerDraw.cpp
    src/render/MaskClip.cpp
    src/render/StackRenderer.cpp
    src/render/DocumentRenderer.cpp
)
target_link_libraries(compositor_render PUBLIC compositor_io Qt6::Gui)
target_compile_options(compositor_render PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_library(compositor_ui STATIC
    src/ui/DocumentHistory.cpp
    src/ui/EditorSession.cpp
    src/ui/BrushStroke.cpp
    src/ui/CanvasWidget.cpp
    src/ui/LayersPanel.cpp
    src/ui/MainWindow.cpp
)
target_link_libraries(compositor_ui PUBLIC compositor_render Qt6::Widgets)
target_compile_options(compositor_ui PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(Compositor WIN32 app/main.cpp)
target_link_libraries(Compositor PRIVATE compositor_ui)

enable_testing()
add_subdirectory(tests)
