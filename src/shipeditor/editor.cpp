#include "editor.h"
#include "xpilotrc.h"

#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ss;

namespace {

const ImU32 kPartColor[kPartCount] = {
    IM_COL32(225, 232, 240, 255),   // hull
    IM_COL32(255, 150, 40, 255),    // engine
    IM_COL32(255, 80, 80, 255),     // main gun
    IM_COL32(255, 120, 200, 255),   // left gun
    IM_COL32(110, 190, 255, 255),   // right gun
    IM_COL32(220, 160, 255, 255),   // left rear gun
    IM_COL32(90, 230, 210, 255),    // right rear gun
    IM_COL32(255, 70, 70, 255),     // left light (red, port)
    IM_COL32(70, 230, 100, 255),    // right light (green, starboard)
    IM_COL32(255, 225, 70, 255),    // missile rack
};
const ImU32 kWallCol = IM_COL32(255, 70, 60, 255);
const ImU32 kHitCol = IM_COL32(255, 90, 255, 255);
const ImU32 kShipShipCol = IM_COL32(80, 220, 255, 255);
const ImU32 kShieldCol = IM_COL32(90, 140, 255, 255);

struct ObjType { const char* name; int radius; };
// pl_radius of the object types (src/server): hit when distance <= SHIP_SZ + radius
const ObjType kObjTypes[] = {
    {"Shot / laser pulse (0)", 0},
    {"Mine (8)", 8},
    {"Item (8)", 8},
    {"Ball (10)", 10},
    {"Missile / torpedo (15)", 15},
};

ImVec4 ColV(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 Alpha(ImU32 c, int a) { return (c & 0x00FFFFFF) | (ImU32(a) << 24); }

void DashedCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float thick) {
    const int seg = std::max(24, int(r / 3));
    for (int i = 0; i < seg; i += 2) {
        float a0 = 2 * IM_PI * i / seg, a1 = 2 * IM_PI * (i + 1) / seg;
        dl->AddLine({c.x + r * std::cos(a0), c.y + r * std::sin(a0)},
                    {c.x + r * std::cos(a1), c.y + r * std::sin(a1)}, col, thick);
    }
}

// Marker shape per part, drawn around c with size r
void DrawMarker(ImDrawList* dl, int part, ImVec2 c, float r, ImU32 col, bool filled) {
    const float t = 2.0f;
    auto tri = [&](float dir) {   // dir: +1 points forward (right), -1 backwards
        ImVec2 a{c.x + dir * r, c.y}, b{c.x - dir * r * 0.8f, c.y - r * 0.8f}, d{c.x - dir * r * 0.8f, c.y + r * 0.8f};
        if (filled) dl->AddTriangleFilled(a, b, d, col);
        else dl->AddTriangle(a, b, d, col, t);
    };
    switch (part) {
    case kEngine:
        if (filled) dl->AddRectFilled({c.x - r * 0.75f, c.y - r * 0.75f}, {c.x + r * 0.75f, c.y + r * 0.75f}, col);
        else dl->AddRect({c.x - r * 0.75f, c.y - r * 0.75f}, {c.x + r * 0.75f, c.y + r * 0.75f}, col, 0, 0, t);
        break;
    case kMainGun: case kLeftGun: case kRightGun: tri(1); break;
    case kLeftRearGun: case kRightRearGun: tri(-1); break;
    case kLeftLight: case kRightLight:
        if (filled) dl->AddCircleFilled(c, r * 0.7f, col);
        else dl->AddCircle(c, r * 0.7f, col, 0, t);
        break;
    case kMissileRack: {
        ImVec2 p[4] = {{c.x + r, c.y}, {c.x, c.y - r}, {c.x - r, c.y}, {c.x, c.y + r}};
        if (filled) dl->AddConvexPolyFilled(p, 4, col);
        else dl->AddPolyline(p, 4, col, ImDrawFlags_Closed, t);
        break;
    }
    default: dl->AddCircleFilled(c, r * 0.5f, col);
    }
}

float SegDist(ImVec2 p, ImVec2 a, ImVec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
    float qx = a.x + t * dx - p.x, qy = a.y + t * dy - p.y;
    return std::sqrt(qx * qx + qy * qy);
}

ShipShape DefaultShip() {
    ShipShape s;
    Parse("(NM:MyShip)(SH: 14,0 -8,8 -8,-8)(EN: -8,0)(MG: 14,0)(LL: -8,8)(RL: -8,-8)(MR: 14,0)", &s);
    return s;
}

const SDL_DialogFileFilter kFilters[] = {{"Ship shape files", "txt;shp;ships"}, {"All files", "*"}};

} // namespace

// ============================================================================
// setup
// ============================================================================

Editor::Editor(SDL_Window* window, std::string app_dir) : window_(window), app_dir_(std::move(app_dir)) {}

void Editor::Init(const std::string& open_arg) {
    data_dir_ = FindDataDir(app_dir_);
    std::string err;
    if (!data_dir_.empty()) {
        stock_path_ = data_dir_ + "/shipshapes.txt";
        LoadShipFile(stock_path_, &stock_, &err);
    }
    mine_path_ = CollectionPath();
    LoadShipFile(mine_path_, &mine_, &err);   // may not exist yet
    ReloadRc();

    SetDoc(DefaultShip(), "new ship");
    if (!rc_value_.empty()) LoadFromXpilotrc();
    if (!open_arg.empty()) {
        ShipShape s;
        if (open_arg.find('(') == std::string::npos) OpenFile(open_arg);
        else if (Parse(open_arg, &s) && !s.hull().empty()) SetDoc(s, "command line");
        else Status("No ship shape in the command line argument", true);
    }
    if (stock_.empty()) Status("Stock ships (lib/shipshapes.txt) not found; set XPILOT_DATADIR.", true);
}

void Editor::ReloadRc() {
    rc_value_.clear();
    XpilotrcGet("shipShape", &rc_value_);
}

void Editor::RequestQuit() {
    if (Dirty()) pending_ = Pending::Quit;
    else quit_ = true;
}

void Editor::Update() {
    std::string open, exp;
    {
        std::lock_guard<std::mutex> lock(dialog_mutex_);
        open.swap(dialog_open_);
        exp.swap(dialog_export_);
    }
    if (!open.empty()) OpenFile(open);
    if (!exp.empty()) {
        std::string err;
        if (SaveShipFile(exp, mine_, &err)) Status("Collection exported to " + exp);
        else Status("Export failed: " + err, true);
    }
    if (spin_) {
        spin_acc_ += ImGui::GetIO().DeltaTime * 32.0;   // 32 directions per second
        while (spin_acc_ >= 1.0) { dir_ = (dir_ + 1) % kRes; spin_acc_ -= 1.0; }
    }
    std::string title = "XPilot TNG Ship Editor - " + (doc_.name.empty() ? std::string("(unnamed)") : doc_.name) +
                        (Dirty() ? " *" : "");
    SDL_SetWindowTitle(window_, title.c_str());
}

