cmake_minimum_required(VERSION 3.22)
project(fluxus_scripting_spikes LANGUAGES C CXX)

# Spikes to PROVE the IScriptHost hypothesis: can s7 Scheme and Lua+sol2 each
# embed, eval live code, call into a C++ engine, capture output, survive errors?
# Isolated from the main JUCE build. `make s7` / `make lua`.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- s7 Scheme (single-file interpreter) -----------------------------------
FetchContent_Declare(s7
    GIT_REPOSITORY https://github.com/radiganm/s7
    GIT_TAG        master
    SOURCE_SUBDIR  _no_auto_add)      # no CMakeLists -> populate only
FetchContent_MakeAvailable(s7)

add_library(s7 STATIC ${s7_SOURCE_DIR}/s7.c)
target_include_directories(s7 PUBLIC ${s7_SOURCE_DIR})
target_compile_definitions(s7 PRIVATE WITH_C_LOADER=0 WITH_SYSTEM_EXTRAS=0)
target_compile_options(s7 PRIVATE -w)   # s7.c is warning-noisy; silence it

add_executable(spike_s7 s7/main.cpp)
target_include_directories(spike_s7 PRIVATE common)
target_link_libraries(spike_s7 PRIVATE s7)

# --- Lua + sol2 -------------------------------------------------------------
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/walterschell/Lua
    GIT_TAG        master)
FetchContent_MakeAvailable(lua)          # provides target: lua_static

FetchContent_Declare(sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2
    GIT_TAG        main
    SOURCE_SUBDIR  _no_auto_add)         # header-only; skip its CMake, use includes
FetchContent_MakeAvailable(sol2)

add_executable(spike_lua lua-sol2/main.cpp)
target_include_directories(spike_lua PRIVATE common ${sol2_SOURCE_DIR}/include)
target_link_libraries(spike_lua PRIVATE lua_static)
