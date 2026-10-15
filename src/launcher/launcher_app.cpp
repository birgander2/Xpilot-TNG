#include "launcher_app.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

#ifndef XPILOT_TNG_VERSION
#define XPILOT_TNG_VERSION "dev"
#endif

namespace {

const ImVec4 kAccent(0.35f, 0.72f, 1.00f, 1.00f);
const ImVec4 kGood(0.25f, 0.85f, 0.40f, 1.00f);
const ImVec4 kWarn(1.00f, 0.80f, 0.30f, 1.00f);
const ImVec4 kBad(1.00f, 0.40f, 0.40f, 1.00f);

void CopyTo(char* dst, size_t cap, const std::string& s) {
    std::snprintf(dst, cap, "%s", s.c_str());
}

void StatusDot(const ImVec4& col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    dl->AddCircleFilled(ImVec2(p.x + 6, p.y + h * 0.5f), 5.0f, ImGui::ColorConvertFloat4ToU32(col));
    ImGui::Dummy(ImVec2(16, h));
    ImGui::SameLine();
}

bool AnyPopupOpen() { return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId); }

bool HotkeyPressed(ImGuiKey key) {
    return !ImGui::GetIO().WantTextInput && !AnyPopupOpen() && ImGui::IsKeyPressed(key, false);
}

} // namespace

// ============================================================================
// lifecycle
// ============================================================================

LauncherApp::LauncherApp(SDL_Window* window, SDL_Renderer* renderer, const std::string& app_dir)
    : window_(window), renderer_(renderer), app_dir_(app_dir) {}

LauncherApp::~LauncherApp() {
    MapScanner::FreeThumbnails(maps_);
}

std::string LauncherApp::FindRoot() const {
    std::vector<fs::path> candidates = {fs::current_path(), fs::path(app_dir_),
                                        fs::path(app_dir_) / "..", fs::path(app_dir_) / "../.."};
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / "lib" / "maps", ec)) return fs::weakly_canonical(c, ec).string();
    }
    return "";
}

std::string LauncherApp::ResolveBinaryPath(const std::string& bin_name) const {
    std::vector<std::string> candidates = {
        root_dir_ + "/" + bin_name, root_dir_ + "/build/" + bin_name,
        app_dir_ + "/" + bin_name,  app_dir_ + "/../" + bin_name,
        "/usr/local/bin/" + bin_name, "/usr/bin/" + bin_name};
    for (const auto& path : candidates) {
        std::error_code ec;
        if (fs::exists(path, ec) && fs::is_regular_file(path, ec))
            return fs::canonical(path, ec).string();
    }
    return bin_name; // let execvp search PATH
}

void LauncherApp::Init() {
    root_dir_ = FindRoot();
    if (!root_dir_.empty()) {
        std::error_code ec;
        fs::current_path(root_dir_, ec); // the server resolves lib/ relative to its cwd
    }
    server_bin_path_ = ResolveBinaryPath("xpilot-tng-server");
    client_bin_path_ = ResolveBinaryPath("xpilot-tng-client");

    baseline_ok_ = options_mgr_.LoadBaseline(server_bin_path_, "lib/defaults.txt");
    if (!baseline_ok_)
        process_mgr_.AddLog("LAUNCHER", "Could not read server defaults (" + server_bin_path_ +
                                            " -dump). Option editing is limited.");

    process_mgr_.AddLog("LAUNCHER", "Scanning maps in lib/maps ...");
    maps_ = MapScanner::ScanDirectory("lib/maps", renderer_);
    for (auto& m : maps_) {
        std::error_code ec;
        m.full_path = fs::absolute(m.full_path, ec).string();
    }
    process_mgr_.AddLog("LAUNCHER", std::to_string(maps_.size()) + " maps found.");

    if (server_port_ <= 0) server_port_ = OptionsManager::FindFreePort(15345);
}

void LauncherApp::DesiredWindowSize(int* w, int* h) const {
    if (screen_ == Screen::Menu) { *w = 560; *h = 560; }
    else { *w = 1120; *h = 740; }
}

void LauncherApp::RequestQuit() {
    if (process_mgr_.IsServerRunning() && !local_shutdown_pending_) {
        open_quit_popup_ = true;
        if (screen_ == Screen::Playing) return; // window is hidden, finish playing first
        return;
    }
    quit_ = true;
}

// ============================================================================
// navigation / actions
// ============================================================================

