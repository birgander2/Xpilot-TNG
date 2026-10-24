#include "launcher_app.h"

#include <imgui.h>

#include "ui_helpers.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <filesystem>
#include <unistd.h>

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

// Returns a human-readable problem description, or "" if the binary looks runnable.
// Missing shared libraries (e.g. libSDL3_ttf) are found via ldd without starting the binary.
std::string CheckBinary(const std::string& label, const std::string& path) {
    if (path.find('/') == std::string::npos) return "";   // resolved via PATH at exec time
    if (access(path.c_str(), X_OK) != 0)
        return "The " + label + " was not found or is not executable (" + path + "). Did the build finish?";

    std::string quoted = "'";
    for (char c : path) quoted += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    quoted += "'";
    FILE* f = popen(("ldd " + quoted + " 2>&1").c_str(), "r");
    if (!f) return "";
    std::string missing;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        if (l.find("not found") == std::string::npos) continue;
        size_t b = l.find_first_not_of(" \t");
        size_t e = l.find(" =>");
        if (b == std::string::npos || e == std::string::npos || e <= b) continue;
        if (!missing.empty()) missing += ", ";
        missing += l.substr(b, e - b);
    }
    pclose(f);
    if (missing.empty()) return "";
    return "The " + label + " cannot start: missing system library " + missing +
           ". Please install the matching package (e.g. SDL3_ttf) and restart the launcher.";
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
    // Same order as Conf_datadir() on the server side
    const char* env_dir = std::getenv("XPILOT_DATADIR");
    std::error_code ec;
    if (env_dir && *env_dir && fs::is_directory(env_dir, ec)) data_dir_ = env_dir;
    else if (!root_dir_.empty()) data_dir_ = root_dir_ + "/lib";
    else data_dir_ = CONF_DATADIR;
    while (data_dir_.size() > 1 && data_dir_.back() == '/') data_dir_.pop_back();

    server_bin_path_ = ResolveBinaryPath("xpilot-tng-server");
    client_bin_path_ = ResolveBinaryPath("xpilot-tng-client");

    server_problem_ = CheckBinary("server", server_bin_path_);
    client_problem_ = CheckBinary("client", client_bin_path_);
    for (const auto* prob : {&server_problem_, &client_problem_})
        if (!prob->empty()) {
            process_mgr_.AddLog("LAUNCHER", *prob);
            banner_ += (banner_.empty() ? "" : "\n") + *prob;
        }

    baseline_ok_ = options_mgr_.LoadBaseline(server_bin_path_, data_dir_ + "/defaults.txt");

    profile_.Load((data_dir_ + "/shipshapes.txt").c_str());
    if (baseline_ok_) {   // network settings: compiled default < lib/defaults.txt < xpilotrc
        CopyTo(server_host_, sizeof(server_host_), options_mgr_.Get("serverHost"));
        server_cport_start_ = options_mgr_.GetInt("clientPortStart");
        server_cport_end_ = options_mgr_.GetInt("clientPortEnd");
        if (options_mgr_.InDefaultsFile("contactPort")) server_port_ = options_mgr_.GetInt("contactPort");
    }
    {
        auto rc_int = [](const char* key, int& out) {
            std::string v = XpilotrcValue(key);
            if (!v.empty()) out = std::atoi(v.c_str());
        };
        std::string host = XpilotrcValue("serverHost");
        if (!host.empty()) CopyTo(server_host_, sizeof(server_host_), host);
        rc_int("contactPort", server_port_);
        rc_int("clientPortStart", server_cport_start_);
        rc_int("clientPortEnd", server_cport_end_);
    }
    if (!baseline_ok_)
        process_mgr_.AddLog("LAUNCHER", "Could not read server defaults (" + server_bin_path_ +
                                            " -dump). Option editing is limited.");

    process_mgr_.AddLog("LAUNCHER", "Scanning maps in " + data_dir_ + "/maps ...");
    maps_ = MapScanner::ScanDirectory(data_dir_ + "/maps", renderer_);
    for (auto& m : maps_) {
        std::error_code ec;
        m.full_path = fs::absolute(m.full_path, ec).string();
    }
    process_mgr_.AddLog("LAUNCHER", std::to_string(maps_.size()) + " maps found.");

    if (server_port_ <= 0) server_port_ = OptionsManager::FindFreePort();
}

void LauncherApp::DesiredWindowSize(int* w, int* h) const {
    if (screen_ == Screen::Menu) { *w = 560; *h = 480; }
    else if (screen_ == Screen::NetworkBrowser) { *w = 900; *h = 700; }
    else { *w = 1120; *h = 740; }
}

