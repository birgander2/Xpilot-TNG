#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>

#include <mutex>
#include <string>
#include <vector>

#include "map_model.h"
#include "option_catalog.h"
#include "process_manager.h"

struct EditorFonts {
    ImFont* regular = nullptr;
    ImFont* heading = nullptr;
    ImFont* small = nullptr;
};

class EditorApp {
public:
    EditorApp(SDL_Window* window, SDL_Renderer* renderer, const std::string& app_dir);
    ~EditorApp();

    void SetFonts(const EditorFonts& f) { fonts_ = f; }
    void Init(const std::string& open_path);
    void Update();
    void RenderUI();

    void RequestQuit();
    bool WantsQuit() const { return quit_; }
    void OpenDropped(const std::string& path);

private:
    enum class Tool { Select, Polygon, Rect, Ellipse, Place };
    enum class Pending { None, New, Open, OpenPath, Quit };
    enum class Drag { None, Pan, Move, Vertex, Box, Shape, Place };

    struct Snapshot {
        xpe::Map map;
        int id = 0;
    };
    struct VertexRef {
        int item = -1, poly = -1, index = -1;
        bool Valid() const { return item >= 0; }
    };

    // --- document ---------------------------------------------------------
    void NewDocument(int w, int h);
    bool OpenDocument(const std::string& path);
    bool SaveDocument(const std::string& path);
    void Save();                        // Save, or Save As if untitled
    bool Modified() const { return dirty_pending_ || stable_id_ != saved_id_; }
    void Guard(Pending action, const std::string& path = "");   // asks about unsaved changes first
    void RunPending();
    void AddRecent(const std::string& path);
    void LoadRecent();
    void SaveRecent() const;
    void UpdateTitle();

    // --- undo -------------------------------------------------------------
    void Changed() { dirty_pending_ = true; revision_++; }
    void CommitIfIdle();
    void Commit();
    void Undo();
    void Redo();

    // --- selection & edit operations --------------------------------------
    bool IsSelected(int item) const;
    void Select(int item, bool add = false);
    void ClearSelection();
    void SanitizeSelection();
    void DeleteSelection();
    void DuplicateSelection();
    void CopySelection();
    void Paste();
    void SelectAll();
    void TransformSelection(int mode);  // 0 flip horizontal, 1 flip vertical, 2 rotate 90 ccw
    void MoveSelectionInOrder(bool to_front);
    void FixOrientation();
    void DeleteSelectedVertex();
    void FocusItem(int item);

    // --- canvas (editor_canvas.cpp) ---------------------------------------
    void DrawCanvas();
    void HandleCanvasInput(bool hovered);
    void DrawMap(ImDrawList* dl);
    void DrawPolygon(ImDrawList* dl, const xpe::Polygon& p, xpe::Kind kind, bool selected, bool hovered,
                     const std::vector<int>* tris);
    const std::vector<int>& Triangles(int item, int poly);
    void DrawPointObject(ImDrawList* dl, const xpe::Item& it, int index, bool selected, bool hovered);
    void DrawToolPreview(ImDrawList* dl);
    void DrawGrid(ImDrawList* dl);
    void FitView();
    void ZoomAt(ImVec2 screen, float factor);
    ImVec2 ToScreen(xpe::Pt p) const;
    ImVec2 ToScreen(double x, double y) const;
    void ToWorld(ImVec2 s, double* x, double* y) const;
    xpe::Pt Snap(double x, double y) const;
    xpe::Pt SnapDelta(int dx, int dy, xpe::Pt anchor) const;
    int HitItem(ImVec2 s) const;
    VertexRef HitVertex(ImVec2 s) const;
    bool HitEdge(ImVec2 s, VertexRef* edge, xpe::Pt* at) const;
    float ObjectRadius() const;
    void FinishPolygon();
    void PlaceObject(xpe::Pt pos, int dir);
    void AddShape(std::vector<xpe::Pt> pts);

    // --- panels (editor_panels.cpp) ---------------------------------------
    void DrawMenuBar();
    void DrawToolbar();
    void DrawToolPalette();
    void DrawInspector();
    void DrawPropertiesTab();
    void DrawItemProperties(xpe::Item& it, int index);
    void DrawPolygonProperties(xpe::Item& it, int index);
    void DrawMapTab();
    void DrawStylesTab();
    void DrawOptionsTab();
    void DrawObjectsTab();
    void DrawProblemsTab();
    void DrawStatusBar();
    void DrawLogWindow();
    void DrawPopups();
    void DrawHelpWindow();
    bool PolyStyleCombo(const char* label, int* style, bool allow_none, const char* none_label = "(none)");
    bool EdgeStyleCombo(const char* label, int* edge);
    bool TeamCombo(const char* label, int* team, bool allow_none);
    bool DirEditor(int* dir);
    bool OptionValueEditor(const char* id, const std::string& name, std::string& value);
    bool QuickOption(const char* label, const char* name, const char* tip);
    void HandleShortcuts();

