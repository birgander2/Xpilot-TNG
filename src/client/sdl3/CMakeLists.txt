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
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(OPUS QUIET IMPORTED_TARGET opus)
    endif()
    if(TARGET PkgConfig::OPUS)
        message(STATUS "Opus found: voice chat enabled.")
        target_compile_definitions(xpilot-tng-client PRIVATE HAVE_OPUS=1)
        target_link_libraries(xpilot-tng-client PRIVATE PkgConfig::OPUS)
    else()
        message(WARNING "libopus not found, voice chat disabled.\n"
                        "    Ubuntu / Debian: sudo apt install libopus-dev\n"
                        "    Fedora:          sudo dnf install opus-devel\n"
                        "    Arch Linux:      sudo pacman -S opus")
    endif()
endif()

add_custom_target(xpilot-tng-sdl3 ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:xpilot-tng-client> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/xpilot-tng-sdl3
    DEPENDS xpilot-tng-client
)

install(TARGETS xpilot-tng-client DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS xpilot-tng-client DESTINATION ${CMAKE_INSTALL_BINDIR})