void LauncherApp::RequestQuit() {
    if (process_mgr_.IsServerRunning() && !local_shutdown_pending_) {
        open_quit_popup_ = true;   // shown once the window is visible again if a game is running
        return;
    }
    quit_ = true;
}

// ============================================================================
// navigation / actions
// ============================================================================

bool LauncherApp::BinariesOk(bool server, bool client) {
    std::string msg;
    if (server) msg = server_problem_;
    if (client && !client_problem_.empty()) msg += (msg.empty() ? "" : "\n") + client_problem_;
    if (msg.empty()) return true;
    banner_ = msg;
    return false;
}

// Client arguments: identity row plus audio edits that are not saved to the xpilotrc yet.
std::vector<std::string> LauncherApp::ClientArgs(int port, const std::string& host) const {
    return profile_.BuildArgs(port, host, profile_editor_.UnsavedAudioArgs());
}

void LauncherApp::GoTo(Screen s) {
    screen_ = s;
    focus_first_ = true;
    if (s == Screen::Menu) {
        banner_.clear();
    }
    if (s == Screen::Profile) profile_editor_.Load(profile_.ship_names, true);   // keeps unsaved audio edits
    if (s == Screen::NetworkBrowser) {
        lan_search_.Start();        // full refresh on every visit (no-op while a scan is still running)
        internet_search_.Start();
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
        if (idx < 0) return;
        // Same map as before: keep the edits made in the GUI.
        if (idx == selected_map_idx_) (s == Screen::ServerSetup ? last_map_server_ : last_map_local_) = maps_[idx].filename;
        else SelectMap(idx);
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

void LauncherApp::JoinRunningServer() {
    banner_.clear();
    if (!BinariesOk(false, true)) return;
    if (!process_mgr_.StartClient(client_bin_path_, ClientArgs(running_port_))) {
        banner_ = "The client could not be started (" + client_bin_path_ + ").";
        return;
    }
    play_kind_ = PlayKind::Network;   // we don't own the shutdown of this server
    GoTo(Screen::Playing);
}

void LauncherApp::StartLocalGame(bool force) {
    if (selected_map_idx_ < 0 || launching_) return;
    if (!force && process_mgr_.IsServerRunning() && !local_shutdown_pending_) {
        open_running_popup_ = true;
        return;
    }
    const MapInfo& map = maps_[selected_map_idx_];

    int port = local_port_ > 0 ? local_port_ : OptionsManager::FindFreePort();
    // our own server on that port is stopped by LaunchLocalGame, so it doesn't count as "in use"
    const bool replaces_own = process_mgr_.IsServerRunning() && port == running_port_;
    if (!replaces_own && !OptionsManager::IsPortFree(port)) {
        banner_ = "Port " + std::to_string(port) + " is already in use. Choose another one under Advanced > Network.";
        return;
    }

    OptionsManager opts = options_mgr_;   // local-only settings stay out of the shared option set
    opts.SetBool("reportToMetaServer", local_meta_);
    auto server_args = opts.BuildServerArgs(LaunchMode::Local, map.full_path, port);

    auto client_args = ClientArgs(port);

    banner_.clear();
    if (!BinariesOk(true, true)) return;
    force_log_open_ = false;
    play_kind_ = PlayKind::Local;
    launching_ = true;
    local_shutdown_pending_ = false;
    running_map_title_ = map.title;
    running_map_file_ = map.filename;
    running_port_ = port;
    server_started_ = std::chrono::steady_clock::now();
    process_mgr_.LaunchLocalGame(server_bin_path_, server_args, client_bin_path_, client_args);
}

// Server mode settings live outside of the per-map option set.
void LauncherApp::ApplyServerModeSettings(OptionsManager& opts) const {
    if (server_max_players_ > 0) opts.SetInt("playerLimit", server_max_players_);
    if (server_greeting_[0]) opts.Set("greeting", server_greeting_);
    opts.SetBool("reportToMetaServer", server_meta_);
    // always set: an empty/zero value must also be able to override defaults.txt
    opts.Set("serverHost", server_host_);
    opts.SetInt("clientPortStart", server_cport_start_);
    opts.SetInt("clientPortEnd", server_cport_end_);
}

std::string LauncherApp::ServerCommandLine() const {
    if (selected_map_idx_ < 0) return {};
    OptionsManager tmp = options_mgr_;   // preview only, don't touch the real option set
    ApplyServerModeSettings(tmp);
    int port = server_port_ > 0 ? server_port_ : kDefaultServerPort;
    auto args = tmp.BuildServerArgs(LaunchMode::Server, maps_[selected_map_idx_].full_path, port, true);
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == "-mapFileName") args[i + 1] = maps_[selected_map_idx_].filename;   // server searches lib/maps itself
    return CommandLine(server_bin_path_, args);
}

void LauncherApp::StartServerMode() {
    if (selected_map_idx_ < 0) return;
    const MapInfo& map = maps_[selected_map_idx_];

    int port = server_port_ > 0 ? server_port_ : OptionsManager::FindFreePort();
    if (!OptionsManager::IsPortFree(port)) {
        banner_ = "Port " + std::to_string(port) + " is already in use.";
        return;
    }

    if ((server_cport_start_ > 0) != (server_cport_end_ > 0) || server_cport_start_ > server_cport_end_ ||
        server_cport_start_ > 65535 || server_cport_end_ > 65535) {
        banner_ = "Invalid client port range: set both ends (start <= end, max 65535) or leave both at 0.";
        return;
    }

    OptionsManager opts = options_mgr_;   // server-only settings must not leak into later local games
    ApplyServerModeSettings(opts);
    auto args = opts.BuildServerArgs(LaunchMode::Server, map.full_path, port);
    banner_.clear();
    if (!BinariesOk(true, false)) return;
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

void LauncherApp::JoinServer(const std::string& host, int port) {
    banner_.clear();
    if (!BinariesOk(false, true)) return;
    if (!process_mgr_.StartClient(client_bin_path_, ClientArgs(port, host))) {
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
        } else if (std::chrono::steady_clock::now() > local_shutdown_deadline_) {
            // Count only after the grace period: our own "[leave]" may still be on its way.
            if (process_mgr_.HumanCount() == 0) {
                process_mgr_.AddLog("LAUNCHER", "Nobody is left on the local server, stopping it.");
                process_mgr_.StopServer();
            }   // else: somebody else is playing, keep the server
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
        case Screen::NetworkBrowser: RenderNetworkBrowser(); break;
        case Screen::Profile:       RenderProfile(); break;
        case Screen::Playing:       break;
    }

    if (open_quit_popup_) {
        ImGui::OpenPopup("Server still running");
        open_quit_popup_ = false;
    }
    RenderQuitPopup();
    if (!banner_.empty()) {   // errors go into their own dialog
        error_text_ = banner_;
        banner_.clear();
        ImGui::OpenPopup("Error");
    }
    RenderErrorPopup();
    if (open_leave_profile_popup_) {
        ImGui::OpenPopup("Unsaved changes");
        open_leave_profile_popup_ = false;
    }
    RenderProfileLeavePopup();
    if (open_running_popup_) {
        ImGui::OpenPopup("Server already running");
        open_running_popup_ = false;
    }
    RenderRunningServerPopup();

    // Esc = back one level
    if (!AnyPopupOpen() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
        !launching_) {
        if (screen_ == Screen::LocalSetup || screen_ == Screen::ServerSetup || screen_ == Screen::ServerRunning ||
            screen_ == Screen::NetworkBrowser || (screen_ == Screen::Profile && !profile_editor_.Capturing()))
        {
            if (screen_ == Screen::Profile) LeaveProfile();
            else GoTo(Screen::Menu);
        }
    }

    ImGui::End();
}

void LauncherApp::RenderHeader(const char* title, bool show_back) {
    if (show_back) {
        const char* back = (screen_ == Screen::ServerRunning) ? "< Menu" : "< Back";
        if (ImGui::Button(back, ImVec2(80, 0)) && !launching_) GoTo(Screen::Menu);
        ImGui::SameLine();
    }
    // centered in the window, independent of the back button
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(title).x) * 0.5f);
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
    const bool server_active = process_mgr_.IsServerRunning() && !local_shutdown_pending_;
    char server_sub[256] = "";
    if (server_active) {
        int humans = process_mgr_.HumanCount();
        snprintf(server_sub, sizeof server_sub, "Server %s  -  %s  -  %d player%s",
                 process_mgr_.IsServerReady() ? "running" : "starting",
                 running_map_file_.c_str(), humans, humans == 1 ? "" : "s");
    }
    bool server = server_active
        ? menu_button("server", "Manage Running Server", server_sub)
        : menu_button("server", "Launch Server", "Host a game for other players");
    bool quit = menu_button("quit", "Quit", "");

    local |= HotkeyPressed(ImGuiKey_L);
    join |= HotkeyPressed(ImGuiKey_J);
    server |= HotkeyPressed(ImGuiKey_S);
    quit |= HotkeyPressed(ImGuiKey_Q);

    // footer
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 32);
    ImGui::Separator();
    std::string ver = std::string("v") + XPILOT_TNG_VERSION;
    float vx = (ImGui::GetWindowWidth() - ImGui::CalcTextSize(ver.c_str()).x) * 0.5f;
    ImGui::SetCursorPosX(vx);
    ImGui::TextDisabled("%s", ver.c_str());

    if (local) GoTo(Screen::LocalSetup);
    else if (join) { banner_.clear(); GoTo(Screen::NetworkBrowser); }
    else if (server) GoTo(server_active ? Screen::ServerRunning : Screen::ServerSetup);
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
    if (!map.description.empty()) ImGui::TextDisabled("%s", map.description.c_str());
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

