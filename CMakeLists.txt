cmake_minimum_required(VERSION 3.21)
project(FreeRod LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# compile_commands.json in the build directory, used by lint.bat.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The portable part (main.c, dos/, game/) as the library freerod_core.
add_subdirectory(src)

# The PC build: freerod_core with the SDL platform layer.
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)

add_executable(freerod
    src/platform/platform.c
    src/platform/sound.c
)

# No SDL_main: platform.c defines SDL_MAIN_HANDLED and calls SDL_SetMainReady.
target_link_libraries(freerod PRIVATE
    freerod_core
    SDL3::SDL3
    SDL3_image::SDL3_image
)

if(MSVC)
    target_compile_options(freerod PRIVATE /W4 /utf-8)
    target_compile_definitions(freerod PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(freerod PRIVATE -Wall -Wextra -Wpedantic)
endif()
