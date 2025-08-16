cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not defined!")
endif()

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if (EXISTS $ENV{DEVKITPRO}/cmake/Switch.cmake)
        set(CMAKE_TOOLCHAIN_FILE $ENV{DEVKITPRO}/cmake/Switch.cmake)
    else()
        message(FATAL_ERROR "please run 'sudo pacman -S switch-cmake`")
    endif()
endif()

project(pulsar LANGUAGES C)

add_library(libpulsar
    src/archive/archive_file.c
    src/archive/archive.c
    src/bfgrp/bfgrp_location.c
    src/bfgrp/bfgrp.c
    src/bfsar/bfsar_file.c
    src/bfsar/bfsar_group.c
    src/bfsar/bfsar_sound.c
    src/bfsar/bfsar_string.c
    src/bfsar/bfsar_wave_archive.c
    src/bfsar/bfsar.c
    src/bfstm/bfstm_channel.c
    src/bfstm/bfstm_info.c
    src/bfstm/bfstm.c
    src/bfwar/bfwar_file.c
    src/bfwar/bfwar.c
    src/bfwav/bfwav_info.c
    src/bfwav/bfwav.c
    src/bfwsd/bfwsd_sound_data.c
    src/bfwsd/bfwsd_wave_id.c
    src/bfwsd/bfwsd.c
    src/player/player_load_formats.c
    src/player/player_load_lookup.c
    src/player/player_load.c
    src/player/player.c
)

target_include_directories(libpulsar PUBLIC include)

set_target_properties(libpulsar PROPERTIES
    C_STANDARD 11
    C_EXTENSIONS ON
)

if (PULSAR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
