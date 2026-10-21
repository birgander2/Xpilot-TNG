# CMakeLists.txt for xpilot-tng-mapeditor (Dear ImGui + SDL3)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8-docking
)
FetchContent_MakeAvailable(imgui)

add_executable(xpilot-tng-mapeditor
    main.cpp
    editor_app.cpp
    editor_canvas.cpp
    editor_panels.cpp
    map_model.cpp
    map_io.cpp
    option_catalog.cpp
    ${PROJECT_SOURCE_DIR}/src/launcher/process_manager.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

target_compile_features(xpilot-tng-mapeditor PRIVATE cxx_std_20)

target_include_directories(xpilot-tng-mapeditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/launcher
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(xpilot-tng-mapeditor PRIVATE
    SDL3::SDL3
    EXPAT::EXPAT
    ZLIB::ZLIB
    Threads::Threads
    m
)

install(TARGETS xpilot-tng-mapeditor DESTINATION ${CMAKE_INSTALL_BINDIR})

add_custom_target(xpilot-tng-mapeditor-symlink ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_BINARY_DIR}/xpilot-tng-mapeditor ${PROJECT_SOURCE_DIR}/xpilot-tng-mapeditor
    DEPENDS xpilot-tng-mapeditor
)
