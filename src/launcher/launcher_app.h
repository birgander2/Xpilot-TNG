#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "client_profile.h"
#include "map_preview.h"
#include "options_model.h"
#include "process_manager.h"
#include "server_browser.h"

enum class Screen { Menu, LocalSetup, ServerSetup, ServerRunning, NetworkBrowser, Playing };

struct ImFont;

struct LauncherFonts {
    ImFont* regular = nullptr;
    ImFont* heading = nullptr;
    ImFont* title = nullptr;
    ImFont* subtitle = nullptr;
};

class LauncherApp {
public:
    LauncherApp(SDL_Window* window, SDL_Renderer* renderer, const std::string& app_dir);
    ~LauncherApp();

    void SetFonts(const LauncherFonts& fonts) { fonts_ = fonts; }
    void Init();
    void Update();
    void RenderUI();

    // Window integration (used by main loop)
    void DesiredWindowSize(int* w, int* h) const;
    bool WindowHidden() const { return screen_ == Screen::Playing; }
    void RequestQuit();                    // window close button / SDL_QUIT
    bool WantsQuit() const { return quit_; }

private:
    enum class PlayKind { None, Local, Network };

    // screens
    void RenderMenu();
    void RenderSetup(bool server_mode);
    void RenderServerRunning();
    void RenderNetworkBrowser();
    void RenderServerList(const char* id, ServerSearch& search);
    void RenderIdentityRow();
    void RenderHeader(const char* title, bool show_back);
    void RenderBanner();
    void RenderQuitPopup();

    // setup building blocks
    void RenderMapPanel(bool with_list);
    void RenderMapCard(const MapInfo& map);
    void RenderQuickSettings(bool server_mode);
    void RenderAdvanced(bool server_mode);
    void RenderLog(float height);
    void RenderActionBar(bool server_mode);

    // option widgets bound to OptionsManager
    bool OptBool(const char* label, const char* key, const char* tip = nullptr);
    bool OptInt(const char* label, const char* key, int lo, int hi, const char* tip = nullptr);
    bool OptFloat(const char* label, const char* key, float lo, float hi, const char* fmt,
                  bool logarithmic = false, const char* tip = nullptr);
    void ModifiedMenu(const char* key);

    // actions
    void GoTo(Screen s);
    void SelectMap(int idx);
    void StartLocalGame(bool force = false);
    void JoinRunningServer();
    void RenderRunningServerPopup();
    void StartServerMode();
    void ApplyServerModeSettings(OptionsManager& opts) const;
    std::string ServerCommandLine() const;
    void JoinServer(const std::string& host, int port);
    void PlaySelectedNetwork();
    bool NetworkTarget(std::string* host, int* port) const;
    std::string ClientCommandLine(const std::string& host, int port) const;
    void StopEverything();
    void FinishPlaying(int client_exit_code);
    std::string FindRoot() const;
    std::string ResolveBinaryPath(const std::string& bin_name) const;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::string app_dir_;
    std::string root_dir_;

    OptionsManager options_mgr_;
    ProcessManager process_mgr_;

    std::vector<MapInfo> maps_;
    int selected_map_idx_ = -1;
    char map_filter_[128] = "";

    std::string server_bin_path_;
    std::string client_bin_path_;
    bool baseline_ok_ = false;

    Screen screen_ = Screen::Menu;
    PlayKind play_kind_ = PlayKind::None;
    bool launching_ = false;          // local game: waiting for server / client
    bool quit_ = false;
    bool open_running_popup_ = false;
    bool open_quit_popup_ = false;
    bool focus_first_ = true;
    std::string banner_;

    // per-mode remembered map (file names)
    std::string last_map_local_, last_map_server_;

    // client session overrides (empty/auto = use ~/.xpilotrc)
    ClientProfile profile_;
    ServerSearch lan_search_{ServerSearch::Kind::Lan};
    ServerSearch internet_search_{ServerSearch::Kind::Internet};
    char manual_addr_[128] = "";
    std::string sel_ip_;              // selected server row (ip + port), empty = none
    std::string sel_host_;            // name to connect with (hostname if the server has one)
    int sel_port_ = 0;
    bool custom_selected_ = false;    // the custom address box is the active choice
    int local_port_ = 0;              // 0 = pick a free port automatically
    bool local_meta_ = false;         // list the local game at the meta server

    // server mode settings (kept across map changes, persisted)
    int server_port_ = 0;
    int server_max_players_ = 0;
    char server_greeting_[128] = "";
    bool server_meta_ = true;
    std::string running_map_title_;
    std::string running_map_file_;
    int running_port_ = 0;
    std::chrono::steady_clock::time_point server_started_;

    // local game shutdown (server quits when nobody is left)
    bool local_shutdown_pending_ = false;
    std::chrono::steady_clock::time_point local_shutdown_deadline_;

    int log_filter_type_ = 0;
    bool auto_scroll_logs_ = true;
    bool quick_open_ = true;
    bool advanced_open_ = false;
    bool log_open_local_ = false;
    bool log_open_server_ = true;
    bool force_log_open_ = false;

    LauncherFonts fonts_;
};