void LauncherApp::GoTo(Screen s) {
    prev_screen_ = screen_;
    screen_ = s;
    focus_first_ = true;
    if (s == Screen::Menu) {
        banner_.clear();
    }
    if (s == Screen::LocalSetup || s == Screen::ServerSetup) {
        const std::string& want = (s == Screen::LocalSetup) ? last_map_local_ : last_map_server_;
        int idx = -1;
        for (size_t i = 0; i < maps_.size(); ++i)
            if (maps_[i].filename == want) { idx = static_cast<int>(i); break; }
        if (idx < 0) {
            for (size_t i = 0; i < maps_.size(); ++i)
                if (maps_[i].filename == "ndh.xp2") { idx = static_cast<int>(i); break; }
        }
        if (idx < 0 && !maps_.empty()) idx = 0;
        if (idx >= 0) SelectMap(idx);
    }
}

void LauncherApp::SelectMap(int idx) {
    if (idx < 0 || idx >= static_cast<int>(maps_.size())) return;
    selected_map_idx_ = idx;
    const MapInfo& map = maps_[idx];
    options_mgr_.SelectMap(map);
    if (screen_ == Screen::ServerSetup) last_map_server_ = map.filename;
    else last_map_local_ = map.filename;
}

void LauncherApp::StartLocalGame() {
    if (selected_map_idx_ < 0 || launching_) return;
    const MapInfo& map = maps_[selected_map_idx_];

    int port = local_port_ > 0 ? local_port_ : OptionsManager::FindFreePort(15345);
    if (!OptionsManager::IsPortFree(port)) {
        banner_ = "Port " + std::to_string(port) + " is already in use. Choose another one under Advanced > Network.";
        return;
    }

    auto server_args = options_mgr_.BuildServerArgs(LaunchMode::Local, map.full_path, port);

    std::vector<std::string> client_args;
    if (name_override_[0]) { client_args.push_back("-name"); client_args.push_back(name_override_); }
    if (team_choice_ >= 0) { client_args.push_back("-team"); client_args.push_back(std::to_string(team_choice_)); }
    client_args.push_back("-port");
    client_args.push_back(std::to_string(port));
    client_args.push_back("localhost");

    banner_.clear();
    force_log_open_ = false;
    play_kind_ = PlayKind::Local;
    launching_ = true;
    local_shutdown_pending_ = false;
    running_map_title_ = map.title;
    running_map_file_ = map.filename;
    running_port_ = port;
    process_mgr_.LaunchLocalGame(server_bin_path_, server_args, client_bin_path_, client_args);
}

void LauncherApp::StartServerMode() {
    if (selected_map_idx_ < 0) return;
    const MapInfo& map = maps_[selected_map_idx_];

    int port = server_port_ > 0 ? server_port_ : OptionsManager::FindFreePort(15345);
    if (!OptionsManager::IsPortFree(port)) {
        banner_ = "Port " + std::to_string(port) + " is already in use.";
        return;
    }

    // Server mode settings live outside of the per-map option set.
    if (server_max_players_ > 0) options_mgr_.SetInt("playerLimit", server_max_players_);
    if (server_greeting_[0]) options_mgr_.Set("greeting", server_greeting_);
    options_mgr_.SetBool("reportToMetaServer", server_meta_);

    auto args = options_mgr_.BuildServerArgs(LaunchMode::Server, map.full_path, port);
    banner_.clear();
    if (!process_mgr_.StartServer(server_bin_path_, args)) {
        banner_ = "The server could not be started. See log.";
        force_log_open_ = true;
        return;
    }
    running_map_title_ = map.title;
    running_map_file_ = map.filename;
    running_port_ = port;
    server_started_ = std::chrono::steady_clock::now();
    play_kind_ = PlayKind::None;
    GoTo(Screen::ServerRunning);
}

void LauncherApp::JoinNetworkGame() {
    banner_.clear();
    // No server address: the client shows its own server browser (welcome screen).
    if (!process_mgr_.StartClient(client_bin_path_, {})) {
        banner_ = "The client could not be started (" + client_bin_path_ + ").";
        return;
    }
    play_kind_ = PlayKind::Network;
    GoTo(Screen::Playing);
}

void LauncherApp::FinishPlaying(int code) {
    bool local = (play_kind_ == PlayKind::Local);
    bool was_active = (screen_ == Screen::Playing) || launching_;
    play_kind_ = PlayKind::None;
    launching_ = false;
    if (!was_active) return;

    if (local) {
        // The server was started with +noQuit and ends by itself when nobody is left.
        local_shutdown_pending_ = true;
        local_shutdown_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    }
    if (code != 0) {
        Screen back = local ? Screen::LocalSetup : Screen::Menu;
        GoTo(back);
        banner_ = "The client ended with an error (exit code " + std::to_string(code) + "). See log.";
        force_log_open_ = true;
    } else {
        GoTo(Screen::Menu);
    }
}