void SDLCALL Editor::OnOpenDialog(void* ud, const char* const* files, int) {
    auto* self = static_cast<Editor*>(ud);
    std::lock_guard<std::mutex> lock(self->dialog_mutex_);
    if (files && files[0]) self->dialog_open_ = files[0];
}

void SDLCALL Editor::OnExportDialog(void* ud, const char* const* files, int) {
    auto* self = static_cast<Editor*>(ud);
    std::lock_guard<std::mutex> lock(self->dialog_mutex_);
    if (files && files[0]) self->dialog_export_ = files[0];
}

// ============================================================================
// document
// ============================================================================

void Editor::SetDoc(const ShipShape& s, const std::string& origin) {
    doc_ = saved_ = s;
    origin_ = origin;
    undo_.clear();
    redo_.clear();
    sel_part_ = sel_idx_ = -1;
    dragging_ = false;
    text_edited_ = false;
}

void Editor::RequestLoad(const ShipShape& s, const std::string& origin) {
    if (Dirty()) {
        pending_ = Pending::Load;
        pending_shape_ = s;
        pending_origin_ = origin;
        return;
    }
    SetDoc(s, origin);
    Status("Loaded \"" + s.name + "\" (" + origin + ")");
}

void Editor::PushUndo() {
    undo_.push_back(doc_);
    if (undo_.size() > 300) undo_.erase(undo_.begin());
    redo_.clear();
    text_edited_ = false;
}

void Editor::Undo() {
    if (undo_.empty()) return;
    redo_.push_back(doc_);
    doc_ = undo_.back();
    undo_.pop_back();
    sel_part_ = sel_idx_ = -1;
    text_edited_ = false;
}

void Editor::Redo() {
    if (redo_.empty()) return;
    undo_.push_back(doc_);
    doc_ = redo_.back();
    redo_.pop_back();
    sel_part_ = sel_idx_ = -1;
    text_edited_ = false;
}

// ============================================================================
// actions
// ============================================================================

void Editor::NewShip() {
    if (Dirty()) { pending_ = Pending::New; return; }
    SetDoc(DefaultShip(), "new ship");
    Status("New ship from the default triangle");
}

bool Editor::SaveCollection() {
    std::string err;
    if (!SaveShipFile(mine_path_, mine_, &err)) {
        Status("Saving the collection failed: " + err, true);
        return false;
    }
    return true;
}

void Editor::SaveToCollection(bool confirmed) {
    std::string why;
    if (doc_.name.empty()) { Status("Give the ship a name first.", true); return; }
    if (!NameUsable(doc_.name, &why)) { Status(why, true); return; }
    int idx = -1;
    for (int i = 0; i < (int)mine_.size(); ++i)
        if (mine_[i].shape.name == doc_.name) idx = i;
    if (idx >= 0 && !confirmed && !(mine_[idx].shape == doc_)) {
        replace_index_ = idx;
        ask_replace_ = true;
        return;
    }
    Entry e{doc_, val_.valid};
    if (idx >= 0) mine_[idx] = e;
    else mine_.push_back(e);
    if (SaveCollection()) {
        saved_ = doc_;
        origin_ = "my collection";
        Status("Saved \"" + doc_.name + "\" in " + mine_path_ + (val_.valid ? "" : "  (the ship is not valid yet!)"),
               !val_.valid);
    }
}

void Editor::SetInXpilotrc() {
    std::string err;
    if (!XpilotrcSet("shipShape", Serialize(doc_), &err)) {
        Status("Writing the xpilotrc failed: " + err, true);
        return;
    }
    ReloadRc();
    Status("xpilot.shipShape set in " + ::XpilotrcPath());
}

void Editor::LoadFromXpilotrc() {
    ReloadRc();
    if (rc_value_.empty()) { Status("No xpilot.shipShape in " + ::XpilotrcPath(), true); return; }
    ShipShape s;
    if (rc_value_.find('(') != std::string::npos) {
        if (Parse(rc_value_, &s)) { RequestLoad(s, "xpilotrc"); return; }
        Status("xpilot.shipShape could not be parsed.", true);
        return;
    }
    // just a name: look it up like the client (collection first, then the stock ships)
    for (auto* list : {&mine_, &stock_})
        for (auto& e : *list)
            if (e.shape.name == rc_value_) { RequestLoad(e.shape, "xpilotrc: " + rc_value_); return; }
    Status("xpilotrc names ship \"" + rc_value_ + "\", which is in no known collection.", true);
}

void Editor::CopyToClipboard() {
    ImGui::SetClipboardText(Serialize(doc_).c_str());
    Status("Shape copied to the clipboard");
}

void Editor::PasteFromClipboard() {
    const char* t = ImGui::GetClipboardText();
    if (!t || !*t) { Status("Clipboard is empty", true); return; }
    ApplyText(t);
}

void Editor::ApplyText(const std::string& text) {
    ShipShape s;
    std::vector<std::string> notes;
    if (!Parse(text, &s, &notes) || s.hull().empty()) {
        Status("No ship shape found in the text" + (notes.empty() ? std::string() : ": " + notes[0]), true);
        return;
    }
    PushUndo();
    doc_ = s;
    sel_part_ = sel_idx_ = -1;
    Status(notes.empty() ? "Shape applied" : "Shape applied - " + notes[0] +
                                                 (notes.size() > 1 ? " (+" + std::to_string(notes.size() - 1) + " more)" : ""),
           !notes.empty());
}

void Editor::OpenFile(const std::string& path) {
    std::string err;
    std::vector<Entry> list;
    if (!LoadShipFile(path, &list, &err)) { Status("Open failed: " + err, true); return; }
    if (list.empty()) { Status("No ship shapes in " + path, true); return; }
    file_ = std::move(list);
    file_path_ = path;
    lib_tab_ = Lib::File;
    force_tab_ = true;
    Status(std::to_string(file_.size()) + " ships read from " + path);
    if (file_.size() == 1) RequestLoad(file_[0].shape, fs::path(path).filename().string());
}

// ============================================================================
// layout
// ============================================================================

