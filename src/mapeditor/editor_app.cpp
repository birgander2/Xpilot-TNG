#include "editor_app.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "map_io.h"

namespace fs = std::filesystem;
using namespace xpe;

namespace {

constexpr int kDefaultServerPort = 15345;
constexpr size_t kMaxUndo = 200;
constexpr size_t kMaxRecent = 10;

bool IsPortFree(int port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    int res = bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    close(sock);
    return res == 0;
}

int FindFreePort() {
    for (int p = kDefaultServerPort; p < kDefaultServerPort + 100; ++p)
        if (IsPortFree(p)) return p;
    return kDefaultServerPort;
}

std::vector<std::string> ListFiles(const std::string& dir, std::initializer_list<const char*> exts) {
    std::vector<std::string> out;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string ext = Lower(e.path().extension().string());
        if (ext == ".gz") ext = Lower(e.path().stem().extension().string());
        for (const char* x : exts)
            if (ext == x) out.push_back(e.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

// ============================================================================
// lifecycle
// ============================================================================

EditorApp::EditorApp(SDL_Window* window, SDL_Renderer* renderer, const std::string& app_dir)
    : window_(window), renderer_(renderer), app_dir_(app_dir) {
    for (int& t : place_teams_) t = kNoTeam;
    place_teams_[int(Kind::Base)] = 0;
    place_teams_[int(Kind::Ball)] = 0;
}

EditorApp::~EditorApp() {
    StopTestPlay();
}

std::string EditorApp::FindRoot() const {
    std::vector<fs::path> candidates = {fs::current_path(), fs::path(app_dir_), fs::path(app_dir_) / "..",
                                        fs::path(app_dir_) / "../.."};
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / "lib" / "maps", ec)) return fs::weakly_canonical(c, ec).string();
    }
    return "";
}

std::string EditorApp::ResolveBinary(const std::string& name) const {
    std::vector<std::string> candidates = {root_dir_ + "/" + name,  root_dir_ + "/build/" + name,
                                           app_dir_ + "/" + name,   app_dir_ + "/../" + name,
                                           "/usr/local/bin/" + name, "/usr/bin/" + name};
    for (const auto& path : candidates) {
        std::error_code ec;
        if (fs::exists(path, ec) && fs::is_regular_file(path, ec)) return fs::canonical(path, ec).string();
    }
    return name;    // let execvp search PATH
}

void EditorApp::Init(const std::string& open_path) {
    std::string abs_open;
    if (!open_path.empty()) {
        std::error_code ec;
        abs_open = fs::absolute(open_path, ec).string();
    }
    root_dir_ = FindRoot();
    if (!root_dir_.empty()) {
        std::error_code ec;
        fs::current_path(root_dir_, ec);    // the server resolves lib/ relative to its cwd
    }
    std::string data_dir;
    const char* env_dir = std::getenv("XPILOT_DATADIR");
    std::error_code ec;
    if (env_dir && *env_dir && fs::is_directory(env_dir, ec)) data_dir = env_dir;
    else if (!root_dir_.empty()) data_dir = root_dir_ + "/lib";
    else data_dir = CONF_DATADIR;
    while (data_dir.size() > 1 && data_dir.back() == '/') data_dir.pop_back();
    maps_dir_ = data_dir + "/maps";
    textures_dir_ = data_dir + "/textures";
    texture_files_ = ListFiles(textures_dir_, {".ppm", ".pnm", ".xpm", ".png"});

    server_bin_ = ResolveBinary("xpilot-tng-server");
    client_bin_ = ResolveBinary("xpilot-tng-client");
    catalog_ok_ = catalog_.Load(server_bin_);

    LoadRecent();
    if (abs_open.empty() || !OpenDocument(abs_open)) NewDocument(3500, 3500);
    UpdateTitle();
}

void EditorApp::Message(const std::string& m, double seconds) {
    message_ = m;
    message_until_ = ImGui::GetTime() + seconds;
}