void LauncherApp::StopEverything() {
    process_mgr_.CancelLaunch();
    process_mgr_.StopClient();
    process_mgr_.StopServer();
    launching_ = false;
    local_shutdown_pending_ = false;
}

void LauncherApp::Update() {
    process_mgr_.Update();

    if (process_mgr_.ConsumeLaunchFailed()) {
        launching_ = false;
        play_kind_ = PlayKind::None;
        if (screen_ != Screen::LocalSetup) GoTo(Screen::LocalSetup);
        banner_ = "The local game could not be started. See log below.";
        force_log_open_ = true;
    }

    if (launching_ && process_mgr_.IsClientRunning()) {
        launching_ = false;
        GoTo(Screen::Playing);
    }

    int code = 0;
    if (process_mgr_.ConsumeClientExited(&code)) FinishPlaying(code);

    int scode = 0;
    if (process_mgr_.ConsumeServerExited(&scode)) {
        if (screen_ == Screen::ServerRunning) {
            GoTo(Screen::ServerSetup);
            banner_ = scode == 0 ? "The server has stopped."
                                 : "The server stopped unexpectedly (exit code " + std::to_string(scode) + "). See log.";
            force_log_open_ = scode != 0;
        } else if (screen_ == Screen::Menu && scode != 0 && !local_shutdown_pending_) {
            banner_ = "The server stopped unexpectedly (exit code " + std::to_string(scode) + ").";
        }
    }

    if (local_shutdown_pending_) {
        if (!process_mgr_.IsServerRunning()) {
            local_shutdown_pending_ = false;
        } else if (process_mgr_.HumanCount() > 0) {
            local_shutdown_pending_ = false; // somebody else is playing: keep the server
        } else if (std::chrono::steady_clock::now() > local_shutdown_deadline_) {
            process_mgr_.AddLog("LAUNCHER", "Nobody is left on the local server, stopping it.");
            process_mgr_.StopServer();
            local_shutdown_pending_ = false;
        }
    }
}

// ============================================================================
// frame
// ============================================================================

void LauncherApp::RenderUI() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::Begin("LauncherRoot", nullptr, flags);
    ImGui::PopStyleVar(3);

    switch (screen_) {
        case Screen::Menu:          RenderMenu(); break;
        case Screen::LocalSetup:    RenderSetup(false); break;
        case Screen::ServerSetup:   RenderSetup(true); break;
        case Screen::ServerRunning: RenderServerRunning(); break;
        case Screen::Playing:       break;
    }

    if (open_quit_popup_) {
        ImGui::OpenPopup("Server still running");
        open_quit_popup_ = false;
    }
    RenderQuitPopup();

    // Esc = back one level
    if (!AnyPopupOpen() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
        !launching_) {
        if (screen_ == Screen::LocalSetup || screen_ == Screen::ServerSetup || screen_ == Screen::ServerRunning)
            GoTo(Screen::Menu);
    }

    ImGui::End();
}

void LauncherApp::RenderBanner() {
    if (banner_.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, kBad);
    ImGui::TextWrapped("%s", banner_.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton("Dismiss")) banner_.clear();
    ImGui::Spacing();
}

void LauncherApp::RenderHeader(const char* title, bool show_back) {
    if (show_back) {
        const char* back = (screen_ == Screen::ServerRunning) ? "< Menu" : "< Back";
        if (ImGui::Button(back, ImVec2(80, 0)) && !launching_) GoTo(Screen::Menu);
        ImGui::SameLine();
    }
    ImGui::TextColored(kAccent, "%s", title);
    ImGui::Separator();
    ImGui::Spacing();
}

// ============================================================================
// main menu
// ============================================================================