void Editor::Render() {
    val_ = Validate(doc_);
    if (!text_edited_) {   // one group per line for reading; Parse() ignores the line breaks
        text_buf_ = Serialize(doc_);
        for (size_t i = 0; (i = text_buf_.find(")(", i)) != std::string::npos; i += 2) text_buf_.insert(i + 1, "\n");
    }

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
    HandleKeys();

    const float status_h = ImGui::GetFrameHeightWithSpacing();
    const float h = ImGui::GetContentRegionAvail().y - status_h;
    const float left_w = 290, right_w = 370;

    ImGui::BeginChild("library", ImVec2(left_w, h), ImGuiChildFlags_Borders);
    DrawLibrary();
    ImGui::EndChild();
    ImGui::SameLine();

    const float mid_w = ImGui::GetContentRegionAvail().x - right_w - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginChild("center", ImVec2(mid_w, h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawToolbar();
    DrawCanvas();
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("side", ImVec2(right_w, h), ImGuiChildFlags_Borders);
    DrawSidePanel();
    ImGui::EndChild();

    // status line
    if (status_error_) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1), "%s", status_.c_str());
    else ImGui::TextDisabled("%s", status_.empty() ? "Ready." : status_.c_str());

    DrawModals();
    ImGui::End();
}

void Editor::DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New ship", "Ctrl+N")) NewShip();
        if (ImGui::MenuItem("Open ship file...", "Ctrl+O"))
            SDL_ShowOpenFileDialog(OnOpenDialog, this, window_, kFilters, 2, data_dir_.empty() ? nullptr : data_dir_.c_str(), false);
        ImGui::Separator();
        if (ImGui::MenuItem("Save to my collection", "Ctrl+S")) SaveToCollection(false);
        if (ImGui::MenuItem("Export my collection...")) SDL_ShowSaveFileDialog(OnExportDialog, this, window_, kFilters, 1, mine_path_.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Use as my ship (xpilotrc)...")) ask_rc_ = true;
        if (ImGui::MenuItem("Load my ship from xpilotrc")) LoadFromXpilotrc();
        ImGui::Separator();
        if (ImGui::MenuItem("Copy shape string", "Ctrl+C")) CopyToClipboard();
        if (ImGui::MenuItem("Paste shape string", "Ctrl+V")) PasteFromClipboard();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) RequestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undo_.empty())) Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redo_.empty())) Redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Flip front/back")) { PushUndo(); FlipFrontBack(doc_); }
        if (ImGui::MenuItem("Flip left/right")) { PushUndo(); FlipLeftRight(doc_); }
        if (ImGui::MenuItem("Rotate 90 left")) { PushUndo(); Rotate90(doc_, true); }
        if (ImGui::MenuItem("Rotate 90 right")) { PushUndo(); Rotate90(doc_, false); }
        if (ImGui::MenuItem("Reverse hull order")) { PushUndo(); std::reverse(doc_.hull().begin(), doc_.hull().end()); }
        ImGui::Separator();
        if (ImGui::MenuItem("Make symmetric (keep left half)")) {
            PushUndo();
            if (!MakeSymmetric(doc_, true)) { Undo(); redo_.clear(); Status("No left half to mirror", true); }
        }
        if (ImGui::MenuItem("Make symmetric (keep right half)")) {
            PushUndo();
            if (!MakeSymmetric(doc_, false)) { Undo(); redo_.clear(); Status("No right half to mirror", true); }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset special points to defaults")) {
            PushUndo();
            for (int p = kEngine; p < kPartCount; ++p) doc_.pts[p].clear();
        }
        if (ImGui::MenuItem("Write defaults as explicit points")) {
            PushUndo();
            ShipShape r = Resolve(doc_);
            for (int p = kEngine; p < kPartCount; ++p) doc_.pts[p] = r.pts[p];
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Wall hitbox (hull edges)", nullptr, &show_wall_);
        ImGui::MenuItem("Shot hit circle", nullptr, &show_hit_);
        ImGui::MenuItem("Ship-ship crash circle", nullptr, &show_shipship_);
        ImGui::MenuItem("Shield", nullptr, &show_shield_);
        ImGui::Separator();
        ImGui::MenuItem("Inside area (game grid)", nullptr, &show_inside_);
        ImGui::MenuItem("Size zones", nullptr, &show_zones_);
        ImGui::MenuItem("Default positions", nullptr, &show_defaults_);
        ImGui::MenuItem("Hull point numbers", nullptr, &show_numbers_);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset zoom", "Home")) { zoom_ = 1; pan_ = {0, 0}; }
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void Editor::HandleKeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) NewShip();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveToCollection(false);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
        SDL_ShowOpenFileDialog(OnOpenDialog, this, window_, kFilters, 2, data_dir_.empty() ? nullptr : data_dir_.c_str(), false);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Q, false)) RequestQuit();
    if (io.WantTextInput) return;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) { if (io.KeyShift) Redo(); else Undo(); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) CopyToClipboard();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteFromClipboard();
    if (ctrl) return;
    for (int i = 0; i < 9; ++i)   // keys 1..9: hull .. right light, 0: missile rack
        if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_1 + i), false)) tool_ = i;
    if (ImGui::IsKeyPressed(ImGuiKey_0, false)) tool_ = kMissileRack;
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) { zoom_ = 1; pan_ = {0, 0}; }
    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) mirror_ = !mirror_;
    const bool has_sel = sel_part_ >= 0 && sel_idx_ >= 0 && sel_idx_ < (int)doc_.pts[sel_part_].size();
    if (has_sel && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        PushUndo();
        DeletePoint(sel_part_, sel_idx_);
    }
    if (has_sel && !dragging_) {
        int dx = 0, dy = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy = -1;
        if (dx || dy) {
            IPt p = doc_.pts[sel_part_][sel_idx_];
            IPt to{std::clamp(p.x + dx, -kLimit, kLimit), std::clamp(p.y + dy, -kLimit, kLimit)};
            if (to != p) { PushUndo(); MovePoint(sel_part_, sel_idx_, to); }
        }
    }
}

// ============================================================================
// library
// ============================================================================

