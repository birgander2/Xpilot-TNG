// Menus, tool palette, inspector tabs, dialogs and keyboard shortcuts.

#include "editor_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <misc/cpp/imgui_stdlib.h>

#include "map_io.h"

namespace fs = std::filesystem;
using namespace xpe;

namespace {

const ImVec4 kAccent(0.35f, 0.75f, 1.00f, 1.0f);
const ImVec4 kDim(0.55f, 0.60f, 0.68f, 1.0f);
const ImVec4 kWarn(1.00f, 0.75f, 0.30f, 1.0f);
const ImVec4 kErr(1.00f, 0.42f, 0.38f, 1.0f);

const uint32_t kTeamColors[kMaxTeams] = {0xD8D8D8, 0x4FA3FF, 0xFF5A5A, 0x5ADC6A, 0xFFD84A,
                                         0xC77DFF, 0x4AE0D6, 0xFF9A3C, 0xFF7AC8, 0xA0A0FF};

ImVec4 ColorVec(uint32_t c) {
    return ImVec4(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, 1.0f);
}

uint32_t VecColor(const float* f) {
    auto ch = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return (ch(f[0]) << 16) | (ch(f[1]) << 8) | ch(f[2]);
}

void Swatch(uint32_t c) {
    ImGui::ColorButton("##sw", ColorVec(c), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                       ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
    ImGui::SameLine();
}

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool Contains(const std::string& hay, const char* needle) {
    if (!needle[0]) return true;
    return Lower(hay).find(Lower(needle)) != std::string::npos;
}

struct PlaceEntry { Kind kind; const char* key; ImGuiKey imkey; };
const PlaceEntry kPlaceKinds[] = {
    {Kind::Base, "B", ImGuiKey_B},       {Kind::Fuel, "F", ImGuiKey_F},     {Kind::Check, "C", ImGuiKey_C},
    {Kind::Cannon, "N", ImGuiKey_N},     {Kind::Target, "T", ImGuiKey_T},   {Kind::Ball, "O", ImGuiKey_O},
    {Kind::Grav, "G", ImGuiKey_G},       {Kind::Wormhole, "W", ImGuiKey_W}, {Kind::ItemConc, "I", ImGuiKey_I},
    {Kind::AsteroidConc, "A", ImGuiKey_A},
};
const Kind kShapeKinds[] = {Kind::Wall, Kind::Decor, Kind::BallArea, Kind::BallTarget, Kind::FrictionArea};

const char* kGravLabels[] = {"Point (pos)", "Point (neg)", "Clockwise", "Anticlockwise",
                             "Up",          "Down",        "Right",     "Left"};
const char* kWormLabels[] = {"Normal (in & out)", "In (entry only)", "Out (exit only)", "Fixed"};
const char* kEdgeLineStyles[] = {"Solid", "Dashed", "Double dashed"};

struct GridChoice { int clicks; const char* label; };
const GridChoice kGrids[] = {{64, "1 px"},           {320, "5 px"},           {640, "10 px"},
                             {kBlockClicks / 4, "1/4 block"}, {kBlockClicks / 2, "1/2 block"},
                             {kBlockClicks, "1 block"},      {kBlockClicks * 2, "2 blocks"}};

void RotatePolys(Item& it, int ddir) {
    const double a = 2 * M_PI * ddir / kRes, c = std::cos(a), s = std::sin(a);
    for (auto& p : it.polys)
        for (auto& v : p.pts) {
            const double x = v.x - it.pos.x, y = v.y - it.pos.y;
            v = {it.pos.x + int(std::lround(x * c - y * s)), it.pos.y + int(std::lround(x * s + y * c))};
        }
}

} // namespace

// ============================================================================
// shared widgets
// ============================================================================

bool EditorApp::PolyStyleCombo(const char* label, int* style, bool allow_none, const char* none_label) {
    bool changed = false;
    const char* preview = *style >= 0 && *style < int(map_.poly_styles.size()) ? map_.poly_styles[*style].id.c_str()
                                                                               : none_label;
    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge)) {
        if (allow_none && ImGui::Selectable(none_label, *style < 0)) {
            *style = -1;
            changed = true;
        }
        for (int i = 0; i < int(map_.poly_styles.size()); i++) {
            ImGui::PushID(i);
            Swatch(map_.poly_styles[i].color);
            if (ImGui::Selectable(map_.poly_styles[i].id.c_str(), *style == i)) {
                *style = i;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool EditorApp::EdgeStyleCombo(const char* label, int* edge) {
    bool changed = false;
    std::string preview = *edge == kEdgeDefault    ? "Polygon default"
                          : *edge == kEdgeInternal ? "internal (hidden)"
                          : *edge >= 0 && *edge < int(map_.edge_styles.size()) ? map_.edge_styles[*edge].id
                                                                               : "Choose...";
    if (ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("Polygon default", *edge == kEdgeDefault)) {
            *edge = kEdgeDefault;
            changed = true;
        }
        if (ImGui::Selectable("internal (hidden)", *edge == kEdgeInternal)) {
            *edge = kEdgeInternal;
            changed = true;
        }
        for (int i = 0; i < int(map_.edge_styles.size()); i++) {
            ImGui::PushID(i);
            Swatch(map_.edge_styles[i].color);
            if (ImGui::Selectable(map_.edge_styles[i].id.c_str(), *edge == i)) {
                *edge = i;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool EditorApp::TeamCombo(const char* label, int* team, bool allow_none) {
    bool changed = false;
    char preview[16];
    if (*team >= 0) std::snprintf(preview, sizeof(preview), "Team %d", *team);
    else std::snprintf(preview, sizeof(preview), "None");
    if (ImGui::BeginCombo(label, preview)) {
        if (allow_none && ImGui::Selectable("None", *team < 0)) {
            *team = kNoTeam;
            changed = true;
        }
        for (int t = 0; t < kMaxTeams; t++) {
            ImGui::PushID(t);
            Swatch(kTeamColors[t]);
            char name[16];
            std::snprintf(name, sizeof(name), "Team %d", t);
            if (ImGui::Selectable(name, *team == t)) {
                *team = t;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool EditorApp::DirEditor(int* dir) {
    bool changed = false;
    ImGui::PushID("dir");
    // small dial: click or drag to point
    const float r = 26.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##dial", ImVec2(r * 2, r * 2));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + r, p.y + r);
    dl->AddCircleFilled(c, r, ImGui::GetColorU32(ImGuiCol_FrameBg));
    dl->AddCircle(c, r, ImGui::GetColorU32(ImGuiCol_Border));
    if (ImGui::IsItemActive()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        double a = std::atan2(-(m.y - c.y), m.x - c.x);
        int d = ((int(std::lround(a / (2 * M_PI) * kRes)) % kRes) + kRes) % kRes;
        if (d != *dir) {
            *dir = d;
            changed = true;
        }
    }
    const double a = 2 * M_PI * *dir / kRes;
    dl->AddLine(c, ImVec2(c.x + float(std::cos(a)) * (r - 3), c.y - float(std::sin(a)) * (r - 3)),
                ImGui::GetColorU32(kAccent), 2.5f);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(130);
    changed |= ImGui::SliderInt("Direction", dir, 0, kRes - 1, "%d");
    ImGui::TextDisabled("%.0f deg", *dir * 360.0 / kRes);
    static const struct { const char* l; int d; } quick[] = {{"Right", 0}, {"Up", 32}, {"Left", 64}, {"Down", 96}};
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine(0, 3);
        if (ImGui::SmallButton(quick[i].l)) {
            *dir = quick[i].d;
            changed = true;
        }
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

bool EditorApp::OptionValueEditor(const char* id, const std::string& name, std::string& value) {
    const OptionInfo* info = catalog_.Find(name);
    const std::string l = Lower(value);
    const bool looks_bool = l == "yes" || l == "no" || l == "true" || l == "false" || l == "on" || l == "off";
    ImGui::PushID(id);
    bool changed = false;
    if ((info && info->type == "bool") || (!info && looks_bool)) {
        bool b = IsTrue(value);
        if (ImGui::Checkbox("##v", &b)) {
            value = b ? "yes" : "no";
            changed = true;
        }
    } else {
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed = ImGui::InputText("##v", &value);
    }
    ImGui::PopID();
    return changed;
}

bool EditorApp::QuickOption(const char* label, const char* name, const char* tip) {
    const OptionInfo* info = catalog_.Find(name);
    const bool set = map_.FindOption(name) >= 0;
    std::string value = set ? map_.Option(name) : (info ? info->def : "");
    ImGui::PushID(name);
    bool changed = false;
    const bool is_bool = info ? info->type == "bool" : true;
    if (!set) ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    if (is_bool) {
        bool b = IsTrue(value);
        if (ImGui::Checkbox(label, &b)) {
            map_.SetOption(info ? info->name : name, b ? "yes" : "no");
            changed = true;
        }
    } else {
        ImGui::SetNextItemWidth(110);
        if (ImGui::InputText(label, &value)) {
            map_.SetOption(info ? info->name : name, value);
            changed = true;
        }
    }
    if (!set) ImGui::PopStyleColor();
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(tip);
        ImGui::TextDisabled("Option %s%s", name, set ? " (set in map)" : " (server default)");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (set) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            map_.RemoveOption(name);
            changed = true;
        }
        ImGui::SetItemTooltip("Remove from the map (use the server default)");
    }
    ImGui::PopID();
    if (changed) Changed();
    return changed;
}

// ============================================================================
// menu bar and toolbar
// ============================================================================

void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    const bool any = !sel_.empty();
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New...", "Ctrl+N")) Guard(Pending::New);
        if (ImGui::MenuItem("Open...", "Ctrl+O")) Guard(Pending::Open);
        if (ImGui::BeginMenu("Open recent", !recent_.empty())) {
            for (const auto& r : recent_)
                if (ImGui::MenuItem(r.c_str())) Guard(Pending::OpenPath, r);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) Save();
        if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) {
            snprintf(path_buf_, sizeof(path_buf_), "%s",
                     path_.empty() ? (maps_dir_ + "/newmap.xp2").c_str() : path_.c_str());
            open_save_popup_ = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) RequestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undo_.empty() || dirty_pending_)) Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redo_.empty())) Redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, any)) {
            CopySelection();
            DeleteSelection();
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, any)) CopySelection();
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !clipboard_.empty())) Paste();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, any)) DuplicateSelection();
        if (ImGui::MenuItem("Delete", "Del", false, any || sel_vertex_.Valid())) DeleteSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", "Ctrl+A")) SelectAll();
        if (ImGui::MenuItem("Select none", "Esc", false, any)) ClearSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Flip horizontally", "Shift+H", false, any)) TransformSelection(0);
        if (ImGui::MenuItem("Flip vertically", "Shift+V", false, any)) TransformSelection(1);
        if (ImGui::MenuItem("Rotate 90 degrees", "Shift+R", false, any)) TransformSelection(2);
        if (ImGui::MenuItem("Bring to front", "PgUp", false, any)) MoveSelectionInOrder(true);
        if (ImGui::MenuItem("Send to back", "PgDn", false, any)) MoveSelectionInOrder(false);
        if (ImGui::MenuItem("Fix orientation", nullptr, false))
            FixOrientation();
        ImGui::SetItemTooltip("Make polygons counter-clockwise (selection, or whole map if nothing is selected)");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fit map", "Home")) FitView();
        if (ImGui::MenuItem("Zoom in", "+")) ZoomAt(ImVec2(canvas_pos_.x + canvas_size_.x / 2, canvas_pos_.y + canvas_size_.y / 2), 1.25f);
        if (ImGui::MenuItem("Zoom out", "-")) ZoomAt(ImVec2(canvas_pos_.x + canvas_size_.x / 2, canvas_pos_.y + canvas_size_.y / 2), 0.8f);
        if (ImGui::MenuItem("Zoom to selection", "Z", false, any)) FocusItem(sel_[0]);
        ImGui::Separator();
        ImGui::MenuItem("Show grid", "Ctrl+G", &show_grid_);
        ImGui::MenuItem("Snap to grid", "S", &snap_);
        ImGui::MenuItem("Show labels", "L", &show_labels_);
        ImGui::MenuItem("Show log", nullptr, &show_log_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Map")) {
        if (ImGui::MenuItem(testing_ ? "Restart test play" : "Test play", "F5")) StartTestPlay();
        if (ImGui::MenuItem("Stop test play", nullptr, false, testing_)) StopTestPlay();
        ImGui::Separator();
        if (ImGui::MenuItem("Check for problems")) inspector_tab_request_ = 5;
        if (ImGui::MenuItem("Scale map...")) open_scale_popup_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Shortcuts & tips", "F1")) show_help_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void EditorApp::DrawToolbar() {
    if (ImGui::Button("New")) Guard(Pending::New);
    ImGui::SameLine();
    if (ImGui::Button("Open")) Guard(Pending::Open);
    ImGui::SameLine();
    if (ImGui::Button("Save")) Save();
    ImGui::SameLine(0, 18);
    ImGui::BeginDisabled(undo_.empty() && !dirty_pending_);
    if (ImGui::Button("Undo")) Undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(redo_.empty());
    if (ImGui::Button("Redo")) Redo();
    ImGui::EndDisabled();
    ImGui::SameLine(0, 18);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Grid");
    ImGui::SameLine();
    const char* grid_label = "custom";
    for (const auto& g : kGrids)
        if (g.clicks == grid_clicks_) grid_label = g.label;
    ImGui::SetNextItemWidth(100);
    if (ImGui::BeginCombo("##grid", grid_label)) {
        for (const auto& g : kGrids)
            if (ImGui::Selectable(g.label, g.clicks == grid_clicks_)) grid_clicks_ = g.clicks;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &snap_);
    ImGui::SameLine();
    ImGui::Checkbox("Show", &show_grid_);
    ImGui::SameLine(0, 18);
    if (ImGui::Button("Fit")) FitView();
    ImGui::SetItemTooltip("Show the whole map (Home)");

    // test play button on the right
    const char* play = testing_ ? "Stop test" : "Test play";
    const float bw = ImGui::CalcTextSize(play).x + ImGui::GetStyle().FramePadding.x * 2 + 10;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw);
    if (testing_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.24f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.14f, 0.14f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.45f, 0.20f, 1.0f));
    }
    if (ImGui::Button(play, ImVec2(bw, 0))) {
        if (testing_) StopTestPlay();
        else StartTestPlay();
    }
    ImGui::PopStyleColor(3);
    ImGui::SetItemTooltip("Start a local server with this map and join it (F5)");
}