void EditorApp::Update() {
    proc_.Update();
    int code = 0;
    if (proc_.ConsumeLaunchFailed()) {
        Message("The test server did not start - see the log for the reason.", 8);
        show_log_ = true;
        testing_ = false;
    }
    if (proc_.ConsumeClientExited(&code) && testing_) StopTestPlay();
    if (proc_.ConsumeServerExited(&code) && testing_) {
        if (code != 0) {
            Message("The test server stopped with an error - see the log.", 8);
            show_log_ = true;
        }
        StopTestPlay();
    }

    std::string open_res, save_res;
    bool failed = false;
    {
        std::lock_guard<std::mutex> lock(dialog_mutex_);
        std::swap(open_res, dialog_open_result_);
        std::swap(save_res, dialog_save_result_);
        std::swap(failed, dialog_failed_);
    }
    if (failed) Message("No native file dialog available - type the path instead.");
    if (!open_res.empty()) browse_open_ = open_res;
    if (!save_res.empty()) browse_save_ = save_res;
    UpdateTitle();
}

void EditorApp::UpdateTitle() {
    std::string name = path_.empty() ? "Untitled" : fs::path(path_).filename().string();
    std::string t = name + (Modified() ? " *" : "") + " - XPilot TNG Map Editor";
    if (t != title_shown_) {
        SDL_SetWindowTitle(window_, t.c_str());
        title_shown_ = t;
    }
}

void EditorApp::RequestQuit() { Guard(Pending::Quit); }

void EditorApp::OpenDropped(const std::string& path) { Guard(Pending::OpenPath, path); }

// ============================================================================
// document
// ============================================================================

void EditorApp::NewDocument(int w, int h) {
    const char* user = std::getenv("USER");
    map_ = NewMap(w, h, "New map", user ? user : "");
    path_.clear();
    stable_ = map_;
    undo_.clear();
    redo_.clear();
    stable_id_ = saved_id_ = next_id_++;
    dirty_pending_ = false;
    revision_++;
    ClearSelection();
    draw_pts_.clear();
    shape_style_ = -1;
    need_fit_ = true;
}

bool EditorApp::OpenDocument(const std::string& path) {
    Map m;
    LoadResult res = LoadMap(path, m);
    if (!res.ok) {
        load_warnings_ = {res.error};
        open_warnings_popup_ = true;
        return false;
    }
    map_ = std::move(m);
    path_ = path;
    if (res.was_xpd) {
        path_ = fs::path(path).replace_extension(".xp2").string();
        res.warnings.insert(res.warnings.begin(),
                            "This is an .xpd bundle. Only the map was loaded; it will be saved as " +
                                fs::path(path_).filename().string() + " (without embedded bitmaps).");
    }
    stable_ = map_;
    undo_.clear();
    redo_.clear();
    stable_id_ = saved_id_ = next_id_++;
    if (res.was_xpd) saved_id_ = -1;    // not saved in the new format yet
    dirty_pending_ = false;
    revision_++;
    ClearSelection();
    draw_pts_.clear();
    shape_style_ = -1;
    need_fit_ = true;
    AddRecent(path);
    if (!res.warnings.empty()) {
        load_warnings_ = res.warnings;
        open_warnings_popup_ = true;
    }
    Message("Opened " + path);
    return true;
}

bool EditorApp::SaveDocument(const std::string& path_in) {
    std::string path = path_in;
    if (fs::path(path).extension().empty()) path += ".xp2";
    if (dirty_pending_) Commit();
    std::string err;
    if (!SaveMap(path, map_, &err)) {
        load_warnings_ = {"Saving failed: " + err};
        open_warnings_popup_ = true;
        return false;
    }
    path_ = path;
    saved_id_ = stable_id_;
    AddRecent(path);
    Message("Saved " + path);
    if (pending_ != Pending::None) RunPending();
    return true;
}

void EditorApp::Save() {
    if (path_.empty() || Lower(fs::path(path_).extension().string()) == ".xpd") {
        snprintf(path_buf_, sizeof(path_buf_), "%s", (maps_dir_ + "/" + "newmap.xp2").c_str());
        open_save_popup_ = true;
    } else {
        SaveDocument(path_);
    }
}

void EditorApp::Guard(Pending action, const std::string& path) {
    pending_ = action;
    pending_path_ = path;
    if (Modified()) open_unsaved_popup_ = true;
    else RunPending();
}

void EditorApp::RunPending() {
    Pending p = pending_;
    pending_ = Pending::None;
    switch (p) {
    case Pending::New: open_new_popup_ = true; break;
    case Pending::Open:
        map_files_ = ListFiles(maps_dir_, {".xp2", ".xpd"});
        path_buf_[0] = 0;
        open_open_popup_ = true;
        break;
    case Pending::OpenPath: OpenDocument(pending_path_); break;
    case Pending::Quit: quit_ = true; break;
    case Pending::None: break;
    }
}