void Editor::DrawLibrary() {
    if (fonts_.heading) ImGui::PushFont(fonts_.heading);
    ImGui::TextUnformatted("Ships");
    if (fonts_.heading) ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Search name / author", filter_, sizeof filter_);

    if (ImGui::BeginTabBar("libtabs")) {
        auto tab = [&](const char* label, Lib which) {
            const bool force = force_tab_ && lib_tab_ == which;
            bool open = ImGui::BeginTabItem(label, nullptr, force ? ImGuiTabItemFlags_SetSelected : 0);
            if (open && !force_tab_) lib_tab_ = which;
            return open;
        };
        if (tab("Stock", Lib::Stock)) {
            ImGui::TextDisabled("%d ships, read-only", (int)stock_.size());
            DrawLibraryList(Lib::Stock, stock_);
            ImGui::EndTabItem();
        }
        if (tab("My collection", Lib::Mine)) {
            if (ImGui::Button("Save current ship")) SaveToCollection(false);
            ImGui::SameLine();
            if (ImGui::Button("Export...")) SDL_ShowSaveFileDialog(OnExportDialog, this, window_, kFilters, 1, mine_path_.c_str());
            ImGui::TextDisabled("%d ships", (int)mine_.size());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nCan be used as shipShapeFile.", mine_path_.c_str());
            DrawLibraryList(Lib::Mine, mine_);
            ImGui::EndTabItem();
        }
        if (tab("File", Lib::File)) {
            if (ImGui::Button("Open file..."))
                SDL_ShowOpenFileDialog(OnOpenDialog, this, window_, kFilters, 2, data_dir_.empty() ? nullptr : data_dir_.c_str(), false);
            if (!file_.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Add all to collection")) {
                    int added = 0, skipped = 0;
                    for (auto& e : file_) {
                        bool exists = e.shape.name.empty() || !NameUsable(e.shape.name, nullptr);
                        for (auto& m : mine_) if (m.shape.name == e.shape.name) exists = true;
                        if (exists) { skipped++; continue; }
                        mine_.push_back(e);
                        added++;
                    }
                    if (SaveCollection())
                        Status(std::to_string(added) + " ships added" +
                               (skipped ? ", " + std::to_string(skipped) + " skipped (name exists or missing)" : ""));
                }
                ImGui::TextDisabled("%s", fs::path(file_path_).filename().string().c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", file_path_.c_str());
            } else {
                ImGui::TextWrapped("Open any shipshapes file (one ship per line) or drop it onto the window.");
            }
            DrawLibraryList(Lib::File, file_);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("xpilotrc")) {
            ImGui::TextWrapped("%s", ::XpilotrcPath().c_str());
            ImGui::Spacing();
            if (rc_value_.empty()) ImGui::TextDisabled("No xpilot.shipShape set: the client flies the default ship.");
            else {
                ShipShape s;
                bool full = rc_value_.find('(') != std::string::npos && Parse(rc_value_, &s);
                ImGui::Text("Current ship: %s", full ? (s.name.empty() ? "(unnamed)" : s.name.c_str()) : rc_value_.c_str());
                if (full) {
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + 96, p.y + 96}, IM_COL32(5, 7, 12, 255), 4);
                    DrawThumb(ImGui::GetWindowDrawList(), s, {p.x + 8, p.y + 8}, 80, IM_COL32(230, 235, 245, 255));
                    ImGui::Dummy({96, 96});
                    if (!Validate(s).valid)
                        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Not valid: the client uses the default ship.");
                } else {
                    ImGui::TextDisabled("(a name, resolved via shipShapeFile)");
                }
                if (ImGui::Button("Load into editor")) LoadFromXpilotrc();
            }
            ImGui::Spacing();
            if (ImGui::Button("Use current ship...")) ask_rc_ = true;
            ImGui::SameLine();
            if (ImGui::Button("Reload")) ReloadRc();
            ImGui::Spacing();
            ImGui::TextWrapped("The full shape definition is written to xpilot.shipShape, so it works without "
                               "a shipShapeFile. A backup is kept as xpilotrc.bak.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        force_tab_ = false;
    }
}

void Editor::DrawLibraryList(Lib which, std::vector<Entry>& list) {
    ImGui::BeginChild("list", ImVec2(0, 0), ImGuiChildFlags_None);
    std::vector<int> shown;
    for (int i = 0; i < (int)list.size(); ++i)
        if (ContainsNoCase(list[i].shape.name, filter_) || ContainsNoCase(list[i].shape.author, filter_)) shown.push_back(i);

    const float row_h = kShipRowH;
    int remove = -1, move_from = -1, move_to = -1;
    ImGuiListClipper clip;
    clip.Begin((int)shown.size(), row_h + ImGui::GetStyle().ItemSpacing.y);
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            const int i = shown[r];
            Entry& e = list[i];
            ImGui::PushID(i);
            const bool current = e.shape == doc_;
            if (ShipRow(e, current, fonts_.small)) {
                if (ImGui::IsMouseDoubleClicked(0)) RequestLoad(e.shape, which == Lib::Stock ? "stock" : which == Lib::Mine ? "my collection" : "file");
            }
            if (ImGui::IsItemHovered() && !ImGui::IsMouseDoubleClicked(0))
                ImGui::SetTooltip("Double-click to edit%s", which == Lib::Mine ? "\nRight-click for more" : "\nRight-click to copy to your collection");
            if (ImGui::BeginPopupContextItem("ctx")) {
                if (ImGui::MenuItem("Edit")) RequestLoad(e.shape, which == Lib::Stock ? "stock" : which == Lib::Mine ? "my collection" : "file");
                if (which != Lib::Mine && ImGui::MenuItem("Copy to my collection")) {
                    bool exists = false;
                    for (auto& m : mine_) if (m.shape.name == e.shape.name) exists = true;
                    if (exists) Status("\"" + e.shape.name + "\" is already in your collection", true);
                    else { mine_.push_back(e); if (SaveCollection()) Status("Copied \"" + e.shape.name + "\" to your collection"); }
                }
                if (ImGui::MenuItem("Use as my ship (xpilotrc)")) {
                    std::string err;
                    if (XpilotrcSet("shipShape", Serialize(e.shape), &err)) { ReloadRc(); Status("xpilot.shipShape set to \"" + e.shape.name + "\""); }
                    else Status("Writing the xpilotrc failed: " + err, true);
                }
                if (ImGui::MenuItem("Copy shape string")) ImGui::SetClipboardText(Serialize(e.shape).c_str());
                if (which == Lib::Mine) {
                    ImGui::Separator();
                    if (ImGui::MenuItem("Move up", nullptr, false, i > 0)) { move_from = i; move_to = i - 1; }
                    if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < (int)list.size())) { move_from = i; move_to = i + 1; }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete")) remove = i;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (remove >= 0) {
        std::string n = list[remove].shape.name;
        list.erase(list.begin() + remove);
        if (SaveCollection()) Status("Deleted \"" + n + "\" from your collection");
    }
    if (move_from >= 0) {
        std::swap(list[move_from], list[move_to]);
        SaveCollection();
    }
}

// ============================================================================
// canvas
// ============================================================================

void Editor::DrawToolbar() {
    for (int p = 0; p < kPartCount; ++p) {
        if (p) ImGui::SameLine(0, 4);
        const bool active = tool_ == p;
        ImVec4 c = ColV(kPartColor[p]);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(c.x * 0.45f, c.y * 0.45f, c.z * 0.45f, 1) : ImVec4(0.14f, 0.17f, 0.23f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4(1, 1, 1, 1) : c);
        static const char* shortl[kPartCount] = {"Hull", "EN", "MG", "LG", "RG", "LR", "RR", "LL", "RL", "MR"};
        char label[32];
        std::snprintf(label, sizeof label, "%s %d##tool%d", shortl[p], (int)doc_.pts[p].size(), p);
        if (ImGui::Button(label)) tool_ = p;
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s  (key %d)\n%d of max %d set%s", Info(p).label, (p + 1) % 10, (int)doc_.pts[p].size(), Info(p).max,
                              p > 0 && doc_.pts[p].empty() ? " - game default is used" : "");
    }
    ImGui::SameLine(0, 14);
    ImGui::Checkbox("Mirror L/R", &mirror_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Left and right guns / lights are placed in pairs (key M)");

    ImGui::TextDisabled("%s", tool_ == kHull
        ? "Click: add point (on an edge: insert) | drag: move | right-click: delete | arrows: nudge"
        : "Click: place | drag: move | right-click: delete (empty = game default)");
}