void LauncherApp::RenderMenu() {
    const float col_w = 440.0f;
    const float x0 = (ImGui::GetWindowWidth() - col_w) * 0.5f;

    ImGui::Dummy(ImVec2(0, 16));
    if (fonts_.title) ImGui::PushFont(fonts_.title);
    float tw = ImGui::CalcTextSize("XPILOT TNG").x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - tw) * 0.5f);
    ImGui::TextColored(kAccent, "XPILOT TNG");
    if (fonts_.title) ImGui::PopFont();

    if (fonts_.subtitle) ImGui::PushFont(fonts_.subtitle);
    float sw = ImGui::CalcTextSize("Launcher").x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - sw) * 0.5f);
    ImGui::TextColored(ImVec4(0.55f, 0.62f, 0.72f, 1.0f), "Launcher");
    if (fonts_.subtitle) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 12));

    if (!banner_.empty()) {
        ImGui::SetCursorPosX(x0);
        ImGui::PushTextWrapPos(x0 + col_w);
        ImGui::PushStyleColor(ImGuiCol_Text, kBad);
        ImGui::TextWrapped("%s", banner_.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }

    auto menu_button = [&](const char* id, const char* label, const char* sub) {
        ImGui::SetCursorPosX(x0);
        float btn_h = sub[0] ? 62.0f : 50.0f;
        bool clicked = ImGui::Button((std::string("##") + id).c_str(), ImVec2(col_w, btn_h));
        ImVec2 mn = ImGui::GetItemRectMin();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImFont* hfont = fonts_.heading ? fonts_.heading : ImGui::GetFont();
        float hfont_size = fonts_.heading ? fonts_.heading->FontSize : ImGui::GetFontSize() * 1.25f;
        ImVec2 label_size = hfont->CalcTextSizeA(hfont_size, FLT_MAX, -1.0f, label);

        if (sub[0]) {
            ImFont* sfont = fonts_.regular ? fonts_.regular : ImGui::GetFont();
            float sfont_size = fonts_.regular ? fonts_.regular->FontSize : ImGui::GetFontSize();
            ImVec2 sub_size = sfont->CalcTextSizeA(sfont_size, FLT_MAX, -1.0f, sub);

            float total_text_h = label_size.y + 3.0f + sub_size.y;
            float start_y = mn.y + (btn_h - total_text_h) * 0.5f;

            float label_x = mn.x + (col_w - label_size.x) * 0.5f;
            dl->AddText(hfont, hfont_size, ImVec2(label_x, start_y),
                        IM_COL32(235, 240, 250, 255), label);

            float sub_x = mn.x + (col_w - sub_size.x) * 0.5f;
            dl->AddText(sfont, sfont_size, ImVec2(sub_x, start_y + label_size.y + 3.0f),
                        IM_COL32(140, 155, 175, 255), sub);
        } else {
            float label_x = mn.x + (col_w - label_size.x) * 0.5f;
            float label_y = mn.y + (btn_h - label_size.y) * 0.5f;
            dl->AddText(hfont, hfont_size, ImVec2(label_x, label_y),
                        IM_COL32(235, 240, 250, 255), label);
        }

        ImGui::Dummy(ImVec2(0, 3));
        return clicked;
    };

    if (focus_first_) {
        ImGui::SetKeyboardFocusHere();
        focus_first_ = false;
    }
    bool local = menu_button("local", "Start Local Game", "Play against bots on this machine");
    bool join = menu_button("join", "Join Network Game", "Browse LAN and internet servers");
    bool server = menu_button("server", "Launch Server", "Host a game for other players");
    bool quit = menu_button("quit", "Quit", "");

    local |= HotkeyPressed(ImGuiKey_L);
    join |= HotkeyPressed(ImGuiKey_J);
    server |= HotkeyPressed(ImGuiKey_S);
    quit |= HotkeyPressed(ImGuiKey_Q);

    if (process_mgr_.IsServerRunning() && !local_shutdown_pending_) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::SetCursorPosX(x0);
        StatusDot(process_mgr_.IsServerReady() ? kGood : kWarn);
        int humans = process_mgr_.HumanCount();
        ImGui::Text("Server %s on port %d  -  %s  -  %d player%s",
                    process_mgr_.IsServerReady() ? "running" : "starting", running_port_,
                    running_map_file_.c_str(), humans, humans == 1 ? "" : "s");
        ImGui::SetCursorPosX(x0 + 22);
        if (ImGui::SmallButton("Manage")) GoTo(Screen::ServerRunning);
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop")) process_mgr_.StopServer();
    }

    // footer
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 32);
    ImGui::Separator();
    std::string ver = std::string("v") + XPILOT_TNG_VERSION;
    float vx = (ImGui::GetWindowWidth() - ImGui::CalcTextSize(ver.c_str()).x) * 0.5f;
    ImGui::SetCursorPosX(vx);
    ImGui::TextDisabled("%s", ver.c_str());

    if (local) GoTo(Screen::LocalSetup);
    else if (join) JoinNetworkGame();
    else if (server) GoTo(Screen::ServerSetup);
    else if (quit) RequestQuit();
}

// ============================================================================
// setup screens
// ============================================================================

