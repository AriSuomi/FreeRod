# freerod_core: the portable part of FreeRod (main.c, dos/, game/).
# No SDL and no platform code: a build links it with a platform layer that
# implements platform/platform.h (the PC build: platform/platform.c and
# platform/sound.c, see ../CMakeLists.txt). Other projects, such as an STM32
# build, can use it with add_subdirectory(FreeRod/src freerod).

add_library(freerod_core STATIC
    main.c
    dos/dos.c
    dos/fpu.c
    game/anim.c
    game/calendar.c
    game/cars.c
    game/challenge.c
    game/cursor.c
    game/drive.c
    game/driving.c
    game/event.c
    game/exe.c
    game/game.c
    game/garage.c
    game/hood.c
    game/station.c
    game/street.c
    game/race.c
    game/fame.c
    game/mover.c
    game/globals.c
    game/hotdata.c
    game/input.c
    game/lib.c
    game/opening.c
    game/paper.c
    game/question.c
    game/effects.c
    game/music.c
    game/random.c
    game/road.c
    game/roaddraw.c
    game/roadpic.c
    game/roadstate.c
    game/roadcar.c
    game/roadview.c
    game/save.c
    game/screen.c
    game/text.c
    game/ui.c
    game/video.c
)

target_include_directories(freerod_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(freerod_core PUBLIC c_std_11)

if(MSVC)
    target_compile_options(freerod_core PRIVATE /W4 /utf-8)
    target_compile_definitions(freerod_core PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(freerod_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
