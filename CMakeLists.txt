cmake_minimum_required(VERSION 3.21)
project(FreeRod LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)

add_executable(freerod
    src/main.c
    src/dos/dos.c
    src/dos/fpu.c
    src/game/calendar.c
    src/game/cars.c
    src/game/challenge.c
    src/game/cursor.c
    src/game/drive.c
    src/game/driving.c
    src/game/event.c
    src/game/exe.c
    src/game/game.c
    src/game/garage.c
    src/game/hood.c
    src/game/station.c
    src/game/street.c
    src/game/race.c
    src/game/fame.c
    src/game/mover.c
    src/game/globals.c
    src/game/hotdata.c
    src/game/input.c
    src/game/lib.c
    src/game/opening.c
    src/game/paper.c
    src/game/question.c
    src/game/random.c
    src/game/save.c
    src/game/screen.c
    src/game/text.c
    src/game/ui.c
    src/game/video.c
    src/platform/platform.c
)

target_include_directories(freerod PRIVATE src)

# No SDL2main: platform.c defines SDL_MAIN_HANDLED and calls SDL_SetMainReady.
target_link_libraries(freerod PRIVATE
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
)

if(MSVC)
    target_compile_options(freerod PRIVATE /W4 /utf-8)
    target_compile_definitions(freerod PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(freerod PRIVATE -Wall -Wextra -Wpedantic)
endif()
