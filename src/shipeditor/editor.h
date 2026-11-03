#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>

#include <mutex>
#include <string>
#include <vector>

#include "ship_files.h"
#include "ship_shape.h"
#include "ship_widgets.h"

struct EditorFonts {
    ImFont* regular = nullptr;
    ImFont* small = nullptr;
    ImFont* heading = nullptr;
};

class Editor {
public:
    Editor(SDL_Window* window, std::string app_dir);
    void SetFonts(const EditorFonts& f) { fonts_ = f; }
    void Init(const std::string& open_arg);
    void Update();
    void Render();
    void RequestQuit();
    bool WantsQuit() const { return quit_; }
    bool Animating() const { return spin_; }
    void OpenDropped(const std::string& path) { OpenFile(path); }

private:
    enum class Lib { Stock, Mine, File };
    enum class Pending { None, Load, Quit, New };

    // document
    void SetDoc(const ss::ShipShape& s, const std::string& origin);
    void RequestLoad(const ss::ShipShape& s, const std::string& origin);
    void PushUndo();
    void Undo();
    void Redo();
    bool Dirty() const { return !(doc_ == saved_); }
    void Status(const std::string& s, bool error = false) { status_ = s; status_error_ = error; }

    // actions
    void NewShip();
    void SaveToCollection(bool confirmed);
    void SetInXpilotrc();
    void LoadFromXpilotrc();
    void CopyToClipboard();
    void PasteFromClipboard();
    void ApplyText(const std::string& text);
    void OpenFile(const std::string& path);
    bool SaveCollection();
    void ReloadRc();

    // UI parts
    void DrawMenuBar();
    void DrawLibrary();
    void DrawLibraryList(Lib which, std::vector<ss::Entry>& list);
    void DrawToolbar();
    void DrawCanvas();
    void DrawSidePanel();
    void DrawPreview();
    void DrawPointLists();
    void DrawModals();
    void HandleKeys();

    // canvas helpers
    ImVec2 ToScreen(double x, double y) const;
    ss::IPt ToGrid(ImVec2 p) const;
    int HitPoint(int part, ImVec2 mouse) const;
    void MovePoint(int part, int idx, ss::IPt to);
    void AddPoint(int part, ss::IPt at, ImVec2 mouse);
    void DeletePoint(int part, int idx);

    SDL_Window* window_;
    std::string app_dir_, data_dir_;
    EditorFonts fonts_;
    bool quit_ = false;

    ss::ShipShape doc_, saved_;
    ss::Validation val_;
    std::string origin_;
    std::vector<ss::ShipShape> undo_, redo_;

    std::vector<ss::Entry> stock_, mine_, file_;
    std::string stock_path_, mine_path_, file_path_;
    Lib lib_tab_ = Lib::Stock;
    bool force_tab_ = false;   // select lib_tab_ in the next frame
    char filter_[64] = "";
    std::string rc_value_;   // current xpilot.shipShape

    // canvas
    int tool_ = ss::kHull;
    int sel_part_ = -1, sel_idx_ = -1;
    bool dragging_ = false, panning_ = false;
    bool mirror_ = true;
    float zoom_ = 1.0f;
    ImVec2 pan_{0, 0};
    ImVec2 canvas_center_{0, 0};
    float scale_ = 10.0f;

    // overlays
    bool show_wall_ = true, show_hit_ = true, show_shipship_ = false, show_shield_ = false;
    bool show_inside_ = true, show_zones_ = true, show_defaults_ = true, show_numbers_ = true;
    int obj_type_ = 0;
    int dir_ = 0;
    bool spin_ = false;
    double spin_acc_ = 0;

    // text field for the shape string
    std::string text_buf_;
    bool text_edited_ = false;

    // modals
    Pending pending_ = Pending::None;
    ss::ShipShape pending_shape_;
    std::string pending_origin_;
    bool ask_replace_ = false, ask_rc_ = false;
    int replace_index_ = -1;

    std::string status_;
    bool status_error_ = false;

    // file dialogs (callbacks may come from another thread)
    static void SDLCALL OnOpenDialog(void* ud, const char* const* files, int filter);
    static void SDLCALL OnExportDialog(void* ud, const char* const* files, int filter);
    std::mutex dialog_mutex_;
    std::string dialog_open_, dialog_export_;
};