void LauncherApp::RenderMapCard(const MapInfo& map) {
    ImGui::BeginChild("ThumbBorder", ImVec2(0, 276), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    float x = (ImGui::GetContentRegionAvail().x - 256) * 0.5f;
    if (x > 0) ImGui::SetCursorPosX(x);
    if (map.thumbnail) ImGui::Image((ImTextureID)(intptr_t)map.thumbnail, ImVec2(256, 256));
    else ImGui::TextDisabled("No preview");
    ImGui::EndChild();

    ImGui::TextColored(kAccent, "%s", map.title.c_str());
    ImGui::TextDisabled("%s  (%s)", map.filename.c_str(), map.is_xp2 ? "XP2 vector" : "XP raster");
    ImGui::Text("by %s", map.author.c_str());
    ImGui::Text("%d x %d  |  %d bases  |  %d cannons", map.width, map.height, map.num_bases, map.num_cannons);
}

void LauncherApp::RenderMapPanel(bool with_list) {
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "MAP");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##mapfilter", "Search maps...", map_filter_, sizeof(map_filter_));
    ImGui::Spacing();

    if (selected_map_idx_ >= 0 && selected_map_idx_ < static_cast<int>(maps_.size()))
        RenderMapCard(maps_[selected_map_idx_]);
    else
        ImGui::TextDisabled("No map selected.");

    if (!with_list) return;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Available maps (%zu)", maps_.size());
    ImGui::BeginChild("MapList", ImVec2(0, 0), ImGuiChildFlags_Borders);

    std::string filter = map_filter_;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    for (size_t i = 0; i < maps_.size(); ++i) {
        const MapInfo& m = maps_[i];
        if (!filter.empty()) {
            std::string fn = m.filename, ti = m.title;
            std::transform(fn.begin(), fn.end(), fn.begin(), ::tolower);
            std::transform(ti.begin(), ti.end(), ti.begin(), ::tolower);
            if (fn.find(filter) == std::string::npos && ti.find(filter) == std::string::npos) continue;
        }
        bool selected = selected_map_idx_ == static_cast<int>(i);
        std::string label = (m.is_xp2 ? "[XP2] " : "[XP]  ") + m.filename;
        if (ImGui::Selectable(label.c_str(), selected) && !launching_) SelectMap(static_cast<int>(i));
        if (selected && focus_first_) ImGui::SetScrollHereY(0.5f);
    }
    ImGui::EndChild();
}

void LauncherApp::ModifiedMenu(const char* key) {
    if (ImGui::BeginPopupContextItem((std::string("ctx_") + key).c_str())) {
        if (ImGui::MenuItem("Reset to map default")) options_mgr_.Reset(key);
        ImGui::EndPopup();
    }
}

static std::string OptLabel(const char* label, const char* key, bool modified) {
    return std::string(label) + (modified ? "  *" : "") + "##" + key;
}

bool LauncherApp::OptBool(const char* label, const char* key, const char* tip) {
    if (!options_mgr_.Known(key)) return false;
    bool v = options_mgr_.GetBool(key);
    bool changed = ImGui::Checkbox(OptLabel(label, key, options_mgr_.IsModified(key)).c_str(), &v);
    if (changed) options_mgr_.SetBool(key, v);
    ModifiedMenu(key);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return changed;
}

bool LauncherApp::OptInt(const char* label, const char* key, int lo, int hi, const char* tip) {
    if (!options_mgr_.Known(key)) return false;
    int v = options_mgr_.GetInt(key);
    const char* fmt = (std::string(key) == "worldLives" && v == 0) ? "Unlimited (0)" : "%d";
    bool changed = ImGui::SliderInt(OptLabel(label, key, options_mgr_.IsModified(key)).c_str(), &v, lo, hi, fmt);
    if (changed) options_mgr_.SetInt(key, v);
    ModifiedMenu(key);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return changed;
}

