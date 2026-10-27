cmake_minimum_required(VERSION 3.25)
project(ForzaMotorsport_ExtraOptions VERSION 2.0.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "build with the mingw-w64 g++ (C:/mingw64/bin), e.g. cmake --preset default")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -fno-exceptions -fno-rtti)
# winmm.dll ships alone: no mingw runtime DLLs (tests/check_imports.cmake checks it)
add_link_options(-static -static-libgcc -static-libstdc++)

# ---- libraries ----
# Each library only sees its own folder plus what it links, so a game cannot include the other game's headers.
# Includes with "../" are rejected by tests/check_includes.cmake.

# core: generic code for every game (log, ini, patch engine, hooks, game interface)
add_library(core STATIC src/core/log.cpp)
target_include_directories(core PUBLIC src/core)

# one library per game, reached only through its game::Module
add_library(fm5 STATIC src/games/fm5/fm5.cpp)
target_include_directories(fm5 PUBLIC src/games/fm5)
target_link_libraries(fm5 PUBLIC core)

add_library(fm6 STATIC src/games/fm6/fm6.cpp)
target_include_directories(fm6 PUBLIC src/games/fm6)
target_link_libraries(fm6 PUBLIC core)

# list of the supported games and the lookup by PE header
add_library(games STATIC src/games/registry.cpp)
target_include_directories(games PUBLIC src/games)
target_link_libraries(games PUBLIC fm5 fm6)

# ---- winmm.dll ----
add_library(winmm SHARED src/main.cpp src/proxy/forward.cpp src/proxy/winmm.def)
target_include_directories(winmm PRIVATE src/proxy)
target_link_libraries(winmm PRIVATE games)
target_link_options(winmm PRIVATE -Wl,--enable-stdcall-fixup)
set_target_properties(winmm PROPERTIES PREFIX "" OUTPUT_NAME winmm)

# ---- tests ----
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
