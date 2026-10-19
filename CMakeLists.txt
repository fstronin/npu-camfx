cmake_minimum_required(VERSION 3.20)
project(npu-camfx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0 gstreamer-video-1.0 gstreamer-base-1.0 gstreamer-app-1.0)
pkg_check_modules(OPENCV REQUIRED IMPORTED_TARGET opencv4)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gio-unix-2.0)

# OpenVINO ships as a Python wheel here; its CMake package still defines the
# openvino::runtime imported target with the correct library path.
if(NOT OpenVINO_DIR)
    set(OpenVINO_DIR "$ENV{HOME}/npu-venv/lib/python3.12/site-packages/openvino/cmake"
        CACHE PATH "OpenVINO CMake package directory")
endif()
find_package(OpenVINO REQUIRED)
find_package(Threads REQUIRED)

add_library(npucam_core STATIC
    src/application/effect_processor.cpp
    src/openvino/openvino_segmenter.cpp
    src/openvino/omz_face_analyzer.cpp
    src/imaging/cv_effect_renderer.cpp
    src/preview/preview_generator.cpp)
target_include_directories(npucam_core PUBLIC include)
target_link_libraries(npucam_core PUBLIC
    PkgConfig::OPENCV
    openvino::runtime
    Threads::Threads)

add_library(gstnpueffects MODULE src/gst/gstnpueffects.cpp)
target_link_libraries(gstnpueffects PRIVATE npucam_core PkgConfig::GST)

add_executable(npueffectsd src/daemon/main.cpp)
target_link_libraries(npueffectsd PRIVATE npucam_core PkgConfig::GST PkgConfig::GIO)

add_executable(npucam-preview src/tools/preview_cli.cpp)
target_link_libraries(npucam-preview PRIVATE npucam_core)

enable_testing()
add_executable(npucam_tests tests/test_effect_processor.cpp)
target_link_libraries(npucam_tests PRIVATE npucam_core)
add_test(NAME core COMMAND npucam_tests)

add_executable(npucam-bench tests/bench_pipeline.cpp)
target_link_libraries(npucam-bench PRIVATE PkgConfig::GST)

install(TARGETS gstnpueffects LIBRARY DESTINATION share/gstreamer-1.0/plugins)
install(TARGETS npueffectsd npucam-preview RUNTIME DESTINATION bin)
