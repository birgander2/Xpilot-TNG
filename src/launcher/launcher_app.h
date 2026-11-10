#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "ship_shape.h"
#include "client_profile.h"
#include "profile_editor.h"
#include "map_preview.h"
#include "options_model.h"
#include "process_manager.h"
#include "server_browser.h"

enum class Screen { Menu, LocalSetup, ServerSetup, ServerRunning, NetworkBrowser, Profile, Playing };

struct ImFont;

struct LauncherFonts {
    ImFont* regular = nullptr;
    ImFont* heading = nullptr;
    ImFont* title = nullptr;
    ImFont* subtitle = nullptr;
};

// One value of a string option shown as a dropdown.
struct NamedChoice {
    const char* value;   // what the server option is set to
    const char* label;   // what the dropdown shows
    const char* tip;     // tooltip, may be null
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
    bool WindowResizable() const { return screen_ != Screen::Menu; }   // welcome screen has a fixed size
    bool WindowHidden() const { return screen_ == Screen::Playing; }
    void RequestQuit();                    // window close button / SDL_QUIT
    bool WantsAnimation() const { return screen_ == Screen::Menu; }   // welcome screen has a moving background
    bool WantsQuit() const { return quit_; }

private:
    enum class PlayKind { None, Local, Network };

    // screens
    void RenderMenu();
    void RenderSetup(bool server_mode);
    void RenderServerRunning();
    void RenderNetworkBrowser();
    void RenderProfile();
    void RenderServerList(const char* id, ServerSearch& search, const bool* show_unreachable = nullptr);
    void RenderIdentityRow();
    std::vector<std::string> ClientArgs(int port, const std::string& host = "localhost") const;
    void RenderHeader(const char* title, bool show_back);
    void RenderErrorPopup();
    void RenderQuitPopup();
    void RenderProfileLeavePopup();
    void LeaveProfile();   // back to profile_return_; asks first when there are unsaved edits

    // setup building blocks
    void RenderMapPanel(bool with_list);
    void RenderMapCard(const MapInfo& map);
    void RenderQuickSettings(bool server_mode);
    void RenderAdvanced(bool server_mode);
    void RenderVoiceTab();
    void RenderLog(float height);
    void RenderActionBar(bool server_mode);

    // option widgets bound to OptionsManager
    bool OptBool(const char* label, const char* key, const char* tip = nullptr);
    bool OptInt(const char* label, const char* key, int lo, int hi, const char* tip = nullptr);
    bool OptChoice(const char* label, const char* key, const char* const* items, int count, const char* tip = nullptr);
    bool OptNamed(const char* label, const char* key, const NamedChoice* items, int count);
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
    SDL_Texture* logo_tex_ = nullptr;   // lib/textures/logo_tng2.ppm, dark background keyed out
    int logo_w_ = 0, logo_h_ = 0;
    void LoadLogo();
    SDL_Texture* wall_tex_[2] = {nullptr, nullptr};   // lib/textures/launcher_wall_*.pam: pre-rendered FX walls for the top corners
    int wall_w_ = 0, wall_h_ = 0;
    ss::ShipShape menu_ship_;                          // the profile's ship, shown rotating in front of the logo's "P"
    SDL_Texture* ship_dome_tex_ = nullptr;             // hull light and highlight, same as the client's ship FX
    SDL_Texture* ship_spec_tex_ = nullptr;
    void MakeShipTextures();
    std::string menu_name_;                            // player name from the xpilotrc, empty if not set
    void LoadMenuShip();
    void DrawMenuDecor(ImVec2 win_pos, float win_w) const;
    void DrawMenuShip(ImVec2 center) const;   // the profile's ship, rotating
    float MenuTileWidth() const;   // width of one welcome-screen tile: just wide enough for its text
    void DrawStarfield(ImVec2 mn, ImVec2 mx) const;
    std::string data_dir_;   // game data (lib/): $XPILOT_DATADIR, root_dir_/lib or CONF_DATADIR, no trailing slash

    OptionsManager options_mgr_;
    ProcessManager process_mgr_;

    std::vector<MapInfo> maps_;
    int selected_map_idx_ = -1;
    char map_filter_[128] = "";

    std::string server_bin_path_;
    std::string client_bin_path_;
    std::string ship_editor_bin_path_;
    std::string mapeditor_bin_path_;
    bool mapeditor_available_ = false;     // xpilot-tng-mapeditor found next to the launcher
    bool ship_editor_available_ = false;   // xpilot-tng-shipeditor found next to the launcher
    std::string server_problem_;   // non-empty: binary missing or shared library not found
    std::string client_problem_;
    bool BinariesOk(bool server, bool client);   // sets banner_ and returns false on problems
    bool baseline_ok_ = false;

    Screen screen_ = Screen::Menu;
    Screen profile_return_ = Screen::Menu;   // screen the profile editor was opened from
    PlayKind play_kind_ = PlayKind::None;
    bool launching_ = false;          // local game: waiting for server / client
    bool quit_ = false;
    bool open_running_popup_ = false;
    bool open_quit_popup_ = false;
    bool open_about_popup_ = false;
    void RenderAboutPopup();
    bool open_leave_profile_popup_ = false;
    bool focus_first_ = true;
    std::string banner_;        // pending error message; shown once in the "Error" dialog
    std::string error_text_;

    // per-mode remembered map (file names)
    std::string last_map_local_, last_map_server_;

    // client session overrides (empty/auto = use ~/.xpilotrc)
    ClientProfile profile_;
    ProfileEditor profile_editor_;
    ServerSearch lan_search_{ServerSearch::Kind::Lan};
    ServerSearch internet_search_{ServerSearch::Kind::Internet};
    char manual_addr_[128] = "";
    std::string sel_ip_;              // selected server row (ip + port), empty = none
    std::string sel_host_;            // name to connect with (hostname if the server has one)
    int sel_port_ = 0;
    bool show_unreachable_ = false;   // Internet list: also show servers that did not answer the ping
    bool custom_selected_ = false;    // the custom address box is the active choice
    int local_port_ = 0;              // 0 = pick a free port automatically
    bool local_highscores_ = false;   // keep and show highscores in the local game
    bool local_meta_ = false;         // list the local game at the meta server

    // server mode settings (kept across map changes, persisted)
    int server_port_ = 0;
    int server_max_players_ = 0;
    char server_greeting_[128] = "";
    bool server_meta_ = true;
    bool server_highscores_ = true;
    char server_host_[128] = "";      // serverHost: public name reported to the meta server
    int server_cport_start_ = 0;      // clientPortStart/End: UDP port range per player (0 = random)
    int server_cport_end_ = 0;
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
