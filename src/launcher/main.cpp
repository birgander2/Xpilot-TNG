#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <iostream>
#include <filesystem>
#include "launcher_app.h"

namespace fs = std::filesystem;

static void SetupModernTheme() {
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
    colors[ImGuiCol_TabUnfocusedActive]  = ImVec4(0.15f, 0.19f, 0.26f, 1.00f);
}

static std::string FindFontFile(const std::vector<std::string>& candidates) {
    for (const auto& path : candidates) {
        std::error_code ec;
        if (fs::exists(path, ec) && fs::is_regular_file(path, ec)) {
            return path;
        }
    }
    return "";
}

static LauncherFonts SetupFonts(ImGuiIO& io, const std::string& app_dir) {
    LauncherFonts fonts;

    // Data directory candidates: $XPILOT_DATADIR, then the installed CONF_DATADIR
    std::string env_dir = std::getenv("XPILOT_DATADIR") ? std::getenv("XPILOT_DATADIR") : "";
    if (!env_dir.empty() && env_dir.back() != '/') env_dir += '/';
    const std::string conf_dir = CONF_DATADIR;

    std::vector<std::string> regular_candidates = {
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-fonts/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        app_dir + "/../lib/fonts/IBMPlexMono-Medium.ttf",
        app_dir + "/lib/fonts/IBMPlexMono-Medium.ttf",
        "lib/fonts/IBMPlexMono-Medium.ttf",
        env_dir + "fonts/IBMPlexMono-Medium.ttf",
        conf_dir + "fonts/IBMPlexMono-Medium.ttf"
    };

    std::vector<std::string> bold_candidates = {
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/liberation-fonts/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        app_dir + "/../lib/fonts/VeraMoBd.ttf",
        app_dir + "/lib/fonts/VeraMoBd.ttf",
        "lib/fonts/VeraMoBd.ttf",
        env_dir + "fonts/VeraMoBd.ttf",
        conf_dir + "fonts/VeraMoBd.ttf"
    };

    std::string reg_path = FindFontFile(regular_candidates);
    std::string bold_path = FindFontFile(bold_candidates);
    if (bold_path.empty()) bold_path = reg_path;
    if (reg_path.empty()) reg_path = bold_path;

    if (!reg_path.empty()) {
        fonts.regular = io.Fonts->AddFontFromFileTTF(reg_path.c_str(), 16.0f);
        io.FontDefault = fonts.regular;
    } else {
        fonts.regular = io.Fonts->AddFontDefault();
    }

    if (!bold_path.empty()) {
        fonts.heading = io.Fonts->AddFontFromFileTTF(bold_path.c_str(), 20.0f);
        fonts.title = io.Fonts->AddFontFromFileTTF(bold_path.c_str(), 38.0f);
    } else {
        fonts.heading = fonts.regular;
        fonts.title = fonts.regular;
    }

    if (!reg_path.empty()) {
        fonts.subtitle = io.Fonts->AddFontFromFileTTF(reg_path.c_str(), 17.0f);
    } else {
        fonts.subtitle = fonts.regular;
    }

    return fonts;
}

int main(int argc, char* argv[]) {
    (void)argc;

    // Relocatable bundle: if lib/maps sits next to the executable, run from there.
    if (const char* base = SDL_GetBasePath()) {
        std::error_code ec;
        fs::path bundle(base);
        if (fs::is_directory(bundle / "lib" / "maps", ec))
            fs::current_path(bundle, ec);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    std::string app_dir = ".";
    if (argv && argv[0]) {
        try {
            app_dir = fs::path(argv[0]).parent_path().string();
        } catch (...) {}
    }

    const int WINDOW_WIDTH = 560;
    const int WINDOW_HEIGHT = 560;

    SDL_Window* window = SDL_CreateWindow(
        "XPilot TNG - Launcher & Control Center",
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    SetupModernTheme();
    LauncherFonts fonts = SetupFonts(io, app_dir);

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    {   // scope: ~LauncherApp frees the map textures, which needs the renderer to still exist
    LauncherApp app(window, renderer, app_dir);
    app.SetFonts(fonts);
    app.Init();

    int cur_w = 0, cur_h = 0;
    Uint64 size_check_until = 0, size_request_at = 0;
    bool size_settled = true;
    bool hidden = false;
    int active_frames = 30;     // full-rate frames left after input; otherwise idle at ~10 fps
    while (!app.WantsQuit()) {
        SDL_Event event;
        // Sleep until input arrives (or 100 ms pass for log/status updates) instead of spinning at vsync.
        bool got = hidden ? false : (active_frames > 0 ? SDL_PollEvent(&event)
                                                       : SDL_WaitEventTimeout(&event, 100));
        while (got) {
            active_frames = 30;
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                app.RequestQuit();
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) {
                app.RequestQuit();
            }
            got = SDL_PollEvent(&event);
        }
        if (active_frames > 0) active_frames--;

        app.Update();

        // The window size follows the current screen (small menu, large setup screens).
        int want_w = 0, want_h = 0;
        app.DesiredWindowSize(&want_w, &want_h);
        // The window manager applies SDL_SetWindowSize asynchronously and some (e.g. on a Raspberry Pi) drop or
        // clamp it when the window was just resized or its resizable flag changed. So the request is verified for
        // a moment and repeated until the real size matches.
        Uint64 now_ms = SDL_GetTicks();
        if (want_w != cur_w || want_h != cur_h) {
            cur_w = want_w;
            cur_h = want_h;
            size_check_until = now_ms + 1500;
            size_request_at = 0;
            size_settled = false;
        }
        if (!size_settled && !hidden) {
            int aw = 0, ah = 0;
            SDL_GetWindowSize(window, &aw, &ah);
            if (aw == cur_w && ah == cur_h) {
                SDL_SetWindowResizable(window, app.WindowResizable());
                size_settled = true;
            } else if (now_ms >= size_check_until) {
                SDL_SetWindowResizable(window, app.WindowResizable());   // give up, keep what the WM gave us
                size_settled = true;
            } else if (now_ms - size_request_at >= 200) {
                SDL_SetWindowResizable(window, true);   // a fixed-size window may refuse SetWindowSize
                SDL_SetWindowSize(window, cur_w, cur_h);
                SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                size_request_at = now_ms;
            }
            active_frames = 30;   // keep polling until settled
        }

        // The launcher is hidden while a game is running and comes back afterwards.
        bool want_hidden = app.WindowHidden();
        if (want_hidden != hidden) {
            if (want_hidden) {
                SDL_HideWindow(window);
            } else {
                SDL_ShowWindow(window);
                SDL_RaiseWindow(window);
            }
            hidden = want_hidden;
        }
        if (hidden) {
            SDL_Delay(50);
            continue;
        }

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
