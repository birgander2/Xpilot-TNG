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

add_custom_target(xpilot-tng-sdl3 ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:xpilot-tng-client> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/xpilot-tng-sdl3
    DEPENDS xpilot-tng-client
)

install(TARGETS xpilot-tng-client DESTINATION ${CMAKE_INSTALL_BINDIR})
