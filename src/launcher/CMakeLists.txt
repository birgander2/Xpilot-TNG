# CMakeLists.txt for xpilot-tng-launcher (Dear ImGui + SDL3)

include(FetchContent)

message(STATUS "Configuring Dear ImGui for xpilot-tng-launcher...")
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8-docking
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

add_executable(xpilot-tng-launcher
    main.cpp
    launcher_app.cpp
    client_profile.cpp
    ui_helpers.cpp
    map_preview.cpp
    options_model.cpp
    process_manager.cpp
    server_browser.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(xpilot-tng-launcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${PROJECT_SOURCE_DIR}/src/common
)

target_link_libraries(xpilot-tng-launcher PRIVATE
    SDL3::SDL3
    EXPAT::EXPAT
    Threads::Threads
    m
)

target_compile_definitions(xpilot-tng-launcher PRIVATE XPILOT_TNG_VERSION="${PROJECT_VERSION}")

install(TARGETS xpilot-tng-launcher DESTINATION ${CMAKE_INSTALL_BINDIR})

add_custom_target(xpilot-tng-launcher-symlink ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_BINARY_DIR}/xpilot-tng-launcher ${PROJECT_SOURCE_DIR}/xpilot-tng-launcher
    DEPENDS xpilot-tng-launcher
)
