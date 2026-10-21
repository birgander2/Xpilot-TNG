# CMakeLists.txt for modern SDL3 client: xpilot-tng-client

add_executable(xpilot-tng-client
    main.c
    sdl3_window.c
    sdl3_colors.c
    sdl3_font.c
    sdl3_draw.c
    sdl3_render_world.c
    sdl3_render_objects.c
    sdl3_hud.c
    sdl3_radar.c
    sdl3_sidebar.c
    sdl3_input.c
    sdl3_audio.c
    sdl3_glue.c
    sdl3_talk.c
    sdl3_voice.c
    sdl3_teamselect.c
    sdl3_welcome.c
    sdl3_bitmaps.c
)

target_include_directories(xpilot-tng-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/client
    ${PROJECT_SOURCE_DIR}/src/common
)

target_link_libraries(xpilot-tng-client PRIVATE
    xpclient
    xpcommon
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    EXPAT::EXPAT
    ZLIB::ZLIB
    m
)

# Voice chat needs libopus; without it the client builds without voice.
option(ENABLE_VOICE "Enable voice chat (requires libopus)" ON)
if(ENABLE_VOICE)
    # pkg-config only supplies hints: a stale opus.pc without headers
    # (half removed -dev package) must not break the configure step.
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_OPUS QUIET opus)
    endif()
    # A cached result may stem from another machine sharing this build dir.
    if(OPUS_INCLUDE_DIR AND NOT EXISTS "${OPUS_INCLUDE_DIR}/opus.h")
        unset(OPUS_INCLUDE_DIR CACHE)
    endif()
    if(OPUS_LIBRARY AND NOT EXISTS "${OPUS_LIBRARY}")
        unset(OPUS_LIBRARY CACHE)
    endif()
    find_path(OPUS_INCLUDE_DIR opus.h
              HINTS ${PC_OPUS_INCLUDE_DIRS} PATH_SUFFIXES opus)
    find_library(OPUS_LIBRARY NAMES opus HINTS ${PC_OPUS_LIBRARY_DIRS})
    if(OPUS_INCLUDE_DIR AND OPUS_LIBRARY)
        message(STATUS "Opus found: voice chat enabled.")
        target_compile_definitions(xpilot-tng-client PRIVATE HAVE_OPUS=1)
        target_include_directories(xpilot-tng-client PRIVATE ${OPUS_INCLUDE_DIR})
        target_link_libraries(xpilot-tng-client PRIVATE ${OPUS_LIBRARY})
    elseif(FETCH_OPUS)
        # Static build, so a packaged client (e.g. Windows) needs no extra DLL.
        message(STATUS "FETCH_OPUS enabled: downloading and compiling Opus...")
        include(FetchContent)
        set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
        set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
        set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
        # EXCLUDE_FROM_ALL (CMake >= 3.28) keeps Opus out of "cmake --install".
        set(_opus_exclude)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
            set(_opus_exclude EXCLUDE_FROM_ALL)
        endif()
        FetchContent_Declare(
            opus
            GIT_REPOSITORY https://github.com/xiph/opus.git
            GIT_TAG v1.5.2
            ${_opus_exclude}
        )
        FetchContent_MakeAvailable(opus)
        target_compile_definitions(xpilot-tng-client PRIVATE HAVE_OPUS=1)
        target_link_libraries(xpilot-tng-client PRIVATE opus)
    else()
        message(WARNING "libopus not found, voice chat disabled.\n"
                        "    Ubuntu / Debian: sudo apt install libopus-dev\n"
                        "    openSUSE:        sudo zypper in libopus-devel\n"
                        "    Fedora:          sudo dnf install opus-devel\n"
                        "    Arch Linux:      sudo pacman -S opus\n"
                        "  Alternatively, re-run CMake with automatic download:\n"
                        "    cmake -B build -DFETCH_OPUS=ON")
    endif()
endif()

add_custom_target(xpilot-tng-sdl3 ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:xpilot-tng-client> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/xpilot-tng-sdl3
    DEPENDS xpilot-tng-client
)

install(TARGETS xpilot-tng-client DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS xpilot-tng-client DESTINATION ${CMAKE_INSTALL_BINDIR})