    // --- test play (editor_app.cpp) ---------------------------------------
    void StartTestPlay();
    void StopTestPlay();
    std::string ResolveBinary(const std::string& name) const;
    std::string FindRoot() const;

    // --- file dialogs -----------------------------------------------------
    void BrowseOpen();
    void BrowseSave();
    static void SDLCALL OnOpenDialog(void* ud, const char* const* files, int filter);
    static void SDLCALL OnSaveDialog(void* ud, const char* const* files, int filter);

    SDL_Window* window_;
    SDL_Renderer* renderer_;
    std::string app_dir_;
    std::string root_dir_;
    std::string maps_dir_;
    std::string textures_dir_;
    std::string server_bin_;
    std::string client_bin_;
    EditorFonts fonts_;
    bool quit_ = false;

    // document
    xpe::Map map_;
    std::string path_;                  // empty = untitled
    std::vector<std::string> recent_;
    std::vector<std::string> map_files_;    // lib/maps content for the open popup
    std::vector<std::string> texture_files_;
    xpe::OptionCatalog catalog_;
    bool catalog_ok_ = false;

    // undo
    xpe::Map stable_;
    int stable_id_ = 0, saved_id_ = 0, next_id_ = 1;
    bool dirty_pending_ = false;
    long revision_ = 0;
    std::vector<Snapshot> undo_, redo_;

    // selection
    std::vector<int> sel_;
    VertexRef sel_vertex_;
    VertexRef sel_edge_;                // index = edge number
    int hover_item_ = -1;
    std::vector<xpe::Item> clipboard_;
    std::vector<xpe::PolyStyle> clip_poly_styles_;
    std::vector<xpe::EdgeStyle> clip_edge_styles_;
    std::vector<xpe::BmpStyle> clip_bmp_styles_;

    // tools
    Tool tool_ = Tool::Select;
    xpe::Kind shape_kind_ = xpe::Kind::Wall;
    xpe::Kind place_kind_ = xpe::Kind::Base;
    int shape_style_ = -1;              // -1 = default style of the kind
    int shape_team_ = xpe::kNoTeam;
    float shape_friction_ = 0.5f;
    int ellipse_segments_ = 16;
    bool add_to_selected_ = false;
    int place_teams_[int(xpe::Kind::Count)];   // team per object type, see constructor
    int place_dir_ = xpe::kRes / 4;
    int place_grav_type_ = 0;
    float place_grav_force_ = -2.7f;
    int place_worm_type_ = 0;
    std::vector<xpe::Pt> draw_pts_;     // polygon tool: points so far

    // view
    ImVec2 canvas_pos_{0, 0}, canvas_size_{1, 1};
    double view_x_ = 0, view_y_ = 0;    // map pixel at the canvas center
    float zoom_ = 0.2f;                 // screen pixels per map pixel
    bool need_fit_ = true;
    bool show_grid_ = true;
    bool snap_ = true;
    int grid_clicks_ = xpe::kBlockClicks / 2;
    bool show_labels_ = true;
    double mouse_wx_ = 0, mouse_wy_ = 0;    // cursor in clicks
    bool mouse_in_canvas_ = false;

    // fill triangulation per item/polygon, recomputed when the points change
    struct TriCache {
        uint64_t hash = 0;
        std::vector<int> tris;
    };
    std::vector<std::vector<TriCache>> tri_cache_;

    // interaction
    Drag drag_ = Drag::None;
    ImVec2 drag_start_s_{0, 0};
    double drag_start_wx_ = 0, drag_start_wy_ = 0;
    xpe::Pt drag_anchor_;
    std::vector<xpe::Item> drag_orig_;  // selected items at drag start
    bool drag_moved_ = false;
    bool space_down_ = false;
    bool rmb_active_ = false, rmb_panned_ = false;
    ImVec2 rmb_start_{0, 0};

    // panels / popups
    int inspector_tab_request_ = -1;
    int sel_poly_style_ = -1, sel_edge_style_ = -1, sel_bmp_style_ = -1;
    char option_filter_[64] = "";
    int option_category_ = 0;
    std::vector<xpe::Problem> problems_;
    long problems_rev_ = -1;
    bool show_help_ = false;
    bool show_log_ = false;
    bool open_new_popup_ = false, open_open_popup_ = false, open_save_popup_ = false;
    bool open_unsaved_popup_ = false, open_scale_popup_ = false;
    int new_w_ = 3500, new_h_ = 3500;
    char path_buf_[1024] = "";
    std::string message_;               // transient status message
    double message_until_ = 0;
    std::vector<std::string> load_warnings_;
    bool open_warnings_popup_ = false;
    Pending pending_ = Pending::None;
    std::string pending_path_;
    std::string title_shown_;

    // native file dialog results (callbacks may run on another thread)
    std::mutex dialog_mutex_;
    std::string dialog_open_result_, dialog_save_result_;
    bool dialog_failed_ = false;
    std::string browse_open_, browse_save_;    // dialog results handed to the popups

    // test play
    ProcessManager proc_;
    std::string test_map_path_;
    bool testing_ = false;

    void Message(const std::string& m, double seconds = 4.0);
};