ImVec2 Editor::ToScreen(double x, double y) const {
    return {canvas_center_.x + float(x) * scale_, canvas_center_.y - float(y) * scale_};
}

IPt Editor::ToGrid(ImVec2 p) const {
    int x = (int)std::lround((p.x - canvas_center_.x) / scale_);
    int y = (int)std::lround(-(p.y - canvas_center_.y) / scale_);
    return {std::clamp(x, -kLimit, kLimit), std::clamp(y, -kLimit, kLimit)};
}

int Editor::HitPoint(int part, ImVec2 mouse) const {
    const auto& v = doc_.pts[part];
    int best = -1;
    float bd = 10.0f;
    for (int i = 0; i < (int)v.size(); ++i) {
        ImVec2 s = ToScreen(v[i].x, v[i].y);
        float d = std::hypot(s.x - mouse.x, s.y - mouse.y);
        if (d <= bd) { bd = d; best = i; }
    }
    return best;
}

void Editor::MovePoint(int part, int idx, IPt to) {
    doc_.pts[part][idx] = to;
    const int m = Info(part).mirror;
    if (mirror_ && m >= 0 && idx < (int)doc_.pts[m].size()) doc_.pts[m][idx] = {to.x, -to.y};
}

void Editor::DeletePoint(int part, int idx) {
    auto& v = doc_.pts[part];
    if (idx < 0 || idx >= (int)v.size()) return;
    v.erase(v.begin() + idx);
    const int m = Info(part).mirror;
    if (mirror_ && m >= 0 && idx < (int)doc_.pts[m].size()) doc_.pts[m].erase(doc_.pts[m].begin() + idx);
    if (sel_part_ == part) sel_idx_ = std::min(sel_idx_, (int)v.size() - 1);
}

void Editor::AddPoint(int part, IPt at, ImVec2 mouse) {
    auto& v = doc_.pts[part];
    const int max = Info(part).max;
    if (part == kHull) {
        if ((int)v.size() >= max) { Status("A hull has at most 24 points.", true); return; }
        PushUndo();
        int insert = (int)v.size();
        // on an edge: insert there
        float best = 8.0f;
        for (int i = 0; i < (int)v.size() && v.size() >= 2; ++i) {
            int j = (i + 1) % (int)v.size();
            float d = SegDist(mouse, ToScreen(v[i].x, v[i].y), ToScreen(v[j].x, v[j].y));
            if (d < best) { best = d; insert = i + 1; }
        }
        if (best >= 8.0f && sel_part_ == kHull && sel_idx_ >= 0 && sel_idx_ < (int)v.size()) insert = sel_idx_ + 1;
        v.insert(v.begin() + insert, at);
        sel_part_ = kHull;
        sel_idx_ = insert;
        dragging_ = true;
        return;
    }
    if (max == 1) {
        PushUndo();
        if (v.empty()) v.push_back(at); else v[0] = at;
        sel_part_ = part; sel_idx_ = 0; dragging_ = true;
        return;
    }
    if ((int)v.size() >= max) {
        Status(std::string(Info(part).label) + ": at most " + std::to_string(max) + " points. Drag or delete one.", true);
        return;
    }
    PushUndo();
    v.push_back(at);
    const int m = Info(part).mirror;
    if (mirror_ && m >= 0 && doc_.pts[m].size() + 1 == v.size()) doc_.pts[m].push_back({at.x, -at.y});
    sel_part_ = part;
    sel_idx_ = (int)v.size() - 1;
    dragging_ = true;
}

