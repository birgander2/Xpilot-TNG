#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <cstdlib>
#include "tng_app_icon.h"
#include <filesystem>
#include <iostream>
#include "editor_app.h"

namespace fs = std::filesystem;

// Same look as the launcher.
static void SetupTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.10f, 0.14f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.10f, 0.12f, 0.17f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.11f, 0.13f, 0.18f, 0.98f);
    colors[ImGuiCol_Border]               = ImVec4(0.20f, 0.25f, 0.32f, 0.60f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.14f, 0.17f, 0.23f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.22f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.10f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.15f, 0.21f, 1.00f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.10f, 0.12f, 0.17f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.08f, 0.10f, 0.14f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.24f, 0.30f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.38f, 0.50f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.36f, 0.45f, 0.60f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.35f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.30f, 0.65f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.40f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.18f, 0.24f, 0.33f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.24f, 0.32f, 0.44f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.28f, 0.38f, 0.52f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.18f, 0.24f, 0.33f, 0.80f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.24f, 0.32f, 0.44f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.28f, 0.38f, 0.52f, 1.00f);
    colors[ImGuiCol_Tab]                  = ImVec4(0.12f, 0.15f, 0.21f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.24f, 0.32f, 0.44f, 1.00f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.20f, 0.27f, 0.38f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.10f, 0.12f, 0.17f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.15f, 0.19f, 0.26f, 1.00f);
}

static std::string FindFontFile(const std::vector<std::string>& candidates) {
    for (const auto& path : candidates) {
        std::error_code ec;
        if (fs::exists(path, ec) && fs::is_regular_file(path, ec)) return path;
    }
    return "";
}

static EditorFonts SetupFonts(ImGuiIO& io, const std::string& app_dir) {
    EditorFonts fonts;
    std::string env_dir = std::getenv("XPILOT_DATADIR") ? std::getenv("XPILOT_DATADIR") : "";
    if (!env_dir.empty() && env_dir.back() != '/') env_dir += '/';
    const std::string conf_dir = CONF_DATADIR;

    const std::string reg = FindFontFile({
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-fonts/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        app_dir + "/../lib/fonts/IBMPlexMono-Medium.ttf",
        app_dir + "/lib/fonts/IBMPlexMono-Medium.ttf",
        "lib/fonts/IBMPlexMono-Medium.ttf",
        env_dir + "fonts/IBMPlexMono-Medium.ttf",
        conf_dir + "fonts/IBMPlexMono-Medium.ttf"});
    const std::string bold = FindFontFile({
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/liberation-fonts/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        app_dir + "/../lib/fonts/VeraMoBd.ttf",
        app_dir + "/lib/fonts/VeraMoBd.ttf",
        "lib/fonts/VeraMoBd.ttf",
        env_dir + "fonts/VeraMoBd.ttf",
        conf_dir + "fonts/VeraMoBd.ttf"});

    if (!reg.empty()) {
        fonts.regular = io.Fonts->AddFontFromFileTTF(reg.c_str(), 15.0f);
        fonts.small = io.Fonts->AddFontFromFileTTF(reg.c_str(), 13.0f);
    }
    if (!fonts.regular) fonts.regular = io.Fonts->AddFontDefault();
    io.FontDefault = fonts.regular;
    if (!bold.empty()) fonts.heading = io.Fonts->AddFontFromFileTTF(bold.c_str(), 17.0f);
    return fonts;
}

int main(int argc, char* argv[]) {
    std::string open_path;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            std::cout << "Usage: xpilot-tng-mapeditor [map.xp2]\n";
            return 0;
        }
        open_path = a;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::string app_dir = ".";
    if (const char* base = SDL_GetBasePath()) app_dir = base;
    while (app_dir.size() > 1 && app_dir.back() == '/') app_dir.pop_back();

    SDL_Window* window = SDL_CreateWindow("XPilot TNG Map Editor", 1440, 900,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    tng_set_window_icon(window);
    SDL_SetWindowMinimumSize(window, 1000, 640);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    SetupTheme();
    EditorFonts fonts = SetupFonts(io, app_dir);

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    {
        EditorApp app(window, renderer, app_dir);
        app.SetFonts(fonts);
        app.Init(open_path);

        int active_frames = 30;     // full rate after input, otherwise idle at ~10 fps
        while (!app.WantsQuit()) {
            SDL_Event event;
            bool got = active_frames > 0 ? SDL_PollEvent(&event) : SDL_WaitEventTimeout(&event, 100);
            while (got) {
                active_frames = 30;
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT) app.RequestQuit();
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                    app.RequestQuit();
                if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) app.OpenDropped(event.drop.data);
                got = SDL_PollEvent(&event);
            }
            if (active_frames > 0) active_frames--;

            app.Update();
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app.RenderUI();
            ImGui::Render();
            SDL_SetRenderDrawColor(renderer, 18, 22, 28, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        }
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
