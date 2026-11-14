// xpilot-tng-shipeditor: graphical ship shape editor for XPilot TNG
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "editor.h"

namespace fs = std::filesystem;

// Same look as the launcher and the map editor
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
    colors[ImGuiCol_TabSelected]          = ImVec4(0.20f, 0.27f, 0.38f, 1.00f);
    colors[ImGuiCol_TabDimmed]            = ImVec4(0.10f, 0.12f, 0.17f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]    = ImVec4(0.15f, 0.19f, 0.26f, 1.00f);
}

static std::string FindFont(const std::vector<std::string>& candidates) {
    for (const auto& path : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) return path;
    }
    return "";
}

static EditorFonts SetupFonts(ImGuiIO& io, const std::string& app_dir) {
    EditorFonts fonts;
    const std::string reg = FindFont({
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-fonts/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        app_dir + "/../../lib/fonts/IBMPlexMono-Medium.ttf",
        app_dir + "/../lib/fonts/IBMPlexMono-Medium.ttf"});
    const std::string bold = FindFont({
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/liberation-fonts/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "C:/Windows/Fonts/segoeuib.ttf",
        app_dir + "/../../lib/fonts/VeraMoBd.ttf",
        app_dir + "/../lib/fonts/VeraMoBd.ttf"});
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
    std::string open_arg;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            std::cout << "Usage: xpilot-tng-shipeditor [shipshapes-file | \"(SH: ...)\"]\n"
                         "Edits XPilot ship shapes; ships are stored in your collection\n"
                         "(~/.config/xpilot-tng/shipshapes.cfg) or as xpilot.shipShape in the xpilotrc.\n";
            return 0;
        }
        open_arg = a;
    }

#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);   // started by the launcher: its log pipe may close before we do
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::string app_dir = ".";
    if (const char* base = SDL_GetBasePath()) app_dir = base;
    while (app_dir.size() > 1 && app_dir.back() == '/') app_dir.pop_back();

    SDL_Window* window = SDL_CreateWindow("XPilot TNG Ship Editor", 1400, 880,
                                          SDL_WINDOW_RESIZABLE);   // no HIGH_PIXEL_DENSITY: mouse offset with fractional scaling
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 1100, 640);
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
        Editor app(window, app_dir);
        app.SetFonts(fonts);
        app.Init(open_arg);

        int active_frames = 30;   // full rate after input, otherwise idle at ~10 fps
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
            if (app.Animating()) active_frames = 2;

            app.Update();
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app.Render();
            ImGui::Render();
            SDL_SetRenderDrawColor(renderer, 18, 22, 28, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
#ifdef SHIPEDITOR_SCREENSHOT   // test builds only: SHIPEDITOR_SCREENSHOT=file.bmp saves frame 20 and quits
            static int frame = 0;
            if (const char* shot = std::getenv("SHIPEDITOR_SCREENSHOT"); shot && ++frame == 20) {
                if (SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr)) { SDL_SaveBMP(s, shot); SDL_DestroySurface(s); }
                break;
            }
#endif
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