void Editor::DrawCanvas() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 200.0f);
    avail.y = std::max(avail.y, 200.0f);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                                ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1{p0.x + avail.x, p0.y + avail.y};

    // view: +-21 pixels fit by default (room for the hit circle)
    scale_ = std::min(avail.x, avail.y) / 44.0f * zoom_;
    canvas_center_ = {p0.x + avail.x * 0.5f + pan_.x, p0.y + avail.y * 0.5f + pan_.y};

    if (hovered && io.MouseWheel != 0) {
        float old = zoom_;
        zoom_ = std::clamp(zoom_ * (io.MouseWheel > 0 ? 1.15f : 1 / 1.15f), 0.4f, 6.0f);
        // zoom around the mouse
        ImVec2 m = io.MousePos;
        ImVec2 base{p0.x + avail.x * 0.5f, p0.y + avail.y * 0.5f};
        pan_.x = (m.x - base.x) - (m.x - base.x - pan_.x) * zoom_ / old;
        pan_.y = (m.y - base.y) - (m.y - base.y - pan_.y) * zoom_ / old;
        scale_ = std::min(avail.x, avail.y) / 44.0f * zoom_;
        canvas_center_ = {base.x + pan_.x, base.y + pan_.y};
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning_ = true;
    if (panning_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) { pan_.x += io.MouseDelta.x; pan_.y += io.MouseDelta.y; }
        else panning_ = false;
    }

    // ---- interaction ----
    const ImVec2 mouse = io.MousePos;
    const IPt g = ToGrid(mouse);
    const int hover = hovered ? HitPoint(tool_, mouse) : -1;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hover >= 0) {
            PushUndo();
            sel_part_ = tool_;
            sel_idx_ = hover;
            dragging_ = true;
        } else {
            AddPoint(tool_, g, mouse);
        }
    }
    if (dragging_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && sel_part_ >= 0 && sel_idx_ >= 0 &&
            sel_idx_ < (int)doc_.pts[sel_part_].size()) {
            if (doc_.pts[sel_part_][sel_idx_] != g) MovePoint(sel_part_, sel_idx_, g);
        } else {
            dragging_ = false;
            if (!undo_.empty() && undo_.back() == doc_) undo_.pop_back();   // a click without change
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hover >= 0) {
        PushUndo();
        DeletePoint(tool_, hover);
        if (sel_part_ == tool_ && sel_idx_ == hover) sel_idx_ = -1;
    }

    // ---- background ----
    dl->PushClipRect(p0, p1, true);
    dl->AddRectFilled(p0, p1, IM_COL32(8, 10, 16, 255));

    const ImVec2 b0 = ToScreen(-kLimit - 0.5, kLimit + 0.5), b1 = ToScreen(kLimit + 0.5, -kLimit - 0.5);
    if (show_zones_) {
        auto zone = [&](double x0, double y0, double x1, double y1, bool ok) {
            ImVec2 a = ToScreen(x0, y1), b = ToScreen(x1, y0);
            dl->AddRectFilled(a, b, ok ? IM_COL32(60, 200, 110, 22) : IM_COL32(255, 150, 40, 34));
        };
        const double L = kLimit + 0.5, Z = 7.5;
        zone(Z, -L, L, L, val_.hi > 0);       // front
        zone(-L, -L, -Z, L, val_.low > 0);    // rear
        zone(-L, Z, L, L, val_.left > 0);     // left
        zone(-L, -L, L, -Z, val_.right > 0);  // right
    }
    if (show_inside_ && val_.grid_ok) {
        for (int x = -kLimit; x <= kLimit; ++x)
            for (int y = -kLimit; y <= kLimit; ++y) {
                int c = val_.grid[x + 15][y + 15];
                if (c == 2) continue;
                ImVec2 a = ToScreen(x - 0.5, y + 0.5), b = ToScreen(x + 0.5, y - 0.5);
                dl->AddRectFilled(a, b, c == 1 ? IM_COL32(110, 140, 190, 60) : IM_COL32(80, 110, 160, 40));
            }
    }
    // grid
    for (int i = -kLimit; i <= kLimit; ++i) {
        ImU32 col = i == 0 ? IM_COL32(120, 140, 170, 140) : (i % 5 == 0 ? IM_COL32(70, 85, 110, 120) : IM_COL32(45, 55, 72, 90));
        if (scale_ < 7 && i % 5) continue;
        dl->AddLine(ToScreen(i, -kLimit), ToScreen(i, kLimit), col);
        dl->AddLine(ToScreen(-kLimit, i), ToScreen(kLimit, i), col);
    }
    dl->AddRect(b0, b1, IM_COL32(140, 160, 200, 160), 0, 0, 1.5f);
    {
        ImVec2 f = ToScreen(kLimit + 1.2, 0);
        dl->AddTriangleFilled({f.x + 10, f.y}, {f.x, f.y - 6}, {f.x, f.y + 6}, IM_COL32(140, 160, 200, 200));
        ImGui::PushFont(fonts_.small ? fonts_.small : ImGui::GetFont());
        dl->AddText({f.x - 8, f.y + 9}, IM_COL32(140, 160, 200, 200), "front");
        ImVec2 l = ToScreen(0, kLimit + 0.7);
        dl->AddText({l.x - 10, l.y - 16}, IM_COL32(140, 160, 200, 200), "left");
        ImVec2 r = ToScreen(0, -kLimit - 0.7);
        dl->AddText({r.x - 12, r.y + 2}, IM_COL32(140, 160, 200, 200), "right");
        ImGui::PopFont();
    }

    const ImVec2 c0 = ToScreen(0, 0);
    // ---- hit boxes ----
    if (show_shield_) dl->AddCircle(c0, val_.shield_radius * 0.5f * scale_, Alpha(kShieldCol, 170), 0, 1.5f);
    if (show_shipship_) DashedCircle(dl, c0, kShipShipRange * 0.5f * scale_, Alpha(kShipShipCol, 200), 1.5f);
    if (show_hit_) {
        dl->AddCircleFilled(c0, kShipSz * scale_, Alpha(kHitCol, 18), 96);
        dl->AddCircle(c0, kShipSz * scale_, Alpha(kHitCol, 220), 96, 2.0f);
        const int r = kObjTypes[obj_type_].radius;
        if (r > 0) DashedCircle(dl, c0, (kShipSz + r) * scale_, Alpha(kHitCol, 150), 1.5f);
    }

    // ---- hull ----
    const auto& h = doc_.hull();
    if (show_wall_ && h.size() >= 2) {
        std::vector<ImVec2> wall;
        for (IPt p : h) { FPt q = Rotate(p, dir_); wall.push_back(ToScreen(q.x, q.y)); }
        dl->AddPolyline(wall.data(), (int)wall.size(), Alpha(kWallCol, dir_ ? 230 : 140), ImDrawFlags_Closed, dir_ ? 2.5f : 6.0f);
        if (dir_) {
            FPt nose = Rotate({kLimit + 2, 0}, dir_);
            dl->AddLine(c0, ToScreen(nose.x, nose.y), Alpha(kWallCol, 160), 1.5f);
        }
    }
    if (h.size() >= 2) {
        std::vector<ImVec2> pts;
        for (IPt p : h) pts.push_back(ToScreen(p.x, p.y));
        dl->AddPolyline(pts.data(), (int)pts.size(), kPartColor[kHull], ImDrawFlags_Closed, 2.0f);
    }
    ImGui::PushFont(fonts_.small ? fonts_.small : ImGui::GetFont());
    for (int i = 0; i < (int)h.size(); ++i) {
        ImVec2 s = ToScreen(h[i].x, h[i].y);
        const bool sel = sel_part_ == kHull && sel_idx_ == i;
        const bool hov = tool_ == kHull && hover == i;
        float r = tool_ == kHull ? 4.5f : 3.0f;
        if (i == 0) dl->AddCircle(s, r + 3, IM_COL32(255, 255, 255, 160), 0, 1.5f);
        dl->AddCircleFilled(s, sel || hov ? r + 2 : r, sel ? IM_COL32(255, 220, 90, 255) : kPartColor[kHull]);
        if (show_numbers_ && scale_ > 9) {
            char num[12];
            std::snprintf(num, sizeof num, "%d", i + 1);
            dl->AddText({s.x + 6, s.y - 16}, IM_COL32(170, 185, 210, 200), num);
        }
    }
    ImGui::PopFont();

    // ---- special points ----
    const float mr = std::clamp(scale_ * 0.45f, 5.0f, 11.0f);
    for (int p = kEngine; p < kPartCount; ++p) {
        const bool active = tool_ == p;
        const auto& own = doc_.pts[p];
        if (own.empty()) {
            if (!show_defaults_ && !active) continue;
            for (IPt q : val_.resolved.pts[p])
                DrawMarker(dl, p, ToScreen(q.x, q.y), mr, Alpha(kPartColor[p], active ? 200 : 110), false);
            continue;
        }
        for (int i = 0; i < (int)own.size(); ++i) {
            ImVec2 s = ToScreen(own[i].x, own[i].y);
            const bool sel = sel_part_ == p && sel_idx_ == i;
            const bool bad = val_.grid_ok && Outside(val_, own[i]);
            DrawMarker(dl, p, s, (active && hover == i) || sel ? mr * 1.3f : mr, Alpha(kPartColor[p], active ? 255 : 190), true);
            if (sel) dl->AddCircle(s, mr * 1.6f, IM_COL32(255, 220, 90, 255), 0, 1.5f);
            if (bad) dl->AddCircle(s, mr * 1.9f, IM_COL32(255, 60, 40, 255), 0, 2.5f);
        }
    }

    // ---- hover info ----
    if (hovered && !panning_) {
        ImVec2 gs = ToScreen(g.x, g.y);
        dl->AddCircle(gs, 3, Alpha(kPartColor[tool_], 200));
        std::string tip = "(" + std::to_string(g.x) + ", " + std::to_string(g.y) + ")";
        for (int p = 0; p < kPartCount; ++p)
            for (IPt q : val_.resolved.pts[p]) if (q == g) { tip += "  "; tip += Info(p).label; break; }
        if (val_.grid_ok) tip += Outside(val_, g) ? "   outside" : "   inside";
        dl->AddText({p0.x + 8, p1.y - ImGui::GetTextLineHeight() - 6}, IM_COL32(190, 200, 220, 255), tip.c_str());
    }
    // legend
    {
        float y = p0.y + 8;
        auto legend = [&](ImU32 col, const char* t, bool on) {
            if (!on) return;
            dl->AddRectFilled({p0.x + 8, y + 4}, {p0.x + 20, y + 12}, col);
            dl->AddText({p0.x + 26, y}, IM_COL32(200, 210, 225, 230), t);
            y += ImGui::GetTextLineHeight() + 2;
        };
        legend(kWallCol, dir_ ? "wall hitbox (rotated)" : "wall hitbox = hull edges", show_wall_);
        legend(kHitCol, "shot hit circle r=16", show_hit_);
        legend(kShipShipCol, "ship-ship crash r=13", show_shipship_);
        legend(kShieldCol, "shield", show_shield_);
        if (zoom_ != 1.0f) {
            char z[32];
            std::snprintf(z, sizeof z, "zoom %.0f%%", zoom_ * 100);
            dl->AddText({p1.x - 90, p0.y + 8}, IM_COL32(150, 160, 180, 200), z);
        }
    }
    dl->PopClipRect();
}