void EditorApp::LoadRecent() {
    recent_.clear();
    char* pref = SDL_GetPrefPath("XPilot-TNG", "mapeditor");
    if (!pref) return;
    std::ifstream f(std::string(pref) + "recent.txt");
    SDL_free(pref);
    std::string line;
    while (std::getline(f, line))
        if (!line.empty() && recent_.size() < kMaxRecent) recent_.push_back(line);
}

void EditorApp::SaveRecent() const {
    char* pref = SDL_GetPrefPath("XPilot-TNG", "mapeditor");
    if (!pref) return;
    std::ofstream f(std::string(pref) + "recent.txt", std::ios::trunc);
    SDL_free(pref);
    for (const auto& r : recent_) f << r << "\n";
}

void EditorApp::AddRecent(const std::string& path) {
    std::error_code ec;
    std::string abs = fs::absolute(path, ec).string();
    recent_.erase(std::remove(recent_.begin(), recent_.end(), abs), recent_.end());
    recent_.insert(recent_.begin(), abs);
    if (recent_.size() > kMaxRecent) recent_.resize(kMaxRecent);
    SaveRecent();
}

// ============================================================================
// undo
// ============================================================================

void EditorApp::CommitIfIdle() {
    if (dirty_pending_ && !ImGui::IsAnyItemActive() && drag_ == Drag::None) Commit();
}

void EditorApp::Commit() {
    undo_.push_back({std::move(stable_), stable_id_});
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    stable_ = map_;
    stable_id_ = next_id_++;
    redo_.clear();
    dirty_pending_ = false;
}

void EditorApp::Undo() {
    if (dirty_pending_) Commit();
    if (undo_.empty()) return;
    redo_.push_back({std::move(stable_), stable_id_});
    stable_ = std::move(undo_.back().map);
    stable_id_ = undo_.back().id;
    undo_.pop_back();
    map_ = stable_;
    revision_++;
    SanitizeSelection();
}

void EditorApp::Redo() {
    if (dirty_pending_) Commit();
    if (redo_.empty()) return;
    undo_.push_back({std::move(stable_), stable_id_});
    stable_ = std::move(redo_.back().map);
    stable_id_ = redo_.back().id;
    redo_.pop_back();
    map_ = stable_;
    revision_++;
    SanitizeSelection();
}

// ============================================================================
// selection & edit operations
// ============================================================================

bool EditorApp::IsSelected(int item) const {
    return std::binary_search(sel_.begin(), sel_.end(), item);
}

void EditorApp::Select(int item, bool add) {
    if (!add) {
        sel_.clear();
        sel_vertex_ = {};
        sel_edge_ = {};
    }
    if (item < 0) return;
    auto it = std::lower_bound(sel_.begin(), sel_.end(), item);
    if (it != sel_.end() && *it == item) {
        if (add) sel_.erase(it);    // toggle
    } else {
        sel_.insert(it, item);
    }
    if (sel_.size() != 1) {
        sel_vertex_ = {};
        sel_edge_ = {};
    }
}

void EditorApp::ClearSelection() {
    sel_.clear();
    sel_vertex_ = {};
    sel_edge_ = {};
}

void EditorApp::SanitizeSelection() {
    const int n = int(map_.items.size());
    sel_.erase(std::remove_if(sel_.begin(), sel_.end(), [n](int i) { return i >= n; }), sel_.end());
    auto valid = [&](const VertexRef& v) {
        if (!v.Valid() || v.item >= n) return false;
        const Item& it = map_.items[v.item];
        return v.poly < int(it.polys.size()) && v.index < int(it.polys[v.poly].pts.size());
    };
    if (!valid(sel_vertex_)) sel_vertex_ = {};
    if (!valid(sel_edge_)) sel_edge_ = {};
    if (hover_item_ >= n) hover_item_ = -1;
}

void EditorApp::DeleteSelectedVertex() {
    if (!sel_vertex_.Valid()) return;
    Polygon& p = map_.items[sel_vertex_.item].polys[sel_vertex_.poly];
    if (p.pts.size() <= 3) {
        Message("A polygon needs at least 3 points.");
        return;
    }
    p.pts.erase(p.pts.begin() + sel_vertex_.index);
    p.edges.erase(p.edges.begin() + sel_vertex_.index);
    sel_vertex_ = {};
    sel_edge_ = {};
    Changed();
}

