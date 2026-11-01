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
    src/handpose.cpp
    src/views.cpp
    src/perfinfo.cpp
    src/effects.cpp
    src/reload.cpp
    src/sounds.cpp
    src/pad.cpp
    src/gunshape.cpp
    src/collide.cpp
    src/pouch.cpp
    src/pistolmag.cpp
    src/pistolslide.cpp
    src/overlay.cpp
    src/shaders.cpp
    src/log.cpp)
target_compile_definitions(bf3vr PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(bf3vr PRIVATE openxr_loader minhook d3d11 dxgi d3dcompiler xaudio2 ole32)

# Diagnostic: OpenXR instance creation outside the game.
add_executable(xrtest tools/xrtest.cpp)
target_compile_definitions(xrtest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(xrtest PRIVATE openxr_loader)

# The left glove's finger poses drawn from a mesh dump, to shape them without the game.
add_executable(handview tools/handview.cpp src/handpose.cpp)
target_compile_definitions(handview PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# The left glove fitted onto the rifle's charging handle from the mesh dumps, for hands.cpp.
add_executable(leverfit tools/leverfit.cpp src/handpose.cpp)
target_compile_definitions(leverfit PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# The left glove fitted round the rifle's handguard from the mesh dumps, for hands.cpp.
add_executable(gripfit tools/gripfit.cpp src/handpose.cpp)
target_compile_definitions(gripfit PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# The rifle's collision shape (a distance field) for the mod, from the gun's mesh dump.
add_executable(gunshape tools/gunshape.cpp src/handpose.cpp)
target_compile_definitions(gunshape PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# The free left glove kept out of the rifle (src/collide.cpp) on scripted hand paths at 72 Hz, from
# the rifle's collision shape: penetration, passing through, jerks, jitter, slaps felt, time a frame.
add_executable(collidetest tools/collidetest.cpp src/collide.cpp src/gunshape.cpp src/handpose.cpp src/log.cpp)
target_compile_definitions(collidetest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# A magazine from the pouch at the pelvis pushed into the rifle's well by the left hand (src/pouch.cpp)
# on scripted frames at 72 Hz: the zone, the squeeze, the body's heading, the funnel, the seat, letting go.
add_executable(magtest tools/magtest.cpp src/pouch.cpp)
target_compile_definitions(magtest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(magtest PRIVATE OpenXR::headers)  # pouch.h's XrPosef: the headers only, no loader

# The train pistol's slide worked by the left hand (src/pistolslide.cpp, as hands.cpp runs it) on scripted
# frames at 72 Hz: the three grips and which the hand takes, the grab and its reach (the first headset
# test's misses), a press waiting for the hand, the hand gliding on and off, fresh presses, the ride back
# and its stops, the spring home, the slide stop, the trigger's rack, the hand or the pistol lost, the
# hand still coming in as it takes hold (from every side), tracking jumps, why a press missed.
add_executable(pistolslidetest tools/pistolslidetest.cpp src/pistolslide.cpp)
target_compile_definitions(pistolslidetest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# The train pistol's magazine by the left hand (src/pistolmag.cpp, as hands.cpp runs it) on scripted frames at
# 72 Hz: the take at the pelvis, the carry and the flat base, what's caught under the grip and what isn't, the
# ride up the raked line, the snap home and how near the palm came, parked and pushed on, let go and fallen, the
# hand or the pistol lost, the game's own reload, the carries, broken inputs. The design's reloadtest, ported.
add_executable(pistolmagtest tools/pistolmagtest.cpp src/pistolmag.cpp)
target_compile_definitions(pistolmagtest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(pistolmagtest PRIVATE OpenXR::headers)  # pistolmag.h's XrVector3f: the headers only

# The forearm placed from its hand (src/armik.h, header only): the bind pose, rigid moves, the game's
# own rifle hold from the mesh dumps, the wrist's readings and range, the twist sweep, degenerate
# poses, the time a solve takes, and the stand-in's test hooks.
add_executable(armtest tools/armtest.cpp)
target_compile_definitions(armtest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# The left hand steering the gun while it holds the handguard (src/steer.h, header only) on scripted
# frames at 72 Hz: both hands swinging it, the left hand pivoting it, wandering off the handguard, the
# hands coming together, letting go, one-frame glitches, the line back while the last steer eases out,
# degenerate inputs, the time an update takes.
add_executable(steertest tools/steertest.cpp)
target_compile_definitions(steertest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# The view cameras' turn to the head (src/viewturn.h, header only, as camera.cpp TurnToHead runs it): the player's
# camera turned bit for bit as before (synthetic and recorded turns), a scripted camera (a cutscene's or a ride's, its
# rows' w lanes filled) turned about its own yaw with the horizon level and its w lanes kept, the camera check, the
# eye shift, mirrored rows, a forward near the vertical, a camera offered again, the w lanes' finding, the time; how
# far a 1-degree turn of a scripted camera turns its level yaw (the review's finding), the drive of Comrades as at
# 833abbd, the yaw going on from frame to frame (spins, pans, cuts, tumbling, a frame's cameras alike); the body's own
# views turned where they're uploaded (skin.cpp's body lock): the math, which uploads are turned, a normal picture's
# left bit for bit, the FOV fix after it as before, the time; and the review's findings: the body at the picture's
# field of view (the knees on the scene as the head turns), a mirrored camera's twin left alone, a frame's build (a
# normal picture drawn behind left bit for bit).
add_executable(viewturntest tools/viewturntest.cpp)
target_compile_definitions(viewturntest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Where the game's threads spend their time, sampled from outside the game.
add_executable(sampler tools/sampler.cpp)
target_compile_definitions(sampler PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# The player's look angles found from outside the game (pairs matching the game camera).
add_executable(anglescan tools/anglescan.cpp)
target_compile_definitions(anglescan PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Records what the PC plays and cuts clips from it: the game's own reload sounds for the mod.
add_executable(capture tools/capture.cpp)
target_compile_definitions(capture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(capture PRIVATE ole32)

# The mod's reload sounds played outside the game.
add_executable(playtest tools/playtest.cpp src/sounds.cpp)
target_compile_definitions(playtest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(playtest PRIVATE xaudio2 ole32)

add_executable(injector injector/main.cpp)
target_compile_definitions(injector PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
