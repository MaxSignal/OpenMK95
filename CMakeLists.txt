cmake_minimum_required(VERSION 3.16)
project(OpenMK95 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(OPENMK95_VENDOR_SDL "Download and build SDL2 statically instead of using an installed SDL2" OFF)
option(OPENMK95_BUILD_TESTS "Build the unit tests" ON)

if(MSVC)
  # Sources contain UTF-8 string literals (the runtime's Japanese system messages).
  add_compile_options(/utf-8 /W3 /permissive- /Zc:__cplusplus)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-parameter>)
endif()

if(MINGW)
  # Self-contained executables: no libgcc/libstdc++/winpthread DLLs next to them.
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

find_package(Threads REQUIRED)

# ---- SDL2 ----
set(OPENMK95_SDL_TARGET "")
set(OPENMK95_SDLMAIN_TARGET "")
if(OPENMK95_VENDOR_SDL)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-2.30.9 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL2)
  set(OPENMK95_SDL_TARGET SDL2::SDL2-static)
  if(TARGET SDL2::SDL2main)
    set(OPENMK95_SDLMAIN_TARGET SDL2::SDL2main)
  endif()
else()
  find_package(SDL2 CONFIG QUIET)
  if(NOT SDL2_FOUND)
    find_package(SDL2 MODULE QUIET)
  endif()
  if(TARGET SDL2::SDL2)
    set(OPENMK95_SDL_TARGET SDL2::SDL2)
  elseif(TARGET SDL2::SDL2-static)
    set(OPENMK95_SDL_TARGET SDL2::SDL2-static)
  elseif(SDL2_LIBRARIES)
    # Old SDL2 installations without imported targets.
    add_library(openmk95_sdl2 INTERFACE)
    target_include_directories(openmk95_sdl2 INTERFACE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(openmk95_sdl2 INTERFACE ${SDL2_LIBRARIES})
    set(OPENMK95_SDL_TARGET openmk95_sdl2)
  endif()
  if(TARGET SDL2::SDL2main)
    set(OPENMK95_SDLMAIN_TARGET SDL2::SDL2main)
  endif()
endif()
if(NOT OPENMK95_SDL_TARGET)
  message(FATAL_ERROR "SDL2 not found. Install the SDL2 development files or configure with -DOPENMK95_VENDOR_SDL=ON.")
endif()

# ---- libraries ----
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB MOVIE_SRC CONFIGURE_DEPENDS src/movie/*.cpp)
file(GLOB SOUND_SRC CONFIGURE_DEPENDS src/sound/*.cpp)
file(GLOB PLATFORM_SRC CONFIGURE_DEPENDS src/platform/*.cpp)
file(GLOB ENGINE_SRC CONFIGURE_DEPENDS src/engine/*.cpp)

# core: Shift_JIS text, file lookup, bitmaps, font, random numbers (no SDL)
add_library(core STATIC ${CORE_SRC})
target_include_directories(core PUBLIC src)

# movie: AVI demuxer and video decoders (no SDL)
add_library(movie STATIC ${MOVIE_SRC})
target_link_libraries(movie PUBLIC core)

# sound: wave mixer, SMF sequencer, MIDI synthesizers (TinySoundFont, built-in, winmm)
add_library(sound STATIC ${SOUND_SRC})
target_include_directories(sound PRIVATE third_party)
target_link_libraries(sound PUBLIC core ${OPENMK95_SDL_TARGET} Threads::Threads)
if(WIN32)
  target_link_libraries(sound PUBLIC winmm)
endif()

# platform: SDL2 window, input, clock, test scripts
add_library(platform STATIC ${PLATFORM_SRC})
target_link_libraries(platform PUBLIC core ${OPENMK95_SDL_TARGET})

# engine: the game runtime itself
add_library(engine STATIC ${ENGINE_SRC})
target_link_libraries(engine PUBLIC core movie sound platform)

# ---- executables ----
add_executable(openmk95 src/app/main.cpp)
target_link_libraries(openmk95 engine ${OPENMK95_SDLMAIN_TARGET} ${OPENMK95_SDL_TARGET})
if(WIN32)
  set_target_properties(openmk95 PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(OPENMK95_BUILD_TESTS)
  file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(unit_tests ${TEST_SRC})
  target_include_directories(unit_tests PRIVATE src)
  target_link_libraries(unit_tests engine)
  enable_testing()
  add_test(NAME unit_tests COMMAND unit_tests)
endif()

install(TARGETS openmk95 RUNTIME DESTINATION bin)