void EditorApp::DeleteSelection() {
    if (sel_vertex_.Valid()) {
        DeleteSelectedVertex();
        return;
    }
    if (sel_.empty()) return;
    for (auto it = sel_.rbegin(); it != sel_.rend(); ++it) map_.items.erase(map_.items.begin() + *it);
    Message(std::to_string(sel_.size()) + " object(s) deleted.");
    ClearSelection();
    hover_item_ = -1;
    Changed();
}

void EditorApp::DuplicateSelection() {
    if (sel_.empty()) return;
    const int off = std::max(grid_clicks_, kBlockClicks / 2);
    std::vector<int> fresh;
    for (int i : sel_) {
        Item copy = map_.items[i];
        TranslateItem(copy, off, -off);
        map_.items.push_back(std::move(copy));
        fresh.push_back(int(map_.items.size()) - 1);
    }
    sel_ = fresh;
    sel_vertex_ = {};
    sel_edge_ = {};
    Changed();
}

void EditorApp::CopySelection() {
    if (sel_.empty()) return;
    clipboard_.clear();
    for (int i : sel_) clipboard_.push_back(map_.items[i]);
    clip_poly_styles_ = map_.poly_styles;
    clip_edge_styles_ = map_.edge_styles;
    clip_bmp_styles_ = map_.bmp_styles;
    Message(std::to_string(clipboard_.size()) + " object(s) copied.");
}

void EditorApp::Paste() {
    if (clipboard_.empty()) return;
    // Styles are matched by id; missing ones are copied over from the source map.
    auto edge_ref = [&](int e) {
        if (e < 0 || e >= int(clip_edge_styles_.size())) return e;
        int idx = map_.FindEdgeStyle(clip_edge_styles_[e].id);
        if (idx >= 0) return idx;
        map_.edge_styles.push_back(clip_edge_styles_[e]);
        return int(map_.edge_styles.size()) - 1;
    };
    auto bmp_ref = [&](int b) {
        if (b < 0 || b >= int(clip_bmp_styles_.size())) return -1;
        int idx = map_.FindBmpStyle(clip_bmp_styles_[b].id);
        if (idx >= 0) return idx;
        map_.bmp_styles.push_back(clip_bmp_styles_[b]);
        return int(map_.bmp_styles.size()) - 1;
    };
    auto poly_ref = [&](int s) {
        if (s < 0 || s >= int(clip_poly_styles_.size())) return s;
        int idx = map_.FindPolyStyle(clip_poly_styles_[s].id);
        if (idx >= 0) return idx;
        PolyStyle ps = clip_poly_styles_[s];
        ps.defedge = edge_ref(ps.defedge);
        ps.texture = bmp_ref(ps.texture);
        map_.poly_styles.push_back(ps);
        return int(map_.poly_styles.size()) - 1;
    };

    Box b;
    for (const auto& it : clipboard_) {
        Box ib = ItemBounds(it);
        if (ib.Valid()) {
            b.Add(ib.lo);
            b.Add(ib.hi);
        }
    }
    double tx = mouse_in_canvas_ ? mouse_wx_ : view_x_ * kClick;
    double ty = mouse_in_canvas_ ? mouse_wy_ : view_y_ * kClick;
    Pt target = Snap(tx, ty);
    int dx = 0, dy = 0;
    if (b.Valid()) {
        Pt center{(b.lo.x + b.hi.x) / 2, (b.lo.y + b.hi.y) / 2};
        Pt d = SnapDelta(target.x - center.x, target.y - center.y, center);
        dx = d.x;
        dy = d.y;
    }
    sel_.clear();
    for (Item it : clipboard_) {
        if (it.style >= 0) it.style = poly_ref(it.style);
        for (auto& p : it.polys) {
            p.style = poly_ref(p.style);
            if (p.destroyed_style >= 0) p.destroyed_style = poly_ref(p.destroyed_style);
            for (int& e : p.edges) e = edge_ref(e);
        }
        TranslateItem(it, dx, dy);
        map_.items.push_back(std::move(it));
        sel_.push_back(int(map_.items.size()) - 1);
    }
    sel_vertex_ = {};
    sel_edge_ = {};
    Changed();
}