bool LauncherApp::OptFloat(const char* label, const char* key, float lo, float hi, const char* fmt,
                           bool logarithmic, const char* tip) {
    if (!options_mgr_.Known(key)) return false;
    float v = static_cast<float>(options_mgr_.GetFloat(key));
    bool changed = ImGui::SliderFloat(OptLabel(label, key, options_mgr_.IsModified(key)).c_str(), &v, lo, hi,
                                      fmt, logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
    if (changed) options_mgr_.SetFloat(key, v);
    ModifiedMenu(key);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return changed;
}

void LauncherApp::RenderQuickSettings(bool server_mode) {
    ImGui::SetNextItemOpen(quick_open_, ImGuiCond_Always);
    quick_open_ = ImGui::CollapsingHeader("Quick settings");
    if (!quick_open_) return;

    ImGui::PushID("quick");

    // mode specific row
    ImGui::PushItemWidth(190);
    if (!server_mode) {
        ImGui::InputTextWithHint("Name", "(from .xpilotrc)", name_override_, sizeof(name_override_));
        ImGui::SameLine(0, 24);
        const char* team_label = team_choice_ < 0 ? "Auto" : nullptr;
        char buf[16];
        if (!team_label) { std::snprintf(buf, sizeof(buf), "Team %d", team_choice_); team_label = buf; }
        ImGui::SetNextItemWidth(110);
        if (ImGui::BeginCombo("Team", team_label)) {
            if (ImGui::Selectable("Auto", team_choice_ < 0)) team_choice_ = -1;
            for (int t = 0; t < 10; ++t) {
                std::snprintf(buf, sizeof(buf), "Team %d", t);
                if (ImGui::Selectable(buf, team_choice_ == t)) team_choice_ = t;
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("Port", &server_port_, 0, 0);
        ImGui::SameLine(0, 24);
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("Max players", &server_max_players_, 0, 0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = number of bases + 10");
        ImGui::SameLine(0, 24);
        ImGui::Checkbox("Register at meta server", &server_meta_);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Publishes this server in the public server list.");
        ImGui::SetNextItemWidth(420);
        ImGui::InputTextWithHint("Greeting", "Message shown to joining players", server_greeting_,
                                 sizeof(server_greeting_));
        if (server_port_ < 0) server_port_ = 0;
        if (server_max_players_ < 0) server_max_players_ = 0;
    }
    ImGui::PopItemWidth();
    ImGui::Separator();

    if (!baseline_ok_) {
        ImGui::TextColored(kWarn, "Server defaults could not be read - option editing is unavailable.");
    } else if (ImGui::BeginTable("quickopts", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-130);
        OptInt("Bots", "maxRobots", 0, 16, "Classic robots");
        OptInt("Smartbots", "maxSmartRobots", 0, 16, "Learning AI robots");
        OptInt("Smartbot level", "smartRobotLevel", 1, 100, "Skill of the learning bots (1-100)");
        ImGui::PopItemWidth();
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-130);
        OptInt("Lives", "worldLives", 0, 20, "0 = unlimited lives");
        OptInt("Frames per second", "framesPerSecond", 14, 120);
        OptFloat("Item frequency", "itemProbMult", 0.0f, 3.0f, "%.2f", false, "0 = no items");
        ImGui::PopItemWidth();
        ImGui::EndTable();
    }
    ImGui::PopID();
}

void LauncherApp::RenderAdvanced(bool server_mode) {
    ImGui::SetNextItemOpen(advanced_open_, ImGuiCond_Always);
    advanced_open_ = ImGui::CollapsingHeader("Advanced settings");
    if (!advanced_open_) return;

    ImGui::PushID("adv");
    if (ImGui::BeginTabBar("AdvTabs")) {
        if (baseline_ok_) {
            if (ImGui::BeginTabItem("Gameplay")) {
                ImGui::Spacing();
                ImGui::PushItemWidth(260);
                OptInt("Lives", "worldLives", 0, 20, "0 = unlimited lives");
                OptFloat("Game speed", "gameSpeed", 1.0f, 40.0f, "%.1f");
                OptInt("Frames per second", "framesPerSecond", 14, 120);
                ImGui::PopItemWidth();
                ImGui::Separator();
                OptBool("Player vs. player damage", "allowPlayerKilling");
                OptBool("Team play", "teamPlay");
                OptBool("Team immunity", "teamImmunity");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Physics")) {
                ImGui::Spacing();
                ImGui::PushItemWidth(260);
                OptFloat("Gravity", "gravity", -1.0f, 1.0f, "%.2f");
                OptFloat("Ship mass", "shipMass", 5.0f, 60.0f, "%.1f");
                OptFloat("Shot speed", "shotSpeed", 10.0f, 50.0f, "%.1f");
                OptFloat("Shot life", "shotLife", 20.0f, 120.0f, "%.0f");
                OptFloat("Fire repeat rate", "fireRepeatRate", 0.5f, 10.0f, "%.1f");
                ImGui::PopItemWidth();
                ImGui::Separator();
                OptBool("World wraps around (edgeWrap)", "edgeWrap");
                OptBool("Wall bounces", "allowPlayerBounces");
                OptBool("Wall crash damage", "allowPlayerCrashes");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Weapons & Items")) {
                ImGui::Spacing();
                OptBool("Shields", "allowShields");
                OptBool("Nukes", "allowNukes");
                OptBool("Cluster bombs", "allowClusters");
                OptBool("Weapon modifiers", "allowModifiers");
                OptBool("Smart missiles", "allowSmartMissiles");
                ImGui::Separator();
                ImGui::PushItemWidth(260);
                OptFloat("Item frequency", "itemProbMult", 0.0f, 3.0f, "%.2f", false, "0 = no items");
                OptFloat("Max item density", "maxItemDensity", 0.00001f, 0.01f, "%.5f", true);
                ImGui::PopItemWidth();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Bots")) {
                ImGui::Spacing();
                ImGui::PushItemWidth(260);
                OptInt("Bots", "maxRobots", 0, 16);
                OptInt("Smartbots", "maxSmartRobots", 0, 16);
                OptInt("Smartbot level", "smartRobotLevel", 1, 100, "Skill of the learning bots (1-100)");
                OptInt("Bot team", "robotTeam", 0, 9);
                ImGui::PopItemWidth();
                OptBool("Restrict bots to their team", "restrictRobots");
                OptBool("Bots talk", "robotsTalk");
                OptBool("Bots may leave", "robotsLeave");
                ImGui::EndTabItem();
            }
        }
        if (ImGui::BeginTabItem("Map Options")) {
            ImGui::Spacing();
            const auto& raw = options_mgr_.MapOptions();
            if (selected_map_idx_ >= 0) ImGui::TextDisabled("Options defined by %s", maps_[selected_map_idx_].filename.c_str());
            if (raw.empty()) ImGui::TextDisabled("This map defines no options.");
            ImGui::BeginChild("RawOpts", ImVec2(0, 220), ImGuiChildFlags_Borders);
            for (const auto& [key, value] : raw) {
                std::string canon = options_mgr_.Canonical(key);
                ImGui::PushID(key.c_str());
                ImGui::Text("%s", key.c_str());
                ImGui::SameLine(250);
                char buf[256];
                CopyTo(buf, sizeof(buf), canon.empty() ? value : options_mgr_.Get(canon));
                ImGui::SetNextItemWidth(240);
                if (canon.empty()) {
                    ImGui::BeginDisabled();
                    ImGui::InputText("##v", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Not a server option - read from the map file only.");
                } else if (ImGui::InputText("##v", buf, sizeof(buf))) {
                    options_mgr_.Set(canon, buf);
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (!server_mode && ImGui::BeginTabItem("Network")) {
            ImGui::Spacing();
            ImGui::SetNextItemWidth(140);
            ImGui::InputInt("Server port (0 = automatic)", &local_port_, 0, 0);
            if (local_port_ < 0) local_port_ = 0;
            ImGui::TextDisabled("A free port is chosen automatically when this is 0.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopID();
}

void LauncherApp::RenderLog(float height) {
    ImGui::SetNextItemWidth(110);
    const char* items[] = {"ALL", "SERVER", "CLIENT", "LAUNCHER"};
    ImGui::Combo("##filter", &log_filter_type_, items, IM_ARRAYSIZE(items));
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &auto_scroll_logs_);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) process_mgr_.ClearLogs();

    ImGui::BeginChild("LogScroll", ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& e : process_mgr_.GetLogs()) {
        if (log_filter_type_ == 1 && e.tag != "SERVER") continue;
        if (log_filter_type_ == 2 && e.tag != "CLIENT") continue;
        if (log_filter_type_ == 3 && e.tag != "LAUNCHER") continue;
        ImVec4 col(0.6f, 0.6f, 0.6f, 1.0f);
        if (e.tag == "SERVER") col = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
        else if (e.tag == "CLIENT") col = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
        else if (e.tag == "LAUNCHER") col = kWarn;
        ImGui::TextDisabled("%s", e.timestamp.c_str());
        ImGui::SameLine();
        ImGui::TextColored(col, "%-8s", e.tag.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(e.text.c_str());
    }
    if (auto_scroll_logs_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void LauncherApp::RenderActionBar(bool server_mode) {
    ImGui::Separator();
    ImGui::Spacing();

    StatusDot(process_mgr_.IsServerRunning() ? (process_mgr_.IsServerReady() ? kGood : kWarn)
                                             : ImVec4(0.5f, 0.5f, 0.5f, 1));
    ImGui::Text("Server: %s", process_mgr_.IsServerRunning() ? (process_mgr_.IsServerReady() ? "ready" : "starting") : "stopped");
    if (!server_mode) {
        ImGui::SameLine(0, 24);
        StatusDot(process_mgr_.IsClientRunning() ? kGood : ImVec4(0.5f, 0.5f, 0.5f, 1));
        ImGui::Text("Client: %s", process_mgr_.IsClientRunning() ? "running" : "idle");
    }

    const float bw = 240, bh = 38;
    ImGui::SameLine(ImGui::GetWindowWidth() - bw - 14);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8);

    bool have_map = selected_map_idx_ >= 0;
    bool busy = launching_;
    ImGui::BeginDisabled(!have_map || busy);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.45f, 0.20f, 1.0f));
    const char* label = server_mode ? "LAUNCH SERVER" : (busy ? "STARTING..." : "PLAY");
    bool go = ImGui::Button(label, ImVec2(bw, bh));
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    if (go) {
        if (server_mode) StartServerMode();
        else StartLocalGame();
    }
}

void LauncherApp::RenderSetup(bool server_mode) {
    RenderHeader(server_mode ? "LAUNCH SERVER" : "START LOCAL GAME", true);
    RenderBanner();

    const float bar_h = 60;
    ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::BeginChild("LeftPanel", ImVec2(340, avail.y - bar_h), ImGuiChildFlags_Borders);
    RenderMapPanel(true);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("RightPanel", ImVec2(0, avail.y - bar_h));
    ImGui::BeginDisabled(launching_);
    RenderQuickSettings(server_mode);
    ImGui::Spacing();
    RenderAdvanced(server_mode);
    ImGui::EndDisabled();

    ImGui::Spacing();
    bool& log_flag = server_mode ? log_open_server_ : log_open_local_;
    if (force_log_open_) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        log_flag = true;
        force_log_open_ = false;
    } else {
        ImGui::SetNextItemOpen(log_flag, ImGuiCond_Always);
    }
    log_flag = ImGui::CollapsingHeader("Log");
    if (log_flag) RenderLog(180);
    ImGui::EndChild();

    RenderActionBar(server_mode);
    focus_first_ = false;
}

// ============================================================================
// server running
// ============================================================================

void LauncherApp::RenderServerRunning() {
    RenderHeader("SERVER", true);
    RenderBanner();

    bool ready = process_mgr_.IsServerReady();
    StatusDot(ready ? kGood : kWarn);
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - server_started_).count();
    ImGui::Text("%s on port %d  -  %s  -  up %02lld:%02lld", ready ? "Running" : "Starting", running_port_,
                running_map_title_.c_str(), static_cast<long long>(secs / 60), static_cast<long long>(secs % 60));
    ImGui::Spacing();

    const float bar_h = 60;
    ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::BeginChild("RunLeft", ImVec2(340, avail.y - bar_h), ImGuiChildFlags_Borders);
    for (const auto& m : maps_)
        if (m.filename == running_map_file_) { RenderMapCard(m); break; }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("RunRight", ImVec2(0, avail.y - bar_h));
    const auto& players = process_mgr_.GetPlayers();
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "PLAYERS (%d)", static_cast<int>(players.size()));
    ImGui::BeginChild("PlayersBox", ImVec2(0, 190), ImGuiChildFlags_Borders);
    if (players.empty()) {
        ImGui::TextDisabled("Nobody is connected yet. Bots join as soon as the first player enters.");
    } else if (ImGui::BeginTable("players", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 3);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_None, 1);
        ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_None, 1);
        ImGui::TableSetupColumn("Host", ImGuiTableColumnFlags_None, 3);
        ImGui::TableHeadersRow();
        for (const auto& p : players) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(p.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(p.kind.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", p.team);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(p.host.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "LOG");
    RenderLog(ImGui::GetContentRegionAvail().y);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Players connect to this machine on port %d.", running_port_);
    const float bw = 240, bh = 38;
    ImGui::SameLine(ImGui::GetWindowWidth() - bw - 14);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("STOP SERVER", ImVec2(bw, bh))) {
        process_mgr_.StopServer();
        GoTo(Screen::ServerSetup);
    }
    ImGui::PopStyleColor(3);
    focus_first_ = false;
}

// ============================================================================
// popups
// ============================================================================

void LauncherApp::RenderQuitPopup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Server still running", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    int humans = process_mgr_.HumanCount();
    ImGui::Text("A server is still running on port %d (%d player%s connected).", running_port_, humans,
                humans == 1 ? "" : "s");
    ImGui::Text("Quitting will stop it.");
    ImGui::Spacing();
    if (ImGui::Button("Stop server and quit", ImVec2(180, 0))) {
        StopEverything();
        quit_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
