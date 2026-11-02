# CMakeLists.txt for xpilot-tng-shipeditor (Dear ImGui + SDL3)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8-docking
)
FetchContent_MakeAvailable(imgui)

# ship_shape / ship_files / ship_widgets are also built into the launcher
# (profile editor); the editor in turn uses the launcher's xpilotrc reader/writer.
add_executable(xpilot-tng-shipeditor
    main.cpp
    editor.cpp
    ship_shape.cpp
    ship_files.cpp
    ship_widgets.cpp
    ${PROJECT_SOURCE_DIR}/src/launcher/xpilotrc.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

target_compile_features(xpilot-tng-shipeditor PRIVATE cxx_std_20)

target_include_directories(xpilot-tng-shipeditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/launcher
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(xpilot-tng-shipeditor PRIVATE
    SDL3::SDL3
    m
)

if(WIN32)
    # GUI subsystem: no console window next to the application
    set_target_properties(xpilot-tng-shipeditor PROPERTIES WIN32_EXECUTABLE ON)
endif()

install(TARGETS xpilot-tng-shipeditor DESTINATION ${CMAKE_INSTALL_BINDIR})

if(NOT WIN32)
add_custom_target(xpilot-tng-shipeditor-symlink ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_BINARY_DIR}/xpilot-tng-shipeditor ${PROJECT_SOURCE_DIR}/xpilot-tng-shipeditor
    DEPENDS xpilot-tng-shipeditor
)
endif()