void EditorApp::SelectAll() {
    sel_.clear();
    for (int i = 0; i < int(map_.items.size()); i++) sel_.push_back(i);
    sel_vertex_ = {};
    sel_edge_ = {};
}

void EditorApp::TransformSelection(int mode) {
    if (sel_.empty()) return;
    Box b;
    for (int i : sel_) {
        Box ib = ItemBounds(map_.items[i]);
        if (ib.Valid()) {
            b.Add(ib.lo);
            b.Add(ib.hi);
        }
    }
    if (!b.Valid()) return;
    const long long cx2 = (long long)b.lo.x + b.hi.x, cy2 = (long long)b.lo.y + b.hi.y;   // 2 * center
    auto tf = [&](Pt& p) {
        if (mode == 0) p.x = int(cx2 - p.x);
        else if (mode == 1) p.y = int(cy2 - p.y);
        else {
            const long long x = p.x, y = p.y;
            p.x = int((cx2 - (2 * y - cy2)) / 2);
            p.y = int((cy2 + (2 * x - cx2)) / 2);
        }
    };
    for (int i : sel_) {
        Item& it = map_.items[i];
        tf(it.pos);
        for (auto& p : it.polys)
            for (auto& v : p.pts) tf(v);
        if (KindHasDir(it.kind)) {
            int d = it.dir;
            if (mode == 0) d = kRes / 2 - d;
            else if (mode == 1) d = kRes - d;
            else d = d + kRes / 4;
            it.dir = ((d % kRes) + kRes) % kRes;
        }
        if (it.kind == Kind::Grav) {
            GravType t = GravType(it.type);
            auto swap = [&](GravType a, GravType b2) {
                if (t == a) t = b2;
                else if (t == b2) t = a;
            };
            if (mode == 0 || mode == 1) swap(GravType::Cwise, GravType::Acwise);
            if (mode == 0) swap(GravType::Left, GravType::Right);
            if (mode == 1) swap(GravType::Up, GravType::Down);
            if (mode == 2) {
                static const GravType rot[] = {GravType::Pos,  GravType::Neg,   GravType::Cwise, GravType::Acwise,
                                               GravType::Left, GravType::Right, GravType::Up,    GravType::Down};
                t = rot[int(t)];
            }
            it.type = int(t);
        }
        if (mode != 2)
            for (auto& p : it.polys) EnsureCCW(p);   // mirroring reverses the winding
    }
    Changed();
}

void EditorApp::FixOrientation() {
    int fixed = 0;
    for (int i = 0; i < int(map_.items.size()); i++) {
        if (!sel_.empty() && !IsSelected(i)) continue;
        for (auto& p : map_.items[i].polys) {
            if (p.pts.size() >= 3 && SignedArea(p.pts) < 0) {
                EnsureCCW(p);
                fixed++;
            }
        }
    }
    Message(std::to_string(fixed) + " polygon(s) reversed.");
    if (fixed) Changed();
}

void EditorApp::MoveSelectionInOrder(bool to_front) {
    if (sel_.empty()) return;
    std::vector<Item> picked, rest;
    for (int i = 0; i < int(map_.items.size()); i++)
        (IsSelected(i) ? picked : rest).push_back(std::move(map_.items[i]));
    map_.items.clear();
    sel_.clear();
    if (to_front) {
        map_.items = std::move(rest);
        for (auto& it : picked) {
            map_.items.push_back(std::move(it));
            sel_.push_back(int(map_.items.size()) - 1);
        }
    } else {
        for (auto& it : picked) {
            map_.items.push_back(std::move(it));
            sel_.push_back(int(map_.items.size()) - 1);
        }
        for (auto& it : rest) map_.items.push_back(std::move(it));
    }
    sel_vertex_ = {};
    sel_edge_ = {};
    Changed();
}

void EditorApp::FocusItem(int item) {
    if (item < 0 || item >= int(map_.items.size())) return;
    Box b = ItemBounds(map_.items[item]);
    if (!b.Valid()) return;
    view_x_ = (b.lo.x + b.hi.x) / 2.0 / kClick;
    view_y_ = (b.lo.y + b.hi.y) / 2.0 / kClick;
    const double w = (b.hi.x - b.lo.x) / double(kClick), h = (b.hi.y - b.lo.y) / double(kClick);
    if (w > 0 || h > 0) {
        float z = float(std::min(canvas_size_.x / std::max(w, 1.0), canvas_size_.y / std::max(h, 1.0)) * 0.5);
        if (z < zoom_) zoom_ = std::max(z, 0.01f);
    }
}