// AIs for smart robots, see smart_bot_types[] in the server (robot.c). Add new ones in both places.
static const NamedChoice kSmartBotTypes[] = {
    {"auto", "Automatic", "The best AI for the map: ball game bot on team maps with treasures, race bot on race maps, smartbot elsewhere"},
    {"smart", "Smartbot", "The learning smartbot"},
    {"ball", "Ball bot", "Planning bot for team ball games. On other maps it only dogfights"},
    {"race", "Race bot", "Planning bot for race maps (timing mode). Elsewhere the smartbot plays"},
};

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

// Integer option with named values (0, 1, 2, ...)
bool LauncherApp::OptChoice(const char* label, const char* key, const char* const* items, int count,
                            const char* tip) {
    if (!options_mgr_.Known(key)) return false;
    int v = std::clamp(options_mgr_.GetInt(key), 0, count - 1);
    bool changed = ImGui::Combo(OptLabel(label, key, options_mgr_.IsModified(key)).c_str(), &v, items, count);
    if (changed) options_mgr_.SetInt(key, v);
    ModifiedMenu(key);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return changed;
}

// String option with a fixed list of values, shown as a dropdown (no label text, put it next to
// another widget). Unknown values from defaults or the map are shown as they are.
bool LauncherApp::OptNamed(const char* label, const char* key, const NamedChoice* items, int count) {
    if (!options_mgr_.Known(key)) return false;
    std::string cur = options_mgr_.Get(key);
    int sel = -1;
    for (int i = 0; i < count; i++) {
        if (strcasecmp(cur.c_str(), items[i].value) == 0) sel = i;
    }
    const char* preview = sel >= 0 ? items[sel].label : cur.c_str();
    bool changed = false;
    ImGui::PushID(key);
    if (ImGui::BeginCombo(OptLabel(label, key, options_mgr_.IsModified(key)).c_str(), preview)) {
        for (int i = 0; i < count; i++) {
            if (ImGui::Selectable(items[i].label, i == sel)) {
                options_mgr_.Set(key, items[i].value);
                changed = true;
            }
            if (items[i].tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", items[i].tip);
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    ModifiedMenu(key);
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

void LauncherApp::RenderIdentityRow() {
    ImGui::SetNextItemWidth(150);
    ImGui::InputTextWithHint("Name", "(account name)", profile_.name, sizeof(profile_.name));
    ImGui::SameLine(0, 24);
    ImGui::SetNextItemWidth(150);
    auto& ships = profile_.ship_names;
    int& choice = profile_.ship_choice;
    // -1 passes no ship: the client then uses ~/.xpilotrc or, without it, its built-in default ship
    const char* none_label = profile_.rc_has_ship ? "(from .xpilotrc)" : "(client default)";
    const char* ship_label = choice >= 0 && choice < int(ships.size()) ? ships[choice].c_str() : none_label;
    if (ImGui::BeginCombo("Ship shape", ship_label)) {
        if (ImGui::Selectable(none_label, choice < 0)) choice = -1;
        for (int i = 0; i < int(ships.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(ships[i].c_str(), choice == i)) choice = i;
            if (choice == i && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 24);
    ImGui::Checkbox("Newbie mode", &profile_.newbie);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show help messages for new players");

    // small profile editor button, right-aligned
    const float bw = ImGui::CalcTextSize("Profile...").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw);
    if (ImGui::Button("Profile...") && !launching_) {
        profile_return_ = screen_;
        GoTo(Screen::Profile);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Edit keys, mouse buttons, modifier banks, audio and ship in the xpilotrc");
}

void LauncherApp::RenderQuickSettings(bool server_mode) {
    ImGui::SetNextItemOpen(quick_open_, ImGuiCond_Always);
    quick_open_ = ImGui::CollapsingHeader("Quick settings");
    if (!quick_open_) return;

    ImGui::PushID("quick");

    // mode specific row
    ImGui::PushItemWidth(190);
    if (!server_mode) {
        RenderIdentityRow();
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
                ImGui::SameLine(0, 16);
                ImGui::SetNextItemWidth(150);
                OptNamed("Type", "smartBotType", kSmartBotTypes, IM_ARRAYSIZE(kSmartBotTypes));
                OptInt("Smartbot level", "smartRobotLevel", 1, 100, "Skill of the learning bots (1-100)");
                OptInt("Bot team", "robotTeam", 0, 9);
                ImGui::PopItemWidth();
                OptBool("All bots in one team", "restrictRobots");
                OptBool("No humans in bot team", "reserveRobotTeam", "Humans cannot join the bot team");
                ImGui::Separator();
                if (OptBool("Bots match humans", "botsMatchHumans",
                            "One bot per human player. Bots / Smartbots then only set the normal-to-smart ratio")
                    && options_mgr_.GetBool("botsMatchHumans"))
                    options_mgr_.SetBool("robotsFillTo", false);
                ImGui::SameLine(0, 16);
                if (OptBool("Humans replace bots", "robotsFillTo",
                            "Fill the game up to Bots + Smartbots players: each human replaces a bot")
                    && options_mgr_.GetBool("robotsFillTo"))
                    options_mgr_.SetBool("botsMatchHumans", false);
                ImGui::PushItemWidth(260);
                OptFloat("Bots per human", "botsPerHuman", 0.25f, 4.0f, "%.2f", false,
                         "Used when 'Bots match humans' is on");
                ImGui::PopItemWidth();
                OptBool("Smartbots may use lasers", "smartRobotsLaser", "Allow smart robots to collect and fire lasers");
                OptBool("Bots talk", "robotsTalk");
                OptBool("Bots may leave", "robotsLeave");
                ImGui::EndTabItem();
            }
        }
        RenderVoiceTab();
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
                } else if (options_mgr_.IsBool(canon)) {
                    bool b = options_mgr_.GetBool(canon);
                    if (ImGui::Checkbox("##v", &b)) options_mgr_.SetBool(canon, b);
                } else if (ImGui::InputText("##v", buf, sizeof(buf))) {
                    options_mgr_.Set(canon, buf);
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Network")) {
            ImGui::Spacing();
            if (server_mode) {
                ImGui::SetNextItemWidth(300);
                ImGui::InputTextWithHint("Public host name", "(automatic)", server_host_, sizeof(server_host_));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Name reported to the meta server (serverHost).\n"
                                      "Set this to your external host name when you are behind a router.");
                ImGui::SetNextItemWidth(110);
                ImGui::InputInt("Client ports from", &server_cport_start_, 0, 0);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110);
                ImGui::InputInt("to", &server_cport_end_, 0, 0);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("UDP port range for the connections of players (clientPortStart/End).\n"
                                      "0 = random ports, which cannot be forwarded by a router.");
                server_cport_start_ = std::clamp(server_cport_start_, 0, 65535);
                server_cport_end_ = std::clamp(server_cport_end_, 0, 65535);
                const int port = server_port_ > 0 ? server_port_ : kDefaultServerPort;
                if (server_cport_start_ > 0 && server_cport_end_ >= server_cport_start_)
                    ImGui::TextDisabled("Forward UDP %d and UDP %d-%d to this machine.", port, server_cport_start_,
                                        server_cport_end_);
                else if (server_cport_start_ > 0 || server_cport_end_ > 0)
                    ImGui::TextColored(kWarn, "Set both ends of the port range (start <= end).");
                else
                    ImGui::TextDisabled("Behind a router, set a port range and forward it (see doc/HOWTO-INTERNET-SERVER.md).");
            } else {
                ImGui::SetNextItemWidth(140);
                ImGui::InputInt("Server port (0 = automatic)", &local_port_, 0, 0);
                if (local_port_ < 0) local_port_ = 0;
                ImGui::TextDisabled("A free port is chosen automatically when this is 0.");
                ImGui::Spacing();
                ImGui::Checkbox("Report to meta server", &local_meta_);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lists this game in the public server list (off by default).");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopID();
}

// Voice chat: server options. The player's own settings are in the profile editor (Audio).
void LauncherApp::RenderVoiceTab() {
    static const char* kFilters[] = {"Unfiltered", "Radio", "Telephone"};

    if (!ImGui::BeginTabItem("Audio")) return;
    ImGui::Spacing();
    if (baseline_ok_ && options_mgr_.Known("voiceChat")) {
        OptBool("Allow voice chat", "voiceChat", "Players with a headset can talk to each other");
        ImGui::BeginDisabled(!options_mgr_.GetBool("voiceChat"));
        OptBool("Team channel only", "voiceTeamOnly", "Disable the channel that enemies nearby hear as well");
        ImGui::PushItemWidth(260);
        OptFloat("Range, all channel", "voiceRange", 0.0f, 5000.0f, "%.0f px", false,
                 "Distance at which voices fade out completely.\n0 = unlimited range at full volume");
        OptFloat("Range, team channel", "voiceTeamRange", 0.0f, 5000.0f, "%.0f px", false,
                 "0 = like a radio: full volume at any distance");
        ImGui::Separator();
        OptInt("Bitrate", "voiceBitrate", 6, 64,
               "Opus bitrate in kbit/s for everybody's voice.\nHigher is clearer but costs bandwidth.");
        OptChoice("Team channel sound", "voiceTeamFilter", kFilters, IM_ARRAYSIZE(kFilters),
                  "How the team channel sounds on all clients");
        OptChoice("All channel sound", "voiceAllFilter", kFilters, IM_ARRAYSIZE(kFilters),
                  "How the all channel sounds on all clients");
        ImGui::PopItemWidth();
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("This server build does not support voice chat.");
    }
    ImGui::TextDisabled("Your own microphone and volume settings: Profile... > Audio.");
    ImGui::EndTabItem();
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
    const auto& all_logs = process_mgr_.GetLogs();
    const char* want_tag = log_filter_type_ == 1 ? "SERVER" : log_filter_type_ == 2 ? "CLIENT"
                         : log_filter_type_ == 3 ? "LAUNCHER" : nullptr;
    std::vector<const ProcessLogEntry*> shown;
    shown.reserve(all_logs.size());
    for (const auto& e : all_logs) {
        if (!want_tag || e.tag == want_tag) shown.push_back(&e);
    }
    ImGuiListClipper clipper;   // only lay out the visible lines (log holds up to 2000)
    clipper.Begin((int)shown.size());
    while (clipper.Step())
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        const auto& e = *shown[i];
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
    // Bottom padding so the last line isn't clipped by the border/h-scrollbar
    ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y + 2.0f));
    if (auto_scroll_logs_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void LauncherApp::RenderActionBar(bool server_mode) {
    ImGui::Separator();
    ImGui::Spacing();

    if (server_mode) {
        // read-only InputText so the command can be selected and copied
        std::string cmd = ServerCommandLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Command");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-70);
        ImGui::InputText("##servercmd", cmd.data(), cmd.size() + 1, ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::Button("Copy")) ImGui::SetClipboardText(cmd.c_str());
        ImGui::Spacing();
    }

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
    const char* label = server_mode ? "LAUNCH SERVER" : (busy ? "STARTING..." : "PLAY");
    bool go = GreenButton(label, ImVec2(bw, bh));
    ImGui::EndDisabled();

    if (go) {
        if (server_mode) StartServerMode();
        else StartLocalGame();
    }
}

void LauncherApp::RenderProfile() {
    if (ImGui::Button("< Back", ImVec2(80, 0))) LeaveProfile();
    ImGui::SameLine();
    RenderHeader("EDIT PLAYER PROFILE", false);
    if (profile_editor_.Render(40))
        profile_.Load((data_dir_ + "/shipshapes.txt").c_str());   // pick up the saved name and ship
}

void LauncherApp::LeaveProfile() {
    if (profile_editor_.Dirty()) open_leave_profile_popup_ = true;
    else GoTo(profile_return_);
}

void LauncherApp::RenderSetup(bool server_mode) {
    RenderHeader(server_mode ? "LAUNCH SERVER" : "START LOCAL GAME", true);

    const float bar_h = server_mode ? 96 : 60;
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
// network browser
// ============================================================================

void LauncherApp::RenderServerList(const char* id, ServerSearch& search, const bool* show_unreachable) {
    std::vector<ServerEntry> servers = search.Snapshot();
    const std::string msg = search.Message();
    if (show_unreachable && !*show_unreachable)
        servers.erase(std::remove_if(servers.begin(), servers.end(), [](const ServerEntry& e) { return e.ping_ms < 0; }),
                      servers.end());

    // Internet list: last line counts the servers that did not answer (not while still measuring)
    const bool footer = show_unreachable && !search.Busy();
    auto draw_footer = [&] {
        if (!footer) return;
        const int n = search.UnreachableTotal();
        ImGui::TextDisabled("%d unreachable server%s found", n, n == 1 ? "" : "s");
    };

    if (search.Busy()) {
        ImGui::TextDisabled("%s", msg.c_str());
    } else if (servers.empty()) {
        ImGui::TextColored(kWarn, "%s", msg.c_str());
        draw_footer();
        return;
    }
    if (servers.empty()) return;

    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    const float footer_h = footer ? ImGui::GetTextLineHeightWithSpacing() : 0.0f;
    if (!ImGui::BeginTable(id, 7, tf, ImVec2(0, ImGui::GetContentRegionAvail().y - footer_h))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Server", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 72);
    ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 64);
    ImGui::TableSetupColumn("Teams", ImGuiTableColumnFlags_WidthFixed, 54);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 56);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < servers.size(); ++i) {
        const ServerEntry& e = servers[i];
        ImGui::PushID(int(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        const std::string& addr = e.ip.empty() ? e.host : e.ip;
        const bool selected = (addr == sel_ip_ && e.port == sel_port_);
        // selected row: strong accent fill instead of the subtle theme header colour
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.45f, 0.78f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.52f, 0.88f, 0.95f));
        }
        const bool clicked = ImGui::Selectable("##row", selected,
                                               ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                                   ImGuiSelectableFlags_AllowDoubleClick);
        if (selected) ImGui::PopStyleColor(2);
        if (clicked) {
            sel_ip_ = addr;
            sel_host_ = e.host.empty() ? addr : e.host;
            sel_port_ = e.port;
            custom_selected_ = false;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !launching_) PlaySelectedNetwork();
        if (ImGui::IsItemHovered() && !e.author.empty()) ImGui::SetTooltip("Map by %s", e.author.c_str());
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(e.host.c_str());
        if (e.port != kDefaultServerPort) { ImGui::SameLine(0, 0); ImGui::TextDisabled(":%d", e.port); }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(e.map.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(e.version.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d/%d", e.players, e.bases);
        ImGui::TableNextColumn();
        if (e.teambases > 0) ImGui::Text("%d", e.teambases);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(e.status.c_str());
        ImGui::TableNextColumn();
        if (e.ping_ms < 0) ImGui::TextDisabled("-");
        else ImGui::TextColored(e.ping_ms < 80 ? kGood : (e.ping_ms < 250 ? kWarn : kBad), "%d", e.ping_ms);
        ImGui::PopID();
    }
    ImGui::EndTable();
    draw_footer();
}

void LauncherApp::RenderNetworkBrowser() {
    RenderHeader("JOIN NETWORK GAME", true);

    RenderIdentityRow();
    ImGui::Spacing();

    // boxes fit their content (message line or table) up to a maximum number of server rows, then scroll
    const ImGuiStyle& st = ImGui::GetStyle();
    const float row_h = ImGui::GetFrameHeight() + st.CellPadding.y * 2;
    auto box_height = [&](const ServerSearch& search, size_t max_rows) {
        const size_t n = search.Snapshot().size();
        const float content = n == 0 ? ImGui::GetTextLineHeight() : row_h * float(std::min(n, max_rows) + 1);
        return st.WindowPadding.y * 2 + 2 + ImGui::GetTextLineHeight() + st.ItemSpacing.y * 3 + 1 + content;
    };
    const float lan_h = box_height(lan_search_, 4);
    const float custom_h = st.WindowPadding.y * 2 + 2 + ImGui::GetTextLineHeight() + st.ItemSpacing.y * 3 + 1 +
                           ImGui::GetFrameHeight();
    auto section = [&](const char* title, const char* id, ServerSearch& search, float height, bool* show_unreachable = nullptr) {
        ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_Borders);
        ImGui::TextColored(kAccent, "%s", title);
        if (show_unreachable) {
            ImGui::SameLine(ImGui::GetWindowWidth() - 100 - ImGui::GetFrameHeight() - st.ItemInnerSpacing.x -
                            ImGui::CalcTextSize("Show unreachable").x - st.ItemSpacing.x);
            ImGui::Checkbox("Show unreachable", show_unreachable);
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
        ImGui::BeginDisabled(search.Busy());
        if (ImGui::SmallButton("Refresh")) search.Start();
        ImGui::EndDisabled();
        ImGui::Separator();
        RenderServerList(id, search, show_unreachable);
        ImGui::EndChild();
    };
    section("Local network", "lan", lan_search_, lan_h);
    // Internet box: fixed size, fills everything down to the custom address / command rows and the bottom-anchored PLAY
    const float play_top = ImGui::GetWindowHeight() - 38 - 10;
    const float below_h = custom_h + st.ItemSpacing.y * 8 + 1 + ImGui::GetFrameHeight();
    const float net_h = std::max(box_height(internet_search_, 3), play_top - ImGui::GetCursorPosY() - below_h);
    section("Internet", "internet", internet_search_, net_h, &show_unreachable_);

    // custom address: a box like the server lists, highlighted while it is the active choice
    bool enter = false;
    {
        ImGui::PushStyleColor(ImGuiCol_Border, custom_selected_ ? kAccent : st.Colors[ImGuiCol_Border]);
        ImGui::BeginChild("custom", ImVec2(0, custom_h), ImGuiChildFlags_Borders);
        ImGui::PopStyleColor();
        ImGui::TextColored(kAccent, "Custom address");
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1);
        enter = ImGui::InputTextWithHint("##manual", "host or host:port", manual_addr_, sizeof(manual_addr_),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemActive() || ImGui::IsItemEdited()) {   // using the field selects it
            custom_selected_ = true;
            sel_ip_.clear();
            sel_port_ = 0;
        }
        ImGui::EndChild();
    }

    ImGui::Separator();
    ImGui::Spacing();
    // read-only InputText so the command can be selected and copied
    std::string target_host;
    int target_port = 0;
    const bool have_target = NetworkTarget(&target_host, &target_port);
    std::string cmd = have_target ? ClientCommandLine(target_host, target_port) : std::string();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Command");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-70);
    ImGui::InputText("##clientcmd", cmd.data(), cmd.size() + 1, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    ImGui::BeginDisabled(!have_target);
    if (ImGui::Button("Copy")) ImGui::SetClipboardText(cmd.c_str());
    ImGui::EndDisabled();
    ImGui::Spacing();
    const float bw = 240, bh = 38;
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - bw - 14, ImGui::GetWindowHeight() - bh - 10));
    const bool can_play = have_target;
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 0.85f);   // stay close to the other green buttons while no server is chosen
    ImGui::BeginDisabled(!can_play);
    bool play = GreenButton("PLAY", ImVec2(bw, bh));
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    if ((play || enter) && can_play) PlaySelectedNetwork();
}

// Host/port of the current choice (selected row or custom address)
bool LauncherApp::NetworkTarget(std::string* host, int* port) const {
    if (!custom_selected_ && !sel_ip_.empty()) {
        // meta server data is untrusted: a name starting with '-' would be read as a client option
        *host = (sel_host_.empty() || sel_host_[0] == '-') ? sel_ip_ : sel_host_;
        *port = sel_port_;
    } else {
        *host = manual_addr_;
        *port = kDefaultServerPort;
        size_t colon = host->rfind(':');
        if (colon != std::string::npos && host->find(':') == colon) {
            *port = std::atoi(host->c_str() + colon + 1);
            host->resize(colon);
        }
    }
    return !host->empty() && (*host)[0] != '-' && *port > 0 && *port < 65536;
}

std::string LauncherApp::ClientCommandLine(const std::string& host, int port) const {
    return CommandLine(client_bin_path_, ClientArgs(port, host));
}

void LauncherApp::PlaySelectedNetwork() {
    std::string host;
    int port = 0;
    if (NetworkTarget(&host, &port)) JoinServer(host, port);
}

// ============================================================================
// server running
// ============================================================================

void LauncherApp::RenderServerRunning() {
    RenderHeader("SERVER", true);

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

void LauncherApp::RenderRunningServerPopup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Server already running", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    int humans = process_mgr_.HumanCount();
    ImGui::Text("A server is already running on port %d (%d player%s connected).", running_port_, humans,
                humans == 1 ? "" : "s");
    if (!running_map_title_.empty()) ImGui::Text("Map: %s", running_map_title_.c_str());
    ImGui::Text("Relaunching will stop it and disconnect everybody.");
    ImGui::Spacing();
    if (ImGui::Button("Join current game", ImVec2(160, 0))) {
        ImGui::CloseCurrentPopup();
        JoinRunningServer();
    }
    ImGui::SameLine();
    if (ImGui::Button("Relaunch new game", ImVec2(160, 0))) {
        ImGui::CloseCurrentPopup();
        StartLocalGame(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void LauncherApp::RenderErrorPopup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 0), ImVec2(560, 400));
    if (!ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(520.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kBad);
    ImGui::TextUnformatted(error_text_.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void LauncherApp::RenderProfileLeavePopup() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("The player profile has unsaved changes.");
    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(110, 0))) {
        if (profile_editor_.Save()) {
            profile_.Load((data_dir_ + "/shipshapes.txt").c_str());
            ImGui::CloseCurrentPopup();
            GoTo(profile_return_);
        } else {
            ImGui::CloseCurrentPopup();   // the editor shows the error; stay on the profile screen
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
        GoTo(profile_return_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

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