// ============================================================================
// side panel
// ============================================================================

void Editor::DrawSidePanel() {
    // ---- name ----
    ImGui::SetNextItemWidth(-60);
    if (ImGui::InputText("Name", &doc_.name)) {}
    if (ImGui::IsItemActivated()) PushUndo();
    ImGui::SetNextItemWidth(-60);
    ImGui::InputText("Author", &doc_.author);
    if (ImGui::IsItemActivated()) PushUndo();
    ImGui::TextDisabled("from: %s%s", origin_.c_str(), Dirty() ? "  (modified)" : "");

    // ---- validation ----
    ImGui::Spacing();
    if (fonts_.heading) ImGui::PushFont(fonts_.heading);
    if (val_.valid) ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1), "Valid ship");
    else ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1), "Invalid ship");
    if (fonts_.heading) ImGui::PopFont();
    if (!val_.valid) ImGui::TextDisabled("The game would fly the default ship instead.");
    for (const auto& is : val_.issues) {
        ImVec4 c = is.sev == Severity::Error ? ImVec4(1, 0.45f, 0.35f, 1)
                 : is.sev == Severity::Warning ? ImVec4(1, 0.8f, 0.35f, 1) : ImVec4(0.6f, 0.7f, 0.85f, 1);
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        ImGui::Bullet();
        ImGui::TextWrapped("%s", is.text.c_str());
        ImGui::PopStyleColor();
    }
    {
        bool stock_clash = false;
        for (auto& e : stock_) if (!doc_.name.empty() && e.shape.name == doc_.name && !(e.shape == doc_)) stock_clash = true;
        if (stock_clash) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.8f, 0.35f, 1));
            ImGui::Bullet();
            ImGui::TextWrapped("A stock ship has the same name: the launcher would pick the stock one. Rename your ship.");
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Save to collection")) SaveToCollection(false);
    ImGui::SameLine();
    if (ImGui::Button("Use as my ship...")) ask_rc_ = true;
    ImGui::Separator();

    // ---- hitbox ----
    if (ImGui::CollapsingHeader("Hitbox", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Walls: hull edges", &show_wall_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Against walls the server tests every hull edge (rotated, rounded to 1/64 px).\n"
                              "Spikes and thin parts touch walls; the inside does not matter.");
        ImGui::Checkbox("Shots, items, balls: circle r=16", &show_hit_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Objects hit the ship when their distance to the centre is at most\n"
                              "16 + the object's radius - independent of the ship's shape!");
        if (show_hit_) {
            ImGui::Indent();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##obj", kObjTypes[obj_type_].name)) {
                for (int i = 0; i < (int)IM_ARRAYSIZE(kObjTypes); ++i)
                    if (ImGui::Selectable(kObjTypes[i].name, i == obj_type_)) obj_type_ = i;
                ImGui::EndCombo();
            }
            ImGui::Unindent();
        }
        ImGui::Checkbox("Ship vs. ship: r=13", &show_shipship_);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Two ships crash when their centres are less than 26 px apart.");
        ImGui::Checkbox("Shield (drawn by the client)", &show_shield_);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Direction");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-70);
        ImGui::SliderInt("##dir", &dir_, 0, kRes - 1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("One of the 128 ship directions; the red outline shows the hull\n"
                                                      "as the server rotates and rounds it for wall collisions.");
        ImGui::SameLine();
        ImGui::Checkbox("Spin", &spin_);

        double hit_area = M_PI * kShipSz * kShipSz;
        ImGui::TextDisabled("Hull covers ~%.0f%% of the shot hit circle", val_.grid_ok ? 100.0 * val_.area / hit_area : 0.0);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A small ship is not harder to hit with shots - only walls\nand the look depend on the shape.");
    }

    if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen)) DrawPreview();

    if (ImGui::CollapsingHeader("Measurements")) {
        ImGui::Text("Hull points: %d / %d", (int)doc_.hull().size(), kMaxPts);
        ImGui::Text("Width + length: %d (min %d)", val_.size_sum, kMinSizeSum);
        ImGui::Text("Width across longest axis: %.1f (min %d)", val_.width, kMinWidth);
        ImGui::Text("Zones front/rear/left/right: %d/%d/%d/%d", val_.hi, val_.low, val_.left, val_.right);
        ImGui::Text("Farthest point: %.1f px", val_.max_radius);
        ImGui::Text("Shield diameter: %d px", val_.shield_radius);
    }

    if (ImGui::CollapsingHeader("Points")) DrawPointLists();

    if (ImGui::CollapsingHeader("Shape string", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::InputTextMultiline("##text", &text_buf_, ImVec2(-1, ImGui::GetTextLineHeight() * 6)))
            text_edited_ = true;
        if (ImGui::Button("Copy")) CopyToClipboard();
        ImGui::SameLine();
        if (ImGui::Button("Paste")) PasteFromClipboard();
        ImGui::SameLine();
        ImGui::BeginDisabled(!text_edited_);
        if (ImGui::Button("Apply text")) { std::string t = text_buf_; text_edited_ = false; ApplyText(t); }
        ImGui::SameLine();
        if (ImGui::Button("Revert")) text_edited_ = false;
        ImGui::EndDisabled();
    }
}

