cmake_minimum_required(VERSION 3.24)
project(bf3vr C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# DLL CRT (/MD): the OpenXR loader build forces it, so everything links against the VC++ redist.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

include(FetchContent)

# OpenXR loader, linked statically into the mod DLL.
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(openxr
    URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.38.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# MinHook for function hooking. Its own CMake is old, so build the sources directly.
FetchContent_Declare(minhook
    URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR do-not-add)

FetchContent_MakeAvailable(openxr minhook)

add_library(minhook STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde32.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

add_library(bf3vr SHARED
    src/dllmain.cpp
    src/d3d11_hook.cpp
    src/xr_runtime.cpp
    src/config.cpp
    src/camera.cpp
    src/finder.cpp
    src/aim.cpp
    src/input.cpp
    src/fov.cpp
    src/window.cpp
    src/skin.cpp
    src/depth3d.cpp
    src/god.cpp
    src/bodies.cpp
    src/ammo.cpp
    src/hands.cpp
    src/log.cpp)
target_compile_definitions(bf3vr PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(bf3vr PRIVATE openxr_loader minhook d3d11 dxgi d3dcompiler)

# Diagnostic: OpenXR instance creation outside the game.
add_executable(xrtest tools/xrtest.cpp)
target_compile_definitions(xrtest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(xrtest PRIVATE openxr_loader)

add_executable(injector injector/main.cpp)
target_compile_definitions(injector PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