// ============================================================================
// test play
// ============================================================================

void EditorApp::StartTestPlay() {
    if (testing_) StopTestPlay();
    if (dirty_pending_) Commit();
    test_map_path_ = (fs::temp_directory_path() / ("xpilot-tng-mapeditor-" + std::to_string(getpid()) + ".xp2")).string();
    std::string err;
    if (!SaveMap(test_map_path_, map_, &err)) {
        Message("Cannot write the test map: " + err, 8);
        return;
    }
    const int port = FindFreePort();
    std::vector<std::string> server_args = {"-mapFileName", test_map_path_, "-contactPort", std::to_string(port),
                                            "+noQuit", "+reportToMetaServer"};
    std::vector<std::string> client_args = {"-port", std::to_string(port), "localhost"};
    proc_.ClearLogs();
    proc_.LaunchLocalGame(server_bin_, server_args, client_bin_, client_args);
    testing_ = true;
    show_log_ = true;
    Message("Starting test game on port " + std::to_string(port) + "...");
}

void EditorApp::StopTestPlay() {
    proc_.CancelLaunch();
    proc_.StopClient();
    proc_.StopServer();
    testing_ = false;
    if (!test_map_path_.empty()) {
        std::error_code ec;
        fs::remove(test_map_path_, ec);
        test_map_path_.clear();
    }
}

// ============================================================================
// native file dialogs
// ============================================================================

namespace {
const SDL_DialogFileFilter kMapFilters[] = {{"XPilot maps", "xp2;xpd;gz"}, {"All files", "*"}};
}

void SDLCALL EditorApp::OnOpenDialog(void* ud, const char* const* files, int) {
    auto* self = static_cast<EditorApp*>(ud);
    std::lock_guard<std::mutex> lock(self->dialog_mutex_);
    if (!files) self->dialog_failed_ = true;
    else if (files[0]) self->dialog_open_result_ = files[0];
}

void SDLCALL EditorApp::OnSaveDialog(void* ud, const char* const* files, int) {
    auto* self = static_cast<EditorApp*>(ud);
    std::lock_guard<std::mutex> lock(self->dialog_mutex_);
    if (!files) self->dialog_failed_ = true;
    else if (files[0]) self->dialog_save_result_ = files[0];
}

void EditorApp::BrowseOpen() {
    SDL_ShowOpenFileDialog(OnOpenDialog, this, window_, kMapFilters, 2, maps_dir_.c_str(), false);
}

void EditorApp::BrowseSave() {
    SDL_ShowSaveFileDialog(OnSaveDialog, this, window_, kMapFilters, 1, path_buf_[0] ? path_buf_ : maps_dir_.c_str());
}

// ============================================================================
// layout
// ============================================================================

void EditorApp::RenderUI() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    DrawMenuBar();
    DrawToolbar();

    const float status_h = ImGui::GetFrameHeight() + 2;
    const float log_h = show_log_ ? 170.0f : 0.0f;
    const float body_h = ImGui::GetContentRegionAvail().y - status_h - log_h -
                         ImGui::GetStyle().ItemSpacing.y * (show_log_ ? 2 : 1);
    const float palette_w = 200.0f;
    const float inspector_w = std::clamp(ImGui::GetContentRegionAvail().x * 0.28f, 320.0f, 430.0f);

    ImGui::BeginChild("##palette", ImVec2(palette_w, body_h), ImGuiChildFlags_Borders);
    DrawToolPalette();
    ImGui::EndChild();
    ImGui::SameLine();

    const float canvas_w = ImGui::GetContentRegionAvail().x - inspector_w - ImGui::GetStyle().ItemSpacing.x;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##canvas", ImVec2(canvas_w, body_h), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawCanvas();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SameLine();

    ImGui::BeginChild("##inspector", ImVec2(inspector_w, body_h), ImGuiChildFlags_Borders);
    DrawInspector();
    ImGui::EndChild();

    if (show_log_) DrawLogWindow();
    DrawStatusBar();
    ImGui::End();

    DrawPopups();
    if (show_help_) DrawHelpWindow();
    if (show_smooth_) DrawSmoothWindow();
    HandleShortcuts();
    CommitIfIdle();
}