// ============================================================================
// tool palette
// ============================================================================

void EditorApp::DrawToolPalette() {
    auto tool_button = [&](const char* label, const char* key, bool active) {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.20f, 0.42f, 0.62f, 1.0f)
                                                      : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        const bool pressed = ImGui::Button(label, ImVec2(-FLT_MIN, 0));
        ImGui::PopStyleColor();
        // shortcut hint at the right edge
        ImVec2 mx = ImGui::GetItemRectMax();
        ImVec2 ks = ImGui::CalcTextSize(key);
        ImGui::GetWindowDrawList()->AddText(ImVec2(mx.x - ks.x - 6, mx.y - ks.y - 3), ImGui::GetColorU32(kDim), key);
        return pressed;
    };
    auto set_tool = [&](Tool t) {
        if (t != Tool::Polygon) draw_pts_.clear();
        tool_ = t;
    };

    if (fonts_.heading) ImGui::PushFont(fonts_.heading);
    ImGui::TextColored(kAccent, "Tools");
    if (fonts_.heading) ImGui::PopFont();
    if (tool_button("Select / move", "V", tool_ == Tool::Select)) set_tool(Tool::Select);

    ImGui::SeparatorText("Draw");
    if (tool_button("Polygon", "P", tool_ == Tool::Polygon)) set_tool(Tool::Polygon);
    if (tool_button("Rectangle", "R", tool_ == Tool::Rect)) set_tool(Tool::Rect);
    if (tool_button("Ellipse", "E", tool_ == Tool::Ellipse)) set_tool(Tool::Ellipse);

    ImGui::SeparatorText("Place");
    for (const auto& pk : kPlaceKinds) {
        ImGui::PushID(int(pk.kind));
        if (tool_button(KindName(pk.kind), pk.key, tool_ == Tool::Place && place_kind_ == pk.kind)) {
            set_tool(Tool::Place);
            place_kind_ = pk.kind;
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Options");
    ImGui::PushItemWidth(-FLT_MIN);
    if (tool_ == Tool::Polygon || tool_ == Tool::Rect || tool_ == Tool::Ellipse) {
        ImGui::TextUnformatted("Create as");
        if (ImGui::BeginCombo("##shape_kind", KindName(shape_kind_))) {
            for (Kind k : kShapeKinds)
                if (ImGui::Selectable(KindName(k), shape_kind_ == k)) {
                    shape_kind_ = k;
                    shape_style_ = -1;
                }
            ImGui::EndCombo();
        }
        ImGui::TextUnformatted("Style");
        std::string def = std::string("Default (") + DefaultStyleFor(shape_kind_) + ")";
        PolyStyleCombo("##shape_style", &shape_style_, true, def.c_str());
        if (shape_kind_ == Kind::BallTarget) {
            ImGui::TextUnformatted("Team");
            TeamCombo("##shape_team", &shape_team_, true);
        }
        if (shape_kind_ == Kind::FrictionArea) {
            ImGui::TextUnformatted("Friction");
            ImGui::DragFloat("##fric", &shape_friction_, 0.01f, 0.0f, 1.0f, "%.2f");
        }
        if (tool_ == Tool::Ellipse) {
            ImGui::TextUnformatted("Segments");
            ImGui::SliderInt("##segs", &ellipse_segments_, 4, 64);
        }
        ImGui::PopItemWidth();
        ImGui::Checkbox("Add to selection", &add_to_selected_);
        ImGui::PushItemWidth(-FLT_MIN);
        HelpMarker("Adds the new polygon to the selected decoration, ball area, cannon, target, wormhole ... "
                   "instead of creating a new object.");
    } else if (tool_ == Tool::Place) {
        if (KindHasTeam(place_kind_)) {
            ImGui::TextUnformatted("Team");
            int& team = place_teams_[int(place_kind_)];
            if (place_kind_ == Kind::Base && team < 0) team = 0;
            TeamCombo("##place_team", &team, place_kind_ != Kind::Base);
        }
        if (KindHasDir(place_kind_)) {
            ImGui::PopItemWidth();
            DirEditor(&place_dir_);
            ImGui::PushItemWidth(-FLT_MIN);
        }
        if (place_kind_ == Kind::Grav) {
            ImGui::TextUnformatted("Type");
            ImGui::Combo("##gtype", &place_grav_type_, kGravLabels, IM_ARRAYSIZE(kGravLabels));
            ImGui::TextUnformatted("Force");
            ImGui::DragFloat("##gforce", &place_grav_force_, 0.05f, -20.0f, 20.0f, "%.2f");
        }
        if (place_kind_ == Kind::Wormhole) {
            ImGui::TextUnformatted("Type");
            ImGui::Combo("##wtype", &place_worm_type_, kWormLabels, IM_ARRAYSIZE(kWormLabels));
        }
        if (place_kind_ == Kind::Check || place_kind_ == Kind::Fuel || place_kind_ == Kind::ItemConc ||
            place_kind_ == Kind::AsteroidConc)
            ImGui::TextDisabled("No options.");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextWrapped("Select objects to edit them in the inspector on the right.");
        ImGui::PopStyleColor();
    }
    ImGui::PopItemWidth();
}

// ============================================================================
// inspector
// ============================================================================

void EditorApp::DrawInspector() {
    if (!ImGui::BeginTabBar("##inspector_tabs")) return;
    static const char* names[] = {"Selection", "Map", "Styles", "Options", "Objects", "Problems"};
    for (int i = 0; i < 6; i++) {
        ImGuiTabItemFlags f = inspector_tab_request_ == i ? ImGuiTabItemFlags_SetSelected : 0;
        std::string label = names[i];
        if (i == 5) {
            if (problems_rev_ != revision_) {
                problems_ = Validate(map_);
                problems_rev_ = revision_;
            }
            int errors = 0;
            for (const auto& p : problems_) errors += p.error;
            if (!problems_.empty()) label += " (" + std::to_string(problems_.size()) + ")";
            label += "###problems";
            if (errors) ImGui::PushStyleColor(ImGuiCol_Text, kErr);
            bool open = ImGui::BeginTabItem(label.c_str(), nullptr, f);
            if (errors) ImGui::PopStyleColor();
            if (open) {
                ImGui::BeginChild("##tab");
                DrawProblemsTab();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            continue;
        }
        if (ImGui::BeginTabItem(label.c_str(), nullptr, f)) {
            ImGui::BeginChild("##tab");
            switch (i) {
            case 0: DrawPropertiesTab(); break;
            case 1: DrawMapTab(); break;
            case 2: DrawStylesTab(); break;
            case 3: DrawOptionsTab(); break;
            case 4: DrawObjectsTab(); break;
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
    }
    inspector_tab_request_ = -1;
    ImGui::EndTabBar();
}

void EditorApp::DrawPropertiesTab() {
    if (sel_.empty()) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::Spacing();
        ImGui::TextWrapped("Click an object on the map to edit it, or pick a tool on the left to draw walls and "
                           "place bases, fuel stations, cannons and more.");
        ImGui::Spacing();
        int counts[int(Kind::Count)] = {};
        for (const auto& it : map_.items) counts[int(it.kind)]++;
        if (ImGui::BeginTable("##counts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            for (int k = 0; k < int(Kind::Count); k++) {
                if (!counts[k]) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(KindName(Kind(k)));
                ImGui::TableNextColumn();
                ImGui::Text("%d", counts[k]);
            }
            ImGui::EndTable();
        }
        return;
    }
    if (sel_.size() == 1) {
        DrawItemProperties(map_.items[sel_[0]], sel_[0]);
        return;
    }

    // multiple selection
    if (fonts_.heading) ImGui::PushFont(fonts_.heading);
    ImGui::Text("%d objects selected", int(sel_.size()));
    if (fonts_.heading) ImGui::PopFont();
    int counts[int(Kind::Count)] = {};
    bool any_team = false, any_poly = false;
    for (int i : sel_) {
        const Item& it = map_.items[i];
        counts[int(it.kind)]++;
        any_team |= KindHasTeam(it.kind);
        any_poly |= !it.polys.empty();
    }
    for (int k = 0; k < int(Kind::Count); k++)
        if (counts[k]) ImGui::BulletText("%d x %s", counts[k], KindName(Kind(k)));
    ImGui::Spacing();
    ImGui::PushItemWidth(-140);
    if (any_team) {
        int team = -2;
        for (int i : sel_)
            if (KindHasTeam(map_.items[i].kind)) {
                if (team == -2) team = map_.items[i].team;
                else if (team != map_.items[i].team) team = -3;
            }
        int t = team < -1 ? kNoTeam : team;
        if (TeamCombo("Team of all", &t, true)) {
            for (int i : sel_) {
                Item& it = map_.items[i];
                if (!KindHasTeam(it.kind)) continue;
                it.team = (it.kind == Kind::Base && t < 0) ? 0 : t;
            }
            Changed();
        }
        if (team == -3) ImGui::TextDisabled("(teams differ)");
    }
    if (any_poly) {
        int style = -1;
        if (PolyStyleCombo("Style of all", &style, false, "(mixed)")) {
            for (int i : sel_)
                for (auto& p : map_.items[i].polys) p.style = style;
            Changed();
        }
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();
    if (ImGui::Button("Duplicate")) DuplicateSelection();
    ImGui::SameLine();
    if (ImGui::Button("Flip H")) TransformSelection(0);
    ImGui::SameLine();
    if (ImGui::Button("Flip V")) TransformSelection(1);
    ImGui::SameLine();
    if (ImGui::Button("Rotate")) TransformSelection(2);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("Delete")) DeleteSelection();
    ImGui::PopStyleColor();
}

void EditorApp::DrawItemProperties(Item& it, int index) {
    if (fonts_.heading) ImGui::PushFont(fonts_.heading);
    ImGui::TextColored(kAccent, "%s", KindName(it.kind));
    if (fonts_.heading) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("#%d", index);
    ImGui::PushItemWidth(-130);

    // polygon containers can be turned into each other
    if (KindIsContainer(it.kind)) {
        if (ImGui::BeginCombo("Type", KindName(it.kind))) {
            for (Kind k : kShapeKinds) {
                if (!ImGui::Selectable(KindName(k), it.kind == k) || k == it.kind) continue;
                if (k == Kind::Wall && it.polys.size() > 1) {
                    // a wall is a single polygon: split into one wall per polygon
                    std::vector<Polygon> polys = std::move(it.polys);
                    map_.items.erase(map_.items.begin() + index);
                    std::vector<Item> walls;
                    for (auto& p : polys) {
                        Item w;
                        w.kind = Kind::Wall;
                        w.polys.push_back(std::move(p));
                        walls.push_back(std::move(w));
                    }
                    map_.items.insert(map_.items.begin() + index, walls.begin(), walls.end());
                    sel_.clear();
                    for (int i = 0; i < int(walls.size()); i++) sel_.push_back(index + i);
                    sel_vertex_ = sel_edge_ = {};
                    Changed();
                    ImGui::EndCombo();
                    ImGui::PopItemWidth();
                    return;
                }
                it.kind = k;
                if (k != Kind::BallTarget) it.team = kNoTeam;
                Changed();
            }
            ImGui::EndCombo();
        }
    }

    if (KindHasPos(it.kind)) {
        float pos[2] = {it.pos.x / float(kClick), it.pos.y / float(kClick)};
        if (ImGui::DragFloat2("Position (px)", pos, 1.0f, 0.0f, 0.0f, "%.1f")) {
            const int dx = int(std::lround(pos[0] * kClick)) - it.pos.x;
            const int dy = int(std::lround(pos[1] * kClick)) - it.pos.y;
            TranslateItem(it, dx, dy);     // owned polygons move along
            Changed();
        }
        HelpMarker("Map coordinates in pixels, origin bottom left. Polygons of cannons, targets and wormholes "
                   "move together with the object.");
    }
    if (KindHasTeam(it.kind)) {
        if (TeamCombo("Team", &it.team, it.kind != Kind::Base)) Changed();
    }
    if (it.kind == Kind::Base) {
        if (ImGui::InputInt("Order", &it.order)) Changed();
        HelpMarker("Bases with a lower order are used first when players join.");
    }
    if (KindHasDir(it.kind)) {
        ImGui::PopItemWidth();
        int dir = it.dir;
        if (DirEditor(&dir)) {
            if (it.kind == Kind::Cannon) RotatePolys(it, dir - it.dir);
            it.dir = dir;
            Changed();
        }
        ImGui::PushItemWidth(-130);
    }
    if (it.kind == Kind::Grav) {
        if (ImGui::Combo("Type", &it.type, kGravLabels, IM_ARRAYSIZE(kGravLabels))) Changed();
        float f = float(it.value);
        if (ImGui::DragFloat("Force", &f, 0.05f, -50.0f, 50.0f, "%.2f")) {
            it.value = f;
            Changed();
        }
        HelpMarker("Converted block maps use -2.7 for attracting (+) and 2.7 for repelling (-) point gravity.");
    }
    if (it.kind == Kind::Wormhole) {
        if (ImGui::Combo("Type", &it.type, kWormLabels, IM_ARRAYSIZE(kWormLabels))) Changed();
        HelpMarker("Ships enter a wormhole by touching its polygon. Exit-only wormholes need no polygon.");
    }
    if (it.kind == Kind::FrictionArea) {
        float f = float(it.value);
        if (ImGui::DragFloat("Friction", &f, 0.01f, 0.0f, 1.0f, "%.3f")) {
            it.value = f;
            Changed();
        }
    }
    if (it.kind == Kind::Ball) {
        if (PolyStyleCombo("Ball style", &it.style, true, "(client default)")) Changed();
        HelpMarker("A ball belongs to the ball target of the same team. Bring it to the other team's target to score.");
    }
    if (it.kind == Kind::BallTarget) HelpMarker("Balls of this team must be delivered here to score.");
    if (it.kind == Kind::Check) {
        int num = 0, prev = -1, next = -1;
        for (int i = 0; i < int(map_.items.size()); i++) {
            if (map_.items[i].kind != Kind::Check) continue;
            if (i < index) {
                num++;
                prev = i;
            } else if (i > index && next < 0) {
                next = i;
            }
        }
        ImGui::Text("Checkpoint %d", num + 1);
        ImGui::SameLine();
        ImGui::BeginDisabled(prev < 0);
        if (ImGui::SmallButton("Earlier")) {
            std::swap(map_.items[prev], map_.items[index]);
            sel_ = {prev};
            Changed();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(next < 0);
        if (ImGui::SmallButton("Later")) {
            std::swap(map_.items[next], map_.items[index]);
            sel_ = {next};
            Changed();
        }
        ImGui::EndDisabled();
        HelpMarker("Race mode (option 'timing') visits the checkpoints in this order.");
    }
    ImGui::PopItemWidth();

    // per object options of bases and cannons
    if (it.kind == Kind::Base || it.kind == Kind::Cannon) {
        ImGui::SeparatorText(it.kind == Kind::Base ? "Base options" : "Cannon options");
        int remove = -1;
        for (int i = 0; i < int(it.options.size()); i++) {
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(it.options[i].first.c_str());
            ImGui::SameLine(170);
            ImGui::SetNextItemWidth(-40);
            if (ImGui::InputText("##v", &it.options[i].second)) Changed();
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove = i;
            ImGui::PopID();
        }
        if (remove >= 0) {
            it.options.erase(it.options.begin() + remove);
            Changed();
        }
        static const char* item_opts[] = {"initialFuel", "initialArmor", "initialMissiles", "initialMines",
                                          "initialLasers", "initialEcms", "initialCloaks", "initialSensors",
                                          "initialAfterburners", "initialTanks", "initialTransporters",
                                          "initialWideangles", "initialRearshots", "initialTractorBeams",
                                          "initialAutopilots", "initialEmergencyThrusts", "initialEmergencyShields",
                                          "initialDeflectors", "initialHyperJumps", "initialPhasings", "initialMirrors"};
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##addopt", "Add option...")) {
            if (it.kind == Kind::Cannon) {
                if (ImGui::Selectable("smartness")) { it.options.emplace_back("smartness", "1"); Changed(); }
                if (ImGui::Selectable("shotSpeed")) { it.options.emplace_back("shotSpeed", "20"); Changed(); }
            }
            for (const char* o : item_opts)
                if (ImGui::Selectable(o)) {
                    it.options.emplace_back(o, "1");
                    Changed();
                }
            ImGui::EndCombo();
        }
    }

    if (!it.polys.empty() || KindHasPolys(it.kind)) DrawPolygonProperties(it, index);

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Duplicate")) DuplicateSelection();
    ImGui::SameLine();
    if (ImGui::Button("Zoom to")) FocusItem(index);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("Delete")) {
        sel_vertex_ = {};
        DeleteSelection();
    }
    ImGui::PopStyleColor();
}

void EditorApp::DrawPolygonProperties(Item& it, int index) {
    ImGui::SeparatorText(it.polys.size() == 1 ? "Polygon" : "Polygons");
    if (it.polys.empty()) {
        ImGui::TextDisabled("No polygon.");
        ImGui::TextWrapped("Draw one with the polygon tool and 'Add to selection' enabled.");
        return;
    }
    const bool owner = it.kind == Kind::Cannon || it.kind == Kind::Target;
    int remove = -1;
    for (int pi = 0; pi < int(it.polys.size()); pi++) {
        Polygon& p = it.polys[pi];
        ImGui::PushID(pi);
        char head[64];
        std::snprintf(head, sizeof(head), "Polygon %d  (%d points)", pi + 1, int(p.pts.size()));
        bool open = it.polys.size() == 1 || ImGui::TreeNodeEx(head, ImGuiTreeNodeFlags_DefaultOpen);
        if (it.polys.size() == 1) ImGui::TextDisabled("%d points, area %.0f px2", int(p.pts.size()),
                                                      std::fabs(SignedArea(p.pts)) / (kClick * kClick));
        if (open) {
            ImGui::PushItemWidth(-130);
            if (PolyStyleCombo("Style", &p.style, false)) Changed();
            if (owner || p.destroyed_style >= 0) {
                if (PolyStyleCombo("Destroyed style", &p.destroyed_style, true)) Changed();
                HelpMarker("Style used after the cannon or target has been destroyed.");
            }
            int all = -3;
            if (EdgeStyleCombo("All edges", &all)) {
                for (int& e : p.edges) e = all;
                Changed();
            }
            if (p.pts.size() >= 3 && SignedArea(p.pts) < 0) {
                ImGui::TextColored(kWarn, "Clockwise outline");
                ImGui::SameLine();
                if (ImGui::SmallButton("Fix")) {
                    EnsureCCW(p);
                    Changed();
                }
            }
            if (sel_vertex_.Valid() && sel_vertex_.item == index && sel_vertex_.poly == pi &&
                sel_vertex_.index < int(p.pts.size())) {
                Pt& v = p.pts[sel_vertex_.index];
                float xy[2] = {v.x / float(kClick), v.y / float(kClick)};
                char lab[32];
                std::snprintf(lab, sizeof(lab), "Vertex %d", sel_vertex_.index + 1);
                if (ImGui::DragFloat2(lab, xy, 1.0f, 0.0f, 0.0f, "%.1f")) {
                    v = {int(std::lround(xy[0] * kClick)), int(std::lround(xy[1] * kClick))};
                    Changed();
                }
                if (ImGui::SmallButton("Delete vertex")) DeleteSelectedVertex();
            }
            if (sel_edge_.Valid() && sel_edge_.item == index && sel_edge_.poly == pi &&
                sel_edge_.index < int(p.edges.size())) {
                char lab[32];
                std::snprintf(lab, sizeof(lab), "Edge %d", sel_edge_.index + 1);
                if (EdgeStyleCombo(lab, &p.edges[sel_edge_.index])) Changed();
            }
            ImGui::PopItemWidth();
            if (it.polys.size() > 1 || !KindIsContainer(it.kind)) {
                if (ImGui::SmallButton("Remove polygon")) remove = pi;
            }
            if (it.polys.size() > 1) ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        it.polys.erase(it.polys.begin() + remove);
        sel_vertex_ = sel_edge_ = {};
        Changed();
    }
    ImGui::TextDisabled("Drag the square handles to move points.\nDouble-click an edge to add a point.");
}

// ============================================================================
// map tab
// ============================================================================

void EditorApp::DrawMapTab() {
    ImGui::PushItemWidth(-130);
    ImGui::SeparatorText("General");
    std::string name = map_.Option("mapName");
    if (ImGui::InputText("Name", &name)) {
        map_.SetOption("mapName", name);
        Changed();
    }
    std::string author = map_.Option("mapAuthor");
    if (ImGui::InputText("Author", &author)) {
        map_.SetOption("mapAuthor", author);
        Changed();
    }
    std::string desc = map_.Attr("description");
    if (ImGui::InputTextMultiline("Description", &desc, ImVec2(-130, ImGui::GetTextLineHeight() * 3.5f))) {
        map_.SetAttr("description", desc);
        Changed();
    }
    HelpMarker("Short text shown in the launcher's map list.");
    std::string prio = map_.Attr("launcherprio");
    if (ImGui::InputText("Priority", &prio, ImGuiInputTextFlags_CharsDecimal)) {
        map_.SetAttr("launcherprio", prio);
        Changed();
    }
    HelpMarker("Maps with a higher priority are listed first in the launcher. Leave empty for none.");

    ImGui::SeparatorText("Size");
    int w = map_.WidthPx(), h = map_.HeightPx();
    if (ImGui::InputInt("Width (px)", &w, 35, 350) && w > 0) {
        map_.SetOption("mapWidth", std::to_string(w));
        Changed();
    }
    if (ImGui::InputInt("Height (px)", &h, 35, 350) && h > 0) {
        map_.SetOption("mapHeight", std::to_string(h));
        Changed();
    }
    ImGui::TextDisabled("%.1f x %.1f blocks (1 block = %d px)", w / double(kBlockPx), h / double(kBlockPx), kBlockPx);
    if (ImGui::Button("Scale map...")) open_scale_popup_ = true;
    ImGui::SameLine();
    ImGui::TextDisabled("resizes the map including its content");

    ImGui::SeparatorText("World");
    QuickOption("Edge wrap", "edgeWrap", "The world wraps around at its borders.");
    QuickOption("Edge bounce", "edgeBounce", "Ships bounce off the world borders (when edge wrap is off).");
    QuickOption("Gravity", "gravity", "Global gravity strength.");
    QuickOption("Gravity angle", "gravityAngle", "Direction of the global gravity in degrees.");

    ImGui::SeparatorText("Game mode");
    QuickOption("Team play", "teamPlay", "Players are organised in teams by their base's team.");
    QuickOption("Limited lives", "limitedLives", "Players have a limited number of lives per round.");
    QuickOption("Lives", "worldLives", "Number of lives with limited lives.");
    QuickOption("Race (timing)", "timing", "Race mode: fly through the checkpoints in order.");
    QuickOption("Race laps", "raceLaps", "Number of laps in race mode.");
    QuickOption("Capture the flag", "captureTheFlag", "Treasures must be brought home while the own one is safe.");
    QuickOption("Team immunity", "teamImmunity", "Team mates cannot harm each other.");
    QuickOption("Team cannons", "teamCannons", "Cannons belong to the team of the closest base.");
    QuickOption("Team fuel", "teamFuel", "Fuel stations belong to teams.");

    ImGui::SeparatorText("Robots");
    QuickOption("Robots", "maxRobots", "Maximum number of classic robots.");
    QuickOption("Smart robots", "maxSmartRobots", "Maximum number of smart robots.");
    QuickOption("Robot team", "robotTeam", "Team the robots play in (with reserveRobotTeam).");
    QuickOption("Reserve robot team", "reserveRobotTeam", "Humans cannot join the robot team.");
    ImGui::PopItemWidth();
    ImGui::Spacing();
    ImGui::TextDisabled("Grey values are server defaults. All options: 'Options' tab.");
}

// ============================================================================
// styles tab
// ============================================================================

void EditorApp::DrawStylesTab() {
    static int replace_with = -1;
    static int delete_kind = -1;    // 0 poly, 1 edge
    auto unique_id = [](const std::string& base, auto exists) {
        std::string id = base;
        for (int n = 2; exists(id); n++) id = base + std::to_string(n);
        return id;
    };

    // --- polygon styles ---
    if (ImGui::CollapsingHeader("Polygon styles", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Fill and default edge of polygons.");
        if (ImGui::BeginListBox("##ps", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 6.5f))) {
            for (int i = 0; i < int(map_.poly_styles.size()); i++) {
                ImGui::PushID(i);
                Swatch(map_.poly_styles[i].color);
                char lab[160];
                std::snprintf(lab, sizeof(lab), "%s  (%d uses)", map_.poly_styles[i].id.c_str(), map_.CountPolyStyleUse(i));
                if (ImGui::Selectable(lab, sel_poly_style_ == i)) sel_poly_style_ = i;
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
        if (ImGui::Button("New##ps")) {
            PolyStyle ps;
            ps.id = unique_id("style", [&](const std::string& id) { return map_.FindPolyStyle(id) >= 0; });
            ps.defedge = map_.edge_styles.empty() ? -1 : 0;
            ps.flags = kStyleFilled;
            map_.poly_styles.push_back(ps);
            sel_poly_style_ = int(map_.poly_styles.size()) - 1;
            Changed();
        }
        ImGui::SameLine();
        const bool valid = sel_poly_style_ >= 0 && sel_poly_style_ < int(map_.poly_styles.size());
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Duplicate##ps")) {
            PolyStyle ps = map_.poly_styles[sel_poly_style_];
            ps.id = unique_id(ps.id, [&](const std::string& id) { return map_.FindPolyStyle(id) >= 0; });
            map_.poly_styles.push_back(ps);
            sel_poly_style_ = int(map_.poly_styles.size()) - 1;
            Changed();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete##ps")) {
            delete_kind = 0;
            replace_with = -1;
            ImGui::OpenPopup("Delete style");
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to selection") && valid) {
            for (int i : sel_)
                for (auto& p : map_.items[i].polys) p.style = sel_poly_style_;
            Changed();
        }
        ImGui::EndDisabled();
        if (valid) {
            PolyStyle& ps = map_.poly_styles[sel_poly_style_];
            ImGui::PushItemWidth(-130);
            std::string id = ps.id;
            if (ImGui::InputText("Id##ps", &id, ImGuiInputTextFlags_EnterReturnsTrue) && !id.empty() &&
                map_.FindPolyStyle(id) < 0) {
                ps.id = id;
                Changed();
            }
            HelpMarker("Press Enter to rename. References are updated automatically.");
            float col[3];
            ImVec4 cv = ColorVec(ps.color);
            col[0] = cv.x, col[1] = cv.y, col[2] = cv.z;
            if (ImGui::ColorEdit3("Color##ps", col)) {
                ps.color = VecColor(col);
                Changed();
            }
            bool f;
            f = ps.flags & kStyleFilled;
            if (ImGui::Checkbox("Filled", &f)) { ps.flags ^= kStyleFilled; Changed(); }
            ImGui::SameLine();
            f = ps.flags & kStyleTextured;
            if (ImGui::Checkbox("Textured", &f)) { ps.flags ^= kStyleTextured; Changed(); }
            f = ps.flags & kStyleInvisible;
            if (ImGui::Checkbox("Invisible", &f)) { ps.flags ^= kStyleInvisible; Changed(); }
            ImGui::SameLine();
            f = ps.flags & kStyleInvisibleRadar;
            if (ImGui::Checkbox("Not on radar", &f)) { ps.flags ^= kStyleInvisibleRadar; Changed(); }
            const char* tex = ps.texture >= 0 && ps.texture < int(map_.bmp_styles.size())
                                  ? map_.bmp_styles[ps.texture].id.c_str() : "(none)";
            if (ImGui::BeginCombo("Texture", tex)) {
                if (ImGui::Selectable("(none)", ps.texture < 0)) { ps.texture = -1; Changed(); }
                for (int i = 0; i < int(map_.bmp_styles.size()); i++)
                    if (ImGui::Selectable(map_.bmp_styles[i].id.c_str(), ps.texture == i)) {
                        ps.texture = i;
                        ps.flags |= kStyleTextured;
                        Changed();
                    }
                ImGui::EndCombo();
            }
            const char* de = ps.defedge >= 0 && ps.defedge < int(map_.edge_styles.size())
                                 ? map_.edge_styles[ps.defedge].id.c_str() : "(missing!)";
            if (ImGui::BeginCombo("Default edge", de)) {
                for (int i = 0; i < int(map_.edge_styles.size()); i++) {
                    ImGui::PushID(i);
                    Swatch(map_.edge_styles[i].color);
                    if (ImGui::Selectable(map_.edge_styles[i].id.c_str(), ps.defedge == i)) {
                        ps.defedge = i;
                        Changed();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
        }
    }

    // --- edge styles ---
    if (ImGui::CollapsingHeader("Edge styles", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Line width, color and pattern of polygon edges.");
        if (ImGui::BeginListBox("##es", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 5.5f))) {
            for (int i = 0; i < int(map_.edge_styles.size()); i++) {
                ImGui::PushID(i);
                Swatch(map_.edge_styles[i].color);
                char lab[160];
                std::snprintf(lab, sizeof(lab), "%s  (%d uses)", map_.edge_styles[i].id.c_str(), map_.CountEdgeStyleUse(i));
                if (ImGui::Selectable(lab, sel_edge_style_ == i)) sel_edge_style_ = i;
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
        if (ImGui::Button("New##es")) {
            EdgeStyle es;
            es.id = unique_id("edge", [&](const std::string& id) { return map_.FindEdgeStyle(id) >= 0; });
            map_.edge_styles.push_back(es);
            sel_edge_style_ = int(map_.edge_styles.size()) - 1;
            Changed();
        }
        ImGui::SameLine();
        const bool valid = sel_edge_style_ >= 0 && sel_edge_style_ < int(map_.edge_styles.size());
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Duplicate##es")) {
            EdgeStyle es = map_.edge_styles[sel_edge_style_];
            es.id = unique_id(es.id, [&](const std::string& id) { return map_.FindEdgeStyle(id) >= 0; });
            map_.edge_styles.push_back(es);
            sel_edge_style_ = int(map_.edge_styles.size()) - 1;
            Changed();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete##es")) {
            delete_kind = 1;
            replace_with = -1;
            ImGui::OpenPopup("Delete style");
        }
        ImGui::EndDisabled();
        if (valid) {
            EdgeStyle& es = map_.edge_styles[sel_edge_style_];
            ImGui::PushItemWidth(-130);
            std::string id = es.id;
            if (ImGui::InputText("Id##es", &id, ImGuiInputTextFlags_EnterReturnsTrue) && !id.empty() &&
                id != "internal" && map_.FindEdgeStyle(id) < 0) {
                es.id = id;
                Changed();
            }
            HelpMarker("Press Enter to rename. References are updated automatically.");
            if (ImGui::SliderInt("Width", &es.width, -1, 8)) Changed();
            HelpMarker("-1 hides the edge of filled polygons.");
            float col[3];
            ImVec4 cv = ColorVec(es.color);
            col[0] = cv.x, col[1] = cv.y, col[2] = cv.z;
            if (ImGui::ColorEdit3("Color##es", col)) {
                es.color = VecColor(col);
                Changed();
            }
            if (ImGui::Combo("Pattern", &es.style, kEdgeLineStyles, IM_ARRAYSIZE(kEdgeLineStyles))) Changed();
            ImGui::PopItemWidth();
        }
    }

    // --- bitmaps ---
    if (ImGui::CollapsingHeader("Textures")) {
        ImGui::TextDisabled("Bitmaps from lib/textures used by textured styles.");
        if (ImGui::BeginListBox("##bs", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 4.5f))) {
            for (int i = 0; i < int(map_.bmp_styles.size()); i++) {
                ImGui::PushID(i);
                if (ImGui::Selectable(map_.bmp_styles[i].id.c_str(), sel_bmp_style_ == i)) sel_bmp_style_ = i;
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
        if (ImGui::Button("New##bs")) {
            BmpStyle bs;
            bs.filename = texture_files_.empty() ? "" : texture_files_[0];
            bs.id = unique_id(bs.filename.empty() ? std::string("texture") : bs.filename,
                              [&](const std::string& id) { return map_.FindBmpStyle(id) >= 0; });
            map_.bmp_styles.push_back(bs);
            sel_bmp_style_ = int(map_.bmp_styles.size()) - 1;
            Changed();
        }
        ImGui::SameLine();
        const bool valid = sel_bmp_style_ >= 0 && sel_bmp_style_ < int(map_.bmp_styles.size());
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Delete##bs")) {
            map_.RemoveBmpStyle(sel_bmp_style_);
            sel_bmp_style_ = -1;
            Changed();
        }
        ImGui::EndDisabled();
        if (valid) {
            BmpStyle& bs = map_.bmp_styles[sel_bmp_style_];
            ImGui::PushItemWidth(-130);
            std::string id = bs.id;
            if (ImGui::InputText("Id##bs", &id, ImGuiInputTextFlags_EnterReturnsTrue) && !id.empty() &&
                map_.FindBmpStyle(id) < 0) {
                bs.id = id;
                Changed();
            }
            HelpMarker("Press Enter to rename. References are updated automatically.");
            if (ImGui::BeginCombo("File", bs.filename.c_str(), ImGuiComboFlags_HeightLarge)) {
                for (const auto& f : texture_files_)
                    if (ImGui::Selectable(f.c_str(), f == bs.filename)) {
                        bs.filename = f;
                        Changed();
                    }
                ImGui::EndCombo();
            }
            bool sc = bs.flags & 1;
            if (ImGui::Checkbox("Scalable", &sc)) {
                bs.flags ^= 1;
                Changed();
            }
            ImGui::PopItemWidth();
        }
    }

    // replacement dialog for deleting a style that is still in use
    if (ImGui::BeginPopupModal("Delete style", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool poly = delete_kind == 0;
        const int idx = poly ? sel_poly_style_ : sel_edge_style_;
        const int uses = poly ? map_.CountPolyStyleUse(idx) : map_.CountEdgeStyleUse(idx);
        const std::string& id = poly ? map_.poly_styles[idx].id : map_.edge_styles[idx].id;
        ImGui::Text("Delete style '%s'?", id.c_str());
        if (uses > 0) {
            ImGui::Text("It is used %d times. Replace it with:", uses);
            ImGui::SetNextItemWidth(250);
            if (poly) PolyStyleCombo("##repl", &replace_with, false, "(choose)");
            else {
                if (replace_with < 0) replace_with = kEdgeDefault;
                EdgeStyleCombo("##repl", &replace_with);
            }
        }
        const bool ok = uses == 0 || (replace_with != idx && (poly ? replace_with >= 0 : true));
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button("Delete", ImVec2(100, 0))) {
            if (poly) {
                map_.RemovePolyStyle(idx, replace_with);
                sel_poly_style_ = -1;
                if (shape_style_ >= int(map_.poly_styles.size())) shape_style_ = -1;
            } else {
                map_.RemoveEdgeStyle(idx, replace_with);
                sel_edge_style_ = -1;
            }
            Changed();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ============================================================================
// options tab
// ============================================================================

void EditorApp::DrawOptionsTab() {
    static bool show_all = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Search options...", option_filter_, sizeof(option_filter_));
    const auto& cats = OptionCatalog::Categories();
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##cat", option_category_ == 0 ? "All categories" : cats[option_category_ - 1].c_str())) {
        if (ImGui::Selectable("All categories", option_category_ == 0)) option_category_ = 0;
        for (int i = 0; i < int(cats.size()); i++)
            if (ImGui::Selectable(cats[i].c_str(), option_category_ == i + 1)) option_category_ = i + 1;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show unset", &show_all);
    ImGui::SetItemTooltip("Also list options the map does not set (with the server default)");
    if (!catalog_ok_)
        ImGui::TextColored(kWarn, "Server option list unavailable (%s).", server_bin_.c_str());

    auto tooltip = [&](const OptionInfo* info, const std::string& name) {
        if (!ImGui::BeginItemTooltip()) return;
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextColored(kAccent, "%s", info ? info->name.c_str() : name.c_str());
        if (info) {
            ImGui::TextUnformatted(info->help.c_str());
            ImGui::TextDisabled("Type: %s   Default: %s", info->type.c_str(), info->def.empty() ? "-" : info->def.c_str());
            if (!info->aliases.empty()) {
                std::string a;
                for (const auto& s : info->aliases) a += (a.empty() ? "" : ", ") + s;
                ImGui::TextDisabled("Also: %s", a.c_str());
            }
            if (!info->map_ok) ImGui::TextColored(kWarn, "This option cannot be set in a map file.");
        } else {
            ImGui::TextColored(kWarn, "Unknown to this server version.");
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    };
    auto visible = [&](const std::string& name, const OptionInfo* info) {
        if (!Contains(name, option_filter_) && !(info && Contains(info->help, option_filter_))) return false;
        if (option_category_ > 0 && (!info || info->category != cats[option_category_ - 1])) return false;
        return true;
    };

    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##opts", 3, tf)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Option", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 22.0f);
    ImGui::TableHeadersRow();

    int remove = -1;
    for (int i = 0; i < int(map_.options.size()); i++) {
        auto& [name, value] = map_.options[i];
        const OptionInfo* info = catalog_.Find(name);
        if (!visible(name, info)) continue;
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (!info && catalog_ok_) ImGui::TextColored(kWarn, "%s", name.c_str());
        else ImGui::TextUnformatted(name.c_str());
        tooltip(info, name);
        ImGui::TableNextColumn();
        if (OptionValueEditor("val", name, value)) Changed();
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("x")) remove = i;
        ImGui::SetItemTooltip("Remove from the map");
        ImGui::PopID();
    }
    if (remove >= 0) {
        map_.options.erase(map_.options.begin() + remove);
        Changed();
    }
    if (show_all) {
        int row = 0;
        for (const auto& info : catalog_.All()) {
            if (!info.map_ok || info.invisible || map_.FindOption(info.name) >= 0) continue;
            bool alias_set = false;
            for (const auto& a : info.aliases) alias_set |= map_.FindOption(a) >= 0;
            if (alias_set || !visible(info.name, &info)) continue;
            ImGui::PushID(10000 + row++);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kDim, "%s", info.name.c_str());
            tooltip(&info, info.name);
            ImGui::TableNextColumn();
            std::string v = info.def;
            ImGui::PushStyleColor(ImGuiCol_Text, kDim);
            if (OptionValueEditor("val", info.name, v)) {
                map_.SetOption(info.name, v);
                Changed();
            }
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

// ============================================================================
// objects & problems
// ============================================================================

void EditorApp::DrawObjectsTab() {
    std::vector<int> by_kind[int(Kind::Count)];
    for (int i = 0; i < int(map_.items.size()); i++) by_kind[int(map_.items[i].kind)].push_back(i);
    for (int k = 0; k < int(Kind::Count); k++) {
        if (by_kind[k].empty()) continue;
        char head[64];
        std::snprintf(head, sizeof(head), "%s (%d)###k%d", KindName(Kind(k)), int(by_kind[k].size()), k);
        if (!ImGui::TreeNode(head)) continue;
        ImGuiListClipper clip;
        clip.Begin(int(by_kind[k].size()));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                const int i = by_kind[k][r];
                const Item& it = map_.items[i];
                char lab[128];
                Box b = ItemBounds(it);
                const int x = b.Valid() ? (b.lo.x + b.hi.x) / 2 / kClick : 0;
                const int y = b.Valid() ? (b.lo.y + b.hi.y) / 2 / kClick : 0;
                if (it.team >= 0 && KindHasTeam(it.kind))
                    std::snprintf(lab, sizeof(lab), "#%d  team %d  (%d, %d)", i, it.team, x, y);
                else
                    std::snprintf(lab, sizeof(lab), "#%d  (%d, %d)", i, x, y);
                if (ImGui::Selectable(lab, IsSelected(i))) {
                    Select(i, ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift);
                    FocusItem(i);
                }
            }
        }
        ImGui::TreePop();
    }
}

void EditorApp::DrawProblemsTab() {
    if (problems_.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "No problems found.");
        ImGui::TextDisabled("Use 'Test play' to try the map in the real game.");
        return;
    }
    for (int i = 0; i < int(problems_.size()); i++) {
        const Problem& p = problems_[i];
        ImGui::PushID(i);
        ImGui::TextColored(p.error ? kErr : kWarn, p.error ? "Error" : "Warning");
        ImGui::SameLine(80);
        ImGui::PushTextWrapPos(0);
        if (p.item >= 0) {
            if (ImGui::Selectable(p.text.c_str(), IsSelected(p.item))) {
                Select(p.item);
                FocusItem(p.item);
            }
        } else {
            ImGui::TextUnformatted(p.text.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::PopID();
    }
}

// ============================================================================
// status bar & log
// ============================================================================

void EditorApp::DrawStatusBar() {
    ImGui::Separator();
    const char* hint = "";
    switch (tool_) {
    case Tool::Select:
        hint = "Click to select, drag to move, Shift+click adds. Drag on empty space selects an area. Right-drag pans.";
        break;
    case Tool::Polygon:
        hint = draw_pts_.empty() ? "Click to set the first point."
                                 : "Click to add points. Click the first point, double-click or Enter finishes. "
                                   "Backspace undoes a point, Esc cancels. Shift: 45 degree steps.";
        break;
    case Tool::Rect:
    case Tool::Ellipse: hint = "Drag to draw the shape."; break;
    case Tool::Place:
        hint = KindHasDir(place_kind_) ? "Click to place, or press and drag to set the direction."
                                       : "Click to place.";
        break;
    }
    // object under the cursor (highlighted on the canvas but not selected yet)
    if (hover_item_ >= 0 && hover_item_ < int(map_.items.size()) && !IsSelected(hover_item_)) {
        const Item& it = map_.items[hover_item_];
        if (KindHasTeam(it.kind) && it.team >= 0)
            ImGui::Text("%s (team %d)", KindName(it.kind), it.team);
        else
            ImGui::TextUnformatted(KindName(it.kind));
        ImGui::SameLine(0, 16);
    }
    if (!message_.empty() && ImGui::GetTime() < message_until_) ImGui::TextColored(kAccent, "%s", message_.c_str());
    else ImGui::TextDisabled("%s", hint);

    char right[160];
    if (mouse_in_canvas_)
        std::snprintf(right, sizeof(right), "x %.1f  y %.1f  (block %d, %d)   |   %d selected   |   %d objects",
                      mouse_wx_ / kClick, mouse_wy_ / kClick, int(std::floor(mouse_wx_ / kBlockClicks)),
                      int(std::floor(mouse_wy_ / kBlockClicks)), int(sel_.size()), int(map_.items.size()));
    else
        std::snprintf(right, sizeof(right), "%d selected   |   %d objects", int(sel_.size()), int(map_.items.size()));
    const float w = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20, ImGui::GetWindowContentRegionMax().x - w));
    ImGui::TextDisabled("%s", right);
}

void EditorApp::DrawLogWindow() {
    ImGui::BeginChild("##log", ImVec2(0, 170), ImGuiChildFlags_Borders);
    ImGui::TextColored(kAccent, "Test play log");
    ImGui::SameLine();
    if (testing_) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), proc_.IsServerReady() ? "(running)" : "(starting)");
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 110);
    if (ImGui::SmallButton("Clear")) proc_.ClearLogs();
    ImGui::SameLine();
    if (ImGui::SmallButton("Hide")) show_log_ = false;
    ImGui::BeginChild("##loglines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (fonts_.small) ImGui::PushFont(fonts_.small);
    for (const auto& e : proc_.GetLogs()) {
        const bool server = e.tag == "SERVER";
        const bool bad = e.text.find("rror") != std::string::npos || e.text.find("arn") != std::string::npos ||
                         e.text.find("nknown") != std::string::npos;
        ImGui::TextColored(bad ? kWarn : (server ? ImVec4(0.75f, 0.80f, 0.88f, 1.0f) : kDim), "%s [%s] %s",
                           e.timestamp.c_str(), e.tag.c_str(), e.text.c_str());
    }
    if (fonts_.small) ImGui::PopFont();
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::EndChild();
}

// ============================================================================
// popups
// ============================================================================

void EditorApp::DrawPopups() {
    if (open_new_popup_) { ImGui::OpenPopup("New map"); open_new_popup_ = false; }
    if (open_open_popup_) { ImGui::OpenPopup("Open map"); open_open_popup_ = false; }
    if (open_save_popup_) { ImGui::OpenPopup("Save map as"); open_save_popup_ = false; }
    if (open_unsaved_popup_) { ImGui::OpenPopup("Unsaved changes"); open_unsaved_popup_ = false; }
    if (open_warnings_popup_) { ImGui::OpenPopup("Messages"); open_warnings_popup_ = false; }
    if (open_scale_popup_) { ImGui::OpenPopup("Scale map"); open_scale_popup_ = false; }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Size of the new map in pixels (1 block = 35 px):");
        ImGui::SetNextItemWidth(200);
        ImGui::InputInt("Width", &new_w_, 35, 350);
        ImGui::SetNextItemWidth(200);
        ImGui::InputInt("Height", &new_h_, 35, 350);
        new_w_ = std::clamp(new_w_, 100, 100000);
        new_h_ = std::clamp(new_h_, 100, 100000);
        static const int presets[] = {1750, 3500, 7000};
        for (int i = 0; i < 3; i++) {
            if (i) ImGui::SameLine();
            char l[32];
            std::snprintf(l, sizeof(l), "%d x %d", presets[i], presets[i]);
            if (ImGui::Button(l)) new_w_ = new_h_ = presets[i];
        }
        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            NewDocument(new_w_, new_h_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Open map", nullptr)) {
        std::string open_now;
        if (!browse_open_.empty()) {
            open_now = browse_open_;
            browse_open_.clear();
        }
        ImGui::TextColored(kAccent, "Maps in %s", maps_dir_.c_str());
        const float list_h = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 3 -
                             (recent_.empty() ? 0 : 130);
        if (ImGui::BeginListBox("##maps", ImVec2(-FLT_MIN, list_h))) {
            for (const auto& f : map_files_) {
                const std::string full = maps_dir_ + "/" + f;
                if (ImGui::Selectable(f.c_str(), full == path_buf_, ImGuiSelectableFlags_AllowDoubleClick)) {
                    snprintf(path_buf_, sizeof(path_buf_), "%s", full.c_str());
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open_now = full;
                }
            }
            ImGui::EndListBox();
        }
        if (!recent_.empty()) {
            ImGui::TextColored(kAccent, "Recent");
            if (ImGui::BeginListBox("##recent", ImVec2(-FLT_MIN, 100))) {
                for (const auto& r : recent_)
                    if (ImGui::Selectable(r.c_str(), r == path_buf_, ImGuiSelectableFlags_AllowDoubleClick)) {
                        snprintf(path_buf_, sizeof(path_buf_), "%s", r.c_str());
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open_now = r;
                    }
                ImGui::EndListBox();
            }
        }
        ImGui::SetNextItemWidth(-110);
        if (ImGui::InputTextWithHint("##path", "Path to an .xp2 file", path_buf_, sizeof(path_buf_),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
            open_now = path_buf_;
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(-FLT_MIN, 0))) BrowseOpen();
        ImGui::BeginDisabled(!path_buf_[0]);
        if (ImGui::Button("Open", ImVec2(120, 0))) open_now = path_buf_;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        if (!open_now.empty()) {
            ImGui::CloseCurrentPopup();
            OpenDocument(open_now);
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save map as", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!browse_save_.empty()) {
            snprintf(path_buf_, sizeof(path_buf_), "%s", browse_save_.c_str());
            browse_save_.clear();
        }
        ImGui::TextUnformatted("File name:");
        ImGui::SetNextItemWidth(480);
        bool go = ImGui::InputText("##savepath", path_buf_, sizeof(path_buf_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) BrowseSave();
        std::error_code ec;
        if (path_buf_[0] && fs::exists(path_buf_, ec) && path_buf_ != path_)
            ImGui::TextColored(kWarn, "The file exists and will be overwritten.");
        if (Lower(fs::path(path_buf_).extension().string()) == ".xpd")
            ImGui::TextColored(kWarn, "Use the .xp2 extension; .xpd bundles cannot be written.");
        ImGui::Spacing();
        go |= ImGui::Button("Save", ImVec2(120, 0));
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            pending_ = Pending::None;
            ImGui::CloseCurrentPopup();
        }
        if (go && path_buf_[0] && Lower(fs::path(path_buf_).extension().string()) != ".xpd") {
            ImGui::CloseCurrentPopup();
            SaveDocument(path_buf_);
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save changes to %s?", path_.empty() ? "the new map" : fs::path(path_).filename().c_str());
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(110, 0))) {
            ImGui::CloseCurrentPopup();
            Save();     // runs the pending action once saved (Save As asks for a name first)
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save", ImVec2(110, 0))) {
            ImGui::CloseCurrentPopup();
            RunPending();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) {
            pending_ = Pending::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Messages", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
        for (const auto& w : load_warnings_) ImGui::BulletText("%s", w.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Scale map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static float factor = 1.0f;
        ImGui::TextUnformatted("Scales the map size and every coordinate.");
        ImGui::SetNextItemWidth(200);
        ImGui::DragFloat("Factor", &factor, 0.01f, 0.1f, 10.0f, "%.2f");
        ImGui::TextDisabled("New size: %d x %d px", int(map_.WidthPx() * factor), int(map_.HeightPx() * factor));
        if (ImGui::Button("Scale", ImVec2(120, 0)) && factor > 0) {
            auto sc = [&](Pt& p) { p = {int(std::lround(p.x * double(factor))), int(std::lround(p.y * double(factor)))}; };
            for (auto& it : map_.items) {
                sc(it.pos);
                for (auto& p : it.polys)
                    for (auto& v : p.pts) sc(v);
            }
            map_.SetOption("mapWidth", std::to_string(int(std::lround(map_.WidthPx() * factor))));
            map_.SetOption("mapHeight", std::to_string(int(std::lround(map_.HeightPx() * factor))));
            Changed();
            need_fit_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void EditorApp::DrawHelpWindow() {
    ImGui::SetNextWindowSize(ImVec2(560, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Shortcuts & tips", &show_help_)) {
        ImGui::End();
        return;
    }
    static const char* keys[][2] = {
        {"V", "Select / move tool"},
        {"P / R / E", "Draw polygon / rectangle / ellipse"},
        {"B F C N T O G W I A", "Place base, fuel, checkpoint, cannon, target, ball, gravity, wormhole, item / asteroid concentrator"},
        {"Mouse wheel", "Zoom at the cursor"},
        {"Right / middle drag, Space+drag", "Pan the view"},
        {"Home", "Fit the whole map"},
        {"Z", "Zoom to the selection"},
        {"Shift+click", "Add to / remove from the selection"},
        {"Double-click edge", "Insert a vertex (selected polygon)"},
        {"Del", "Delete selection or selected vertex"},
        {"Arrow keys", "Nudge selection by 1 px (Shift: one grid step)"},
        {"Ctrl+D / C / V / X", "Duplicate / copy / paste / cut"},
        {"Shift+H / V / R", "Flip horizontally / vertically, rotate 90 degrees"},
        {"PgUp / PgDn", "Bring to front / send to back"},
        {"Ctrl+Z / Ctrl+Y", "Undo / redo"},
        {"S / Ctrl+G / L", "Toggle snapping / grid / labels"},
        {"Enter / Backspace / Esc", "Polygon tool: finish / remove last point / cancel"},
        {"F5", "Test play the map"},
        {"Ctrl+S / Ctrl+O / Ctrl+N", "Save / open / new"},
    };
    if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 200);
        for (const auto& k : keys) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(kAccent, "%s", k[0]);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", k[1]);
        }
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Tips");
    ImGui::TextWrapped(
        "- Walls are polygons. Their look comes from polygon styles (fill) and edge styles (lines), see 'Styles'.\n"
        "- Every base needs a team (0-9). With teamPlay the bases' teams form the teams.\n"
        "- A ball game needs a ball target and a ball per team; the ball area marks where balls may be.\n"
        "- Cannons, targets and wormholes get a default polygon that you can edit like a wall.\n"
        "- Checkpoints are numbered in race order; reorder them in the Selection tab.\n"
        "- Coordinates are in pixels with the origin at the bottom left, like in the game.");
    ImGui::End();
}

// ============================================================================
// keyboard
// ============================================================================

void EditorApp::HandleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    auto pressed = [](ImGuiKey k, bool repeat = false) { return ImGui::IsKeyPressed(k, repeat); };
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;

    if (ctrl) {
        if (pressed(ImGuiKey_N)) Guard(Pending::New);
        if (pressed(ImGuiKey_O)) Guard(Pending::Open);
        if (pressed(ImGuiKey_S)) {
            if (shift) {
                snprintf(path_buf_, sizeof(path_buf_), "%s", path_.empty() ? (maps_dir_ + "/newmap.xp2").c_str() : path_.c_str());
                open_save_popup_ = true;
            } else {
                Save();
            }
        }
        if (pressed(ImGuiKey_Z, true)) shift ? Redo() : Undo();
        if (pressed(ImGuiKey_Y, true)) Redo();
        if (pressed(ImGuiKey_C)) CopySelection();
        if (pressed(ImGuiKey_X)) {
            CopySelection();
            DeleteSelection();
        }
        if (pressed(ImGuiKey_V)) Paste();
        if (pressed(ImGuiKey_D)) DuplicateSelection();
        if (pressed(ImGuiKey_A)) SelectAll();
        if (pressed(ImGuiKey_Q)) RequestQuit();
        if (pressed(ImGuiKey_G)) show_grid_ = !show_grid_;
        return;
    }
    if (alt) return;

    if (pressed(ImGuiKey_F1)) show_help_ = !show_help_;
    if (pressed(ImGuiKey_F5)) StartTestPlay();
    if (pressed(ImGuiKey_Home)) FitView();
    const ImVec2 mid(canvas_pos_.x + canvas_size_.x / 2, canvas_pos_.y + canvas_size_.y / 2);
    if (pressed(ImGuiKey_Equal, true) || pressed(ImGuiKey_KeypadAdd, true))
        ZoomAt(mouse_in_canvas_ ? io.MousePos : mid, 1.25f);
    if (pressed(ImGuiKey_Minus, true) || pressed(ImGuiKey_KeypadSubtract, true))
        ZoomAt(mouse_in_canvas_ ? io.MousePos : mid, 0.8f);

    // polygon tool keys
    if (tool_ == Tool::Polygon && !draw_pts_.empty()) {
        if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) FinishPolygon();
        if (pressed(ImGuiKey_Backspace, true)) draw_pts_.pop_back();
        if (pressed(ImGuiKey_Escape)) draw_pts_.clear();
        return;
    }
    if (pressed(ImGuiKey_Escape)) {
        if (sel_vertex_.Valid() || sel_edge_.Valid()) sel_vertex_ = sel_edge_ = {};
        else if (!sel_.empty()) ClearSelection();
        else tool_ = Tool::Select;
    }
    if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace)) DeleteSelection();
    if (pressed(ImGuiKey_PageUp)) MoveSelectionInOrder(true);
    if (pressed(ImGuiKey_PageDown)) MoveSelectionInOrder(false);

    if (shift) {
        if (pressed(ImGuiKey_H)) TransformSelection(0);
        if (pressed(ImGuiKey_V)) TransformSelection(1);
        if (pressed(ImGuiKey_R)) TransformSelection(2);
    } else {
        auto set_tool = [&](Tool t) {
            draw_pts_.clear();
            tool_ = t;
        };
        if (pressed(ImGuiKey_V)) set_tool(Tool::Select);
        if (pressed(ImGuiKey_P)) set_tool(Tool::Polygon);
        if (pressed(ImGuiKey_R)) set_tool(Tool::Rect);
        if (pressed(ImGuiKey_E)) set_tool(Tool::Ellipse);
        for (const auto& pk : kPlaceKinds)
            if (pressed(pk.imkey)) {
                set_tool(Tool::Place);
                place_kind_ = pk.kind;
            }
        if (pressed(ImGuiKey_S)) snap_ = !snap_;
        if (pressed(ImGuiKey_L)) show_labels_ = !show_labels_;
        if (pressed(ImGuiKey_Z) && !sel_.empty()) FocusItem(sel_[0]);
    }

    // nudge
    int dx = 0, dy = 0;
    const int step = shift ? std::max(grid_clicks_, kClick) : kClick;
    if (pressed(ImGuiKey_LeftArrow, true)) dx = -step;
    if (pressed(ImGuiKey_RightArrow, true)) dx = step;
    if (pressed(ImGuiKey_UpArrow, true)) dy = step;
    if (pressed(ImGuiKey_DownArrow, true)) dy = -step;
    if ((dx || dy) && (!sel_.empty() || sel_vertex_.Valid())) {
        if (sel_vertex_.Valid()) {
            Pt& v = map_.items[sel_vertex_.item].polys[sel_vertex_.poly].pts[sel_vertex_.index];
            v.x += dx;
            v.y += dy;
        } else {
            for (int i : sel_) TranslateItem(map_.items[i], dx, dy);
        }
        Changed();
    }
}