void Editor::DrawPreview() {
    const float w = ImGui::GetContentRegionAvail().x;
    const float hgt = 120;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, {p0.x + w, p0.y + hgt}, IM_COL32(0, 0, 0, 255), 4);
    const auto& h = val_.resolved.hull();
    auto draw = [&](float cx, float sc, const char* label) {
        ImVec2 c{p0.x + cx, p0.y + hgt * 0.5f - 6};
        if (show_hit_) dl->AddCircle(c, kShipSz * sc, Alpha(kHitCol, 120), 64);
        std::vector<ImVec2> pts;
        for (IPt p : h) { FPt q = Rotate(p, dir_); pts.push_back({c.x + float(q.x) * sc, c.y - float(q.y) * sc}); }
        if (pts.size() >= 2) dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, std::max(1.0f, sc * 0.5f));
        if (val_.grid_ok && !val_.resolved.pts[kLeftLight].empty()) {
            for (IPt p : val_.resolved.pts[kLeftLight]) { FPt q = Rotate(p, dir_); dl->AddCircleFilled({c.x + float(q.x) * sc, c.y - float(q.y) * sc}, std::max(1.0f, sc * 0.7f), kPartColor[kLeftLight]); }
            for (IPt p : val_.resolved.pts[kRightLight]) { FPt q = Rotate(p, dir_); dl->AddCircleFilled({c.x + float(q.x) * sc, c.y - float(q.y) * sc}, std::max(1.0f, sc * 0.7f), kPartColor[kRightLight]); }
        }
        ImGui::PushFont(fonts_.small ? fonts_.small : ImGui::GetFont());
        dl->AddText({c.x - 8, p0.y + hgt - 17}, IM_COL32(150, 160, 180, 255), label);
        ImGui::PopFont();
    };
    draw(w * 0.18f, 1.0f, "1:1");
    draw(w * 0.6f, 2.5f, "2.5x");
    ImGui::Dummy({w, hgt});
}

void Editor::DrawPointLists() {
    for (int p = 0; p < kPartCount; ++p) {
        ImGui::PushID(p);
        auto& v = doc_.pts[p];
        ImGui::TextColored(ColV(kPartColor[p]), "%s (%d/%d)%s", Info(p).label, (int)v.size(), Info(p).max,
                           p > 0 && v.empty() ? " - default" : "");
        int del = -1;
        for (int i = 0; i < (int)v.size(); ++i) {
            ImGui::PushID(i);
            int xy[2] = {v[i].x, v[i].y};
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputInt2("##xy", xy)) {
                v[i] = {std::clamp(xy[0], -kLimit, kLimit), std::clamp(xy[1], -kLimit, kLimit)};
            }
            if (ImGui::IsItemActivated()) PushUndo();
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) del = i;
            if (p == kHull) {
                ImGui::SameLine();
                if (ImGui::SmallButton("^") && i > 0) { PushUndo(); std::swap(v[i], v[i - 1]); }
                ImGui::SameLine();
                if (ImGui::SmallButton("v") && i + 1 < (int)v.size()) { PushUndo(); std::swap(v[i], v[i + 1]); }
            }
            ImGui::PopID();
        }
        if (del >= 0) { PushUndo(); v.erase(v.begin() + del); }
        if ((int)v.size() < Info(p).max && ImGui::SmallButton("+ add")) {
            PushUndo();
            v.push_back(v.empty() ? (p == kHull ? IPt{0, 0} : val_.resolved.pts[p].empty() ? IPt{0, 0} : val_.resolved.pts[p][0]) : v.back());
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
}

// ============================================================================
// modals
// ============================================================================

void Editor::DrawModals() {
    if (pending_ != Pending::None && !ImGui::IsPopupOpen("Unsaved changes")) ImGui::OpenPopup("Unsaved changes");
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("\"%s\" has unsaved changes.", doc_.name.empty() ? "(unnamed)" : doc_.name.c_str());
        ImGui::Spacing();
        auto proceed = [&]() {
            if (pending_ == Pending::Quit) quit_ = true;
            else if (pending_ == Pending::Load) { SetDoc(pending_shape_, pending_origin_); Status("Loaded \"" + doc_.name + "\""); }
            else if (pending_ == Pending::New) { SetDoc(DefaultShip(), "new ship"); }
            pending_ = Pending::None;
            ImGui::CloseCurrentPopup();
        };
        if (ImGui::Button("Save to collection")) {
            SaveToCollection(true);
            if (!Dirty()) proceed();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) proceed();
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { pending_ = Pending::None; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    if (ask_replace_) { ImGui::OpenPopup("Replace ship?"); ask_replace_ = false; }
    if (ImGui::BeginPopupModal("Replace ship?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Your collection already has a ship named \"%s\".", doc_.name.c_str());
        if (replace_index_ >= 0 && replace_index_ < (int)mine_.size()) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, {p.x + 80, p.y + 80}, IM_COL32(5, 7, 12, 255), 4);
            DrawThumb(dl, mine_[replace_index_].shape, {p.x + 6, p.y + 6}, 68, IM_COL32(200, 205, 215, 255));
            dl->AddRectFilled({p.x + 100, p.y}, {p.x + 180, p.y + 80}, IM_COL32(5, 7, 12, 255), 4);
            DrawThumb(dl, doc_, {p.x + 106, p.y + 6}, 68, IM_COL32(120, 255, 160, 255));
            dl->AddText({p.x + 86, p.y + 30}, IM_COL32(200, 200, 200, 255), ">");
            ImGui::Dummy({180, 84});
        }
        if (ImGui::Button("Replace")) { SaveToCollection(true); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ask_rc_) { ImGui::OpenPopup("Use as my ship"); ask_rc_ = false; ReloadRc(); }
    if (ImGui::BeginPopupModal("Use as my ship", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Write this ship to xpilot.shipShape in");
        ImGui::TextDisabled("%s", ::XpilotrcPath().c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440);
        if (!val_.valid)
            ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "The ship is not valid: the client will reject it and fly the default ship.");
        for (auto& e : stock_)
            if (!doc_.name.empty() && e.shape.name == doc_.name && !(e.shape == doc_)) {
                ImGui::TextColored(ImVec4(1, 0.8f, 0.35f, 1),
                                   "A stock ship is named \"%s\" too. The launcher selects ships by name and would "
                                   "start the stock one - better rename your ship.", doc_.name.c_str());
                break;
            }
        if (!rc_value_.empty()) ImGui::TextDisabled("Replaces the current setting.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Write xpilotrc")) { SetInXpilotrc(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
