// Map canvas: view transform, drawing and the mouse driven tools.

#include "editor_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace xpe;

namespace {

ImU32 Rgb(uint32_t c, int alpha = 255) {
    return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, alpha);
}

const uint32_t kTeamColors[kMaxTeams] = {0xD8D8D8, 0x4FA3FF, 0xFF5A5A, 0x5ADC6A, 0xFFD84A,
                                         0xC77DFF, 0x4AE0D6, 0xFF9A3C, 0xFF7AC8, 0xA0A0FF};

uint32_t TeamColor(int team) {
    return team >= 0 && team < kMaxTeams ? kTeamColors[team] : 0x9AA4B0;
}

constexpr ImU32 kSelColor = IM_COL32(255, 210, 60, 255);
constexpr ImU32 kHoverColor = IM_COL32(255, 255, 255, 170);

void DashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness, float dash = 6.0f) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const int n = std::min(400, int(len / dash));
    if (n < 2) {
        dl->AddLine(a, b, col, thickness);
        return;
    }
    for (int i = 0; i < n; i += 2) {
        const float t0 = float(i) / n, t1 = std::min(1.0f, float(i + 1) / n);
        dl->AddLine(ImVec2(a.x + dx * t0, a.y + dy * t0), ImVec2(a.x + dx * t1, a.y + dy * t1), col, thickness);
    }
}

void CenteredText(ImDrawList* dl, ImVec2 c, ImU32 col, const char* text) {
    ImVec2 sz = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), col, text);
}

double Dist(ImVec2 a, ImVec2 b) {
    return std::sqrt(double(a.x - b.x) * (a.x - b.x) + double(a.y - b.y) * (a.y - b.y));
}

} // namespace

// ============================================================================
// view transform
// ============================================================================

ImVec2 EditorApp::ToScreen(double x, double y) const {
    return ImVec2(canvas_pos_.x + canvas_size_.x * 0.5f + float((x / kClick - view_x_) * zoom_),
                  canvas_pos_.y + canvas_size_.y * 0.5f - float((y / kClick - view_y_) * zoom_));
}

ImVec2 EditorApp::ToScreen(Pt p) const { return ToScreen(double(p.x), double(p.y)); }

void EditorApp::ToWorld(ImVec2 s, double* x, double* y) const {
    *x = ((s.x - canvas_pos_.x - canvas_size_.x * 0.5f) / zoom_ + view_x_) * kClick;
    *y = (-(s.y - canvas_pos_.y - canvas_size_.y * 0.5f) / zoom_ + view_y_) * kClick;
}

Pt EditorApp::Snap(double x, double y) const {
    if (!snap_ || grid_clicks_ <= 1) return {int(std::lround(x)), int(std::lround(y))};
    const double g = grid_clicks_;
    return {int(std::lround(x / g) * grid_clicks_), int(std::lround(y / g) * grid_clicks_)};
}

Pt EditorApp::SnapDelta(int dx, int dy, Pt anchor) const {
    if (!snap_) return {dx, dy};
    Pt t = Snap(double(anchor.x) + dx, double(anchor.y) + dy);
    return {t.x - anchor.x, t.y - anchor.y};
}

float EditorApp::ObjectRadius() const { return std::max(7.0f, kBlockPx * 0.42f * zoom_); }

void EditorApp::FitView() {
    if (canvas_size_.x < 50 || canvas_size_.y < 50) return;
    const double w = map_.WidthPx(), h = map_.HeightPx();
    zoom_ = float(std::min(canvas_size_.x / w, canvas_size_.y / h) * 0.94);
    view_x_ = w / 2;
    view_y_ = h / 2;
    need_fit_ = false;
}

void EditorApp::ZoomAt(ImVec2 s, float factor) {
    double wx, wy;
    ToWorld(s, &wx, &wy);
    zoom_ = std::clamp(zoom_ * factor, 0.005f, 40.0f);
    view_x_ = wx / kClick - (s.x - canvas_pos_.x - canvas_size_.x * 0.5f) / zoom_;
    view_y_ = wy / kClick + (s.y - canvas_pos_.y - canvas_size_.y * 0.5f) / zoom_;
}

// ============================================================================
// hit testing
// ============================================================================

int EditorApp::HitItem(ImVec2 s) const {
    const float r = ObjectRadius() + 3;
    const int n = int(map_.items.size());
    for (int i = n - 1; i >= 0; i--) {
        const Item& it = map_.items[i];
        if (KindHasPos(it.kind) && Dist(ToScreen(it.pos), s) <= r) return i;
    }
    // edges first, so walls inside large areas (ball areas, decorations) stay clickable
    for (int i = n - 1; i >= 0; i--) {
        for (const auto& p : map_.items[i].polys) {
            const size_t m = p.pts.size();
            for (size_t k = 0; k < m; k++) {
                ImVec2 a = ToScreen(p.pts[k]), b = ToScreen(p.pts[(k + 1) % m]);
                if (DistToSegment(s.x, s.y, {int(a.x), int(a.y)}, {int(b.x), int(b.y)}) <= 5) return i;
            }
        }
    }
    double wx, wy;
    ToWorld(s, &wx, &wy);
    for (int i = n - 1; i >= 0; i--)
        for (const auto& p : map_.items[i].polys)
            if (p.pts.size() >= 3 && PointInPolygon(p.pts, wx, wy)) return i;
    return -1;
}

EditorApp::VertexRef EditorApp::HitVertex(ImVec2 s) const {
    if (sel_.size() != 1) return {};
    const Item& it = map_.items[sel_[0]];
    for (int pi = 0; pi < int(it.polys.size()); pi++) {
        const auto& pts = it.polys[pi].pts;
        for (int k = int(pts.size()) - 1; k >= 0; k--)
            if (Dist(ToScreen(pts[k]), s) <= 7) return {sel_[0], pi, k};
    }
    return {};
}

bool EditorApp::HitEdge(ImVec2 s, VertexRef* edge, Pt* at) const {
    if (sel_.size() != 1) return false;
    const Item& it = map_.items[sel_[0]];
    for (int pi = 0; pi < int(it.polys.size()); pi++) {
        const auto& pts = it.polys[pi].pts;
        const int m = int(pts.size());
        for (int k = 0; k < m; k++) {
            ImVec2 a = ToScreen(pts[k]), b = ToScreen(pts[(k + 1) % m]);
            double t = 0;
            if (DistToSegment(s.x, s.y, {int(a.x), int(a.y)}, {int(b.x), int(b.y)}, &t) <= 5) {
                *edge = {sel_[0], pi, k};
                const Pt& pa = pts[k];
                const Pt& pb = pts[(k + 1) % m];
                *at = Snap(pa.x + t * (pb.x - pa.x), pa.y + t * (pb.y - pa.y));
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// object creation
// ============================================================================

void EditorApp::AddShape(std::vector<Pt> pts) {
    if (pts.size() < 3) return;
    Kind kind = shape_kind_;
    int style = shape_style_ >= 0 && shape_style_ < int(map_.poly_styles.size())
                    ? shape_style_
                    : map_.EnsureBuiltinPolyStyle(DefaultStyleFor(kind));
    Polygon poly = MakePolygon(std::move(pts), style);
    EnsureCCW(poly);

    if (add_to_selected_ && sel_.size() == 1 && KindHasPolys(map_.items[sel_[0]].kind) &&
        map_.items[sel_[0]].kind != Kind::Wall) {
        map_.items[sel_[0]].polys.push_back(std::move(poly));
        Message("Polygon added to the selected " + std::string(KindName(map_.items[sel_[0]].kind)) + ".");
        Changed();
        return;
    }
    Item it;
    it.kind = kind;
    if (kind == Kind::BallTarget) it.team = shape_team_;
    if (kind == Kind::FrictionArea) it.value = shape_friction_;
    it.polys.push_back(std::move(poly));
    map_.items.push_back(std::move(it));
    Select(int(map_.items.size()) - 1);
    Changed();
}

void EditorApp::FinishPolygon() {
    if (draw_pts_.size() >= 3) AddShape(draw_pts_);
    else if (!draw_pts_.empty()) Message("A polygon needs at least 3 points.");
    draw_pts_.clear();
}

void EditorApp::PlaceObject(Pt pos, int dir) {
    Item it = MakeItem(map_, place_kind_, pos);
    if (KindHasTeam(place_kind_)) {
        it.team = place_teams_[int(place_kind_)];
        if (place_kind_ == Kind::Base && it.team < 0) it.team = 0;
    }
    if (KindHasDir(place_kind_)) {
        it.dir = dir;
        if (place_kind_ == Kind::Cannon) RebuildCannonPoly(map_, it);
    }
    if (place_kind_ == Kind::Grav) {
        it.type = place_grav_type_;
        it.value = place_grav_force_;
    }
    if (place_kind_ == Kind::Wormhole) {
        it.type = place_worm_type_;
        if (it.type == int(WormType::Out)) it.polys.clear();    // exits cannot be entered
    }
    map_.items.push_back(std::move(it));
    Select(int(map_.items.size()) - 1);
    Changed();
}

// ============================================================================
// input
// ============================================================================

void EditorApp::HandleCanvasInput(bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 m = io.MousePos;
    mouse_in_canvas_ = hovered;
    ToWorld(m, &mouse_wx_, &mouse_wy_);
    space_down_ = !io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_Space);
    const bool shift = io.KeyShift;

    if (hovered && io.MouseWheel != 0.0f) ZoomAt(m, std::pow(1.18f, io.MouseWheel));

    // --- panning: middle button, right button drag, space + left button
    if (hovered && drag_ == Drag::None &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (space_down_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
        drag_ = Drag::Pan;
    if (drag_ == Drag::Pan) {
        view_x_ -= io.MouseDelta.x / zoom_;
        view_y_ += io.MouseDelta.y / zoom_;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            drag_ = Drag::None;
        return;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        rmb_active_ = true;
        rmb_panned_ = false;
        rmb_start_ = m;
    }
    if (rmb_active_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (!rmb_panned_ && Dist(m, rmb_start_) > 4) rmb_panned_ = true;
            if (rmb_panned_) {
                view_x_ -= io.MouseDelta.x / zoom_;
                view_y_ += io.MouseDelta.y / zoom_;
            }
        } else {
            rmb_active_ = false;
            if (!rmb_panned_ && hovered) {
                if (tool_ == Tool::Polygon && !draw_pts_.empty()) {
                    FinishPolygon();
                } else {
                    if (tool_ == Tool::Select) {
                        int hit = HitItem(m);
                        if (hit >= 0 && !IsSelected(hit)) Select(hit);
                    }
                    ImGui::OpenPopup("##canvas_ctx");
                }
            }
        }
    }

    const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !space_down_;
    const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    hover_item_ = (hovered && drag_ == Drag::None && tool_ == Tool::Select) ? HitItem(m) : -1;

    switch (tool_) {
    case Tool::Select: {
        if (clicked) {
            drag_start_s_ = m;
            drag_start_wx_ = mouse_wx_;
            drag_start_wy_ = mouse_wy_;
            drag_moved_ = false;
            VertexRef v = HitVertex(m);
            VertexRef e;
            Pt at;
            if (v.Valid()) {
                sel_vertex_ = v;
                sel_edge_ = {};
                drag_ = Drag::Vertex;
            } else if (HitEdge(m, &e, &at)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    Polygon& p = map_.items[e.item].polys[e.poly];
                    p.pts.insert(p.pts.begin() + e.index + 1, at);
                    p.edges.insert(p.edges.begin() + e.index + 1, p.edges[e.index]);
                    sel_vertex_ = {e.item, e.poly, e.index + 1};
                    sel_edge_ = {};
                    drag_ = Drag::Vertex;
                    Changed();
                } else {
                    sel_edge_ = e;
                    sel_vertex_ = {};
                    drag_ = Drag::Move;
                }
            } else {
                int hit = HitItem(m);
                if (hit >= 0) {
                    if (shift) Select(hit, true);
                    else if (!IsSelected(hit)) Select(hit);
                    else {
                        sel_vertex_ = {};
                        sel_edge_ = {};
                    }
                    drag_ = IsSelected(hit) ? Drag::Move : Drag::None;
                } else {
                    if (!shift) ClearSelection();
                    drag_ = Drag::Box;
                }
            }
            if (drag_ == Drag::Move) {
                drag_orig_.clear();
                for (int i : sel_) drag_orig_.push_back(map_.items[i]);
                const Item& first = map_.items[sel_[0]];
                drag_anchor_ = KindHasPos(first.kind) || first.polys.empty() ? first.pos : first.polys[0].pts[0];
            }
        }
        if (drag_ == Drag::Move && down) {
            if (!drag_moved_ && Dist(m, drag_start_s_) > 3) drag_moved_ = true;
            if (drag_moved_ && drag_orig_.size() == sel_.size()) {
                Pt d = SnapDelta(int(std::lround(mouse_wx_ - drag_start_wx_)),
                                 int(std::lround(mouse_wy_ - drag_start_wy_)), drag_anchor_);
                for (size_t k = 0; k < sel_.size(); k++) {
                    Item moved = drag_orig_[k];
                    TranslateItem(moved, d.x, d.y);
                    map_.items[sel_[k]] = std::move(moved);
                }
                Changed();
            }
        }
        if (drag_ == Drag::Vertex && down && sel_vertex_.Valid()) {
            if (!drag_moved_ && Dist(m, drag_start_s_) > 2) drag_moved_ = true;
            if (drag_moved_) {
                Pt& v = map_.items[sel_vertex_.item].polys[sel_vertex_.poly].pts[sel_vertex_.index];
                Pt p = Snap(mouse_wx_, mouse_wy_);
                if (!(p == v)) {
                    v = p;
                    Changed();
                }
            }
        }
        if (released && drag_ != Drag::None) {
            if (drag_ == Drag::Box && Dist(m, drag_start_s_) > 3) {
                const double x0 = std::min(drag_start_wx_, mouse_wx_), x1 = std::max(drag_start_wx_, mouse_wx_);
                const double y0 = std::min(drag_start_wy_, mouse_wy_), y1 = std::max(drag_start_wy_, mouse_wy_);
                for (int i = 0; i < int(map_.items.size()); i++) {
                    Box b = ItemBounds(map_.items[i]);
                    if (b.Valid() && b.lo.x >= x0 && b.hi.x <= x1 && b.lo.y >= y0 && b.hi.y <= y1 && !IsSelected(i))
                        Select(i, true);
                }
            }
            drag_ = Drag::None;
            drag_orig_.clear();
        }
        break;
    }
    case Tool::Polygon: {
        if (clicked) {
            Pt p = Snap(mouse_wx_, mouse_wy_);
            if (shift && !draw_pts_.empty()) {     // constrain to 45 degree steps
                const Pt& l = draw_pts_.back();
                const double dx = p.x - l.x, dy = p.y - l.y;
                const double a = std::round(std::atan2(dy, dx) / (M_PI / 4)) * (M_PI / 4);
                const double len = std::sqrt(dx * dx + dy * dy);
                p = {l.x + int(std::lround(std::cos(a) * len)), l.y + int(std::lround(std::sin(a) * len))};
            }
            if (draw_pts_.size() >= 3 &&
                (Dist(ToScreen(draw_pts_[0]), m) < 9 || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)))
                FinishPolygon();
            else if (draw_pts_.empty() || !(p == draw_pts_.back()))
                draw_pts_.push_back(p);
        }
        break;
    }
    case Tool::Rect:
    case Tool::Ellipse: {
        if (clicked) {
            drag_ = Drag::Shape;
            Pt s = Snap(mouse_wx_, mouse_wy_);
            drag_start_wx_ = s.x;
            drag_start_wy_ = s.y;
        }
        if (drag_ == Drag::Shape && released) {
            drag_ = Drag::None;
            Pt a{int(drag_start_wx_), int(drag_start_wy_)};
            Pt b = Snap(mouse_wx_, mouse_wy_);
            if (std::abs(b.x - a.x) < kClick || std::abs(b.y - a.y) < kClick) break;
            const int x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
            const int y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
            std::vector<Pt> pts;
            if (tool_ == Tool::Rect) {
                pts = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
            } else {
                const double cx = (x0 + x1) / 2.0, cy = (y0 + y1) / 2.0, rx = (x1 - x0) / 2.0, ry = (y1 - y0) / 2.0;
                for (int i = 0; i < ellipse_segments_; i++) {
                    const double t = 2 * M_PI * i / ellipse_segments_;
                    pts.push_back({int(std::lround(cx + rx * std::cos(t))), int(std::lround(cy + ry * std::sin(t)))});
                }
            }
            AddShape(std::move(pts));
        }
        break;
    }
    case Tool::Place: {
        if (clicked) {
            drag_ = Drag::Place;
            drag_start_s_ = m;
            drag_anchor_ = Snap(mouse_wx_, mouse_wy_);
        }
        if (drag_ == Drag::Place && released) {
            drag_ = Drag::None;
            int dir = place_dir_;
            if (KindHasDir(place_kind_) && Dist(m, drag_start_s_) > 8) {
                const double a = std::atan2(mouse_wy_ - drag_anchor_.y, mouse_wx_ - drag_anchor_.x);
                dir = ((int(std::lround(a / (2 * M_PI) * kRes)) % kRes) + kRes) % kRes;
                place_dir_ = dir;
            }
            PlaceObject(drag_anchor_, dir);
        }
        break;
    }
    }
    if (!down && drag_ != Drag::None && drag_ != Drag::Pan) {
        drag_ = Drag::None;     // button released outside the canvas
        drag_orig_.clear();
    }
}

// ============================================================================
// drawing
// ============================================================================

void EditorApp::DrawCanvas() {
    canvas_pos_ = ImGui::GetCursorScreenPos();
    canvas_size_ = ImGui::GetContentRegionAvail();
    canvas_size_.x = std::max(canvas_size_.x, 1.0f);
    canvas_size_.y = std::max(canvas_size_.y, 1.0f);
    if (need_fit_) FitView();

    ImGui::InvisibleButton("##canvas_area", canvas_size_,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    HandleCanvasInput(hovered || ImGui::IsItemActive());

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(canvas_pos_.x + canvas_size_.x, canvas_pos_.y + canvas_size_.y);
    dl->PushClipRect(canvas_pos_, end, true);
    dl->AddRectFilled(canvas_pos_, end, IM_COL32(10, 12, 17, 255));
    DrawGrid(dl);
    DrawMap(dl);
    DrawToolPreview(dl);

    // map info overlay
    char info[160];
    std::snprintf(info, sizeof(info), "%s  %dx%d px  %.0f%%", map_.Option("mapName", "(no name)").c_str(),
                  map_.WidthPx(), map_.HeightPx(), zoom_ * 100.0f);
    dl->AddText(ImVec2(canvas_pos_.x + 8, canvas_pos_.y + 6), IM_COL32(150, 165, 185, 200), info);
    dl->PopClipRect();

    if (ImGui::BeginPopup("##canvas_ctx")) {
        if (sel_vertex_.Valid() && ImGui::MenuItem("Delete vertex", "Del")) DeleteSelectedVertex();
        if (sel_edge_.Valid()) {
            int& e = map_.items[sel_edge_.item].polys[sel_edge_.poly].edges[sel_edge_.index];
            if (ImGui::BeginMenu("Edge style")) {
                auto entry = [&](const char* label, int value) {
                    if (ImGui::MenuItem(label, nullptr, e == value)) {
                        e = value;
                        Changed();
                    }
                };
                entry("Polygon default", kEdgeDefault);
                entry("internal (hidden)", kEdgeInternal);
                for (int i = 0; i < int(map_.edge_styles.size()); i++) entry(map_.edge_styles[i].id.c_str(), i);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Insert vertex here")) {
                Polygon& p = map_.items[sel_edge_.item].polys[sel_edge_.poly];
                const int k = sel_edge_.index;
                const Pt a = p.pts[k], b = p.pts[(k + 1) % p.pts.size()];
                p.pts.insert(p.pts.begin() + k + 1, Pt{(a.x + b.x) / 2, (a.y + b.y) / 2});
                p.edges.insert(p.edges.begin() + k + 1, p.edges[k]);
                sel_edge_ = {};
                Changed();
            }
        }
        if (sel_vertex_.Valid() || sel_edge_.Valid()) ImGui::Separator();
        const bool any = !sel_.empty();
        if (ImGui::MenuItem("Delete", "Del", false, any)) DeleteSelection();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, any)) DuplicateSelection();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, any)) CopySelection();
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !clipboard_.empty())) Paste();
        ImGui::Separator();
        if (ImGui::MenuItem("Flip horizontally", "Shift+H", false, any)) TransformSelection(0);
        if (ImGui::MenuItem("Flip vertically", "Shift+V", false, any)) TransformSelection(1);
        if (ImGui::MenuItem("Rotate 90 degrees", "Shift+R", false, any)) TransformSelection(2);
        if (ImGui::MenuItem("Bring to front", "PgUp", false, any)) MoveSelectionInOrder(true);
        if (ImGui::MenuItem("Send to back", "PgDn", false, any)) MoveSelectionInOrder(false);
        ImGui::EndPopup();
    }
}

void EditorApp::DrawGrid(ImDrawList* dl) {
    const double w = map_.WidthPx() * double(kClick), h = map_.HeightPx() * double(kClick);
    const ImVec2 a = ToScreen(0.0, h), b = ToScreen(w, 0.0);   // top-left, bottom-right
    dl->AddRectFilled(a, b, IM_COL32(16, 20, 28, 255));

    if (show_grid_) {
        double step = grid_clicks_;
        while (step / kClick * zoom_ < 8) step *= 2;
        const ImVec2 cmin = canvas_pos_, cmax(canvas_pos_.x + canvas_size_.x, canvas_pos_.y + canvas_size_.y);
        const float x_lo = std::max(a.x, cmin.x), x_hi = std::min(b.x, cmax.x);
        const float y_lo = std::max(a.y, cmin.y), y_hi = std::min(b.y, cmax.y);
        double wx0, wy0, wx1, wy1;
        ToWorld(ImVec2(x_lo, y_hi), &wx0, &wy0);
        ToWorld(ImVec2(x_hi, y_lo), &wx1, &wy1);
        const double block10 = kBlockClicks * 10.0;
        for (double x = std::max(0.0, std::floor(wx0 / step) * step); x <= std::min(w, wx1); x += step) {
            const bool major = std::fmod(x, block10) < 1;
            const float sx = ToScreen(x, 0.0).x;
            dl->AddLine(ImVec2(sx, y_lo), ImVec2(sx, y_hi), major ? IM_COL32(48, 58, 76, 255) : IM_COL32(28, 34, 46, 255));
        }
        for (double y = std::max(0.0, std::floor(wy0 / step) * step); y <= std::min(h, wy1); y += step) {
            const bool major = std::fmod(y, block10) < 1;
            const float sy = ToScreen(0.0, y).y;
            dl->AddLine(ImVec2(x_lo, sy), ImVec2(x_hi, sy), major ? IM_COL32(48, 58, 76, 255) : IM_COL32(28, 34, 46, 255));
        }
    }
    dl->AddRect(a, b, map_.OptionBool("edgeWrap") ? IM_COL32(80, 140, 200, 200) : IM_COL32(200, 90, 70, 220), 0, 0,
                1.5f);
}

const std::vector<int>& EditorApp::Triangles(int item, int poly) {
    if (int(tri_cache_.size()) < int(map_.items.size())) tri_cache_.resize(map_.items.size());
    auto& per_item = tri_cache_[item];
    if (int(per_item.size()) <= poly) per_item.resize(poly + 1);
    TriCache& c = per_item[poly];
    uint64_t h = 1469598103934665603ULL;    // FNV-1a over the points
    for (const Pt& v : map_.items[item].polys[poly].pts) {
        h = (h ^ uint32_t(v.x)) * 1099511628211ULL;
        h = (h ^ uint32_t(v.y)) * 1099511628211ULL;
    }
    if (h != c.hash || c.tris.empty()) {
        c.hash = h;
        c.tris = Triangulate(map_.items[item].polys[poly].pts);
    }
    return c.tris;
}

namespace {
// Fills triangles without anti-aliasing fringes, so adjacent triangles show no seams.
void FillTriangles(ImDrawList* dl, const std::vector<ImVec2>& s, const std::vector<int>& tris, ImU32 col) {
    if (tris.empty()) return;
    dl->PrimReserve(int(tris.size()), int(s.size()));
    const ImDrawIdx base = ImDrawIdx(dl->_VtxCurrentIdx);
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    for (const ImVec2& p : s) dl->PrimWriteVtx(p, uv, col);
    for (int i : tris) dl->PrimWriteIdx(ImDrawIdx(base + i));
}
} // namespace

void EditorApp::DrawPolygon(ImDrawList* dl, const Polygon& p, Kind kind, bool selected, bool hovered,
                            const std::vector<int>* tris) {
    const size_t n = p.pts.size();
    if (n < 2) return;
    static std::vector<ImVec2> s;
    s.resize(n);
    ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
    for (size_t i = 0; i < n; i++) {
        s[i] = ToScreen(p.pts[i]);
        lo.x = std::min(lo.x, s[i].x);
        lo.y = std::min(lo.y, s[i].y);
        hi.x = std::max(hi.x, s[i].x);
        hi.y = std::max(hi.y, s[i].y);
    }
    if (hi.x < canvas_pos_.x || hi.y < canvas_pos_.y || lo.x > canvas_pos_.x + canvas_size_.x ||
        lo.y > canvas_pos_.y + canvas_size_.y)
        return;

    const PolyStyle* ps = p.style >= 0 && p.style < int(map_.poly_styles.size()) ? &map_.poly_styles[p.style] : nullptr;
    const uint32_t color = ps ? ps->color : 0x808080;
    const bool invisible = ps && (ps->flags & kStyleInvisible);
    const bool filled = ps && (ps->flags & (kStyleFilled | kStyleTextured));

    if (tris && filled) FillTriangles(dl, s, *tris, Rgb(color, invisible ? 50 : 200));
    else if (tris && (kind == Kind::BallArea || kind == Kind::BallTarget || kind == Kind::FrictionArea))
        FillTriangles(dl, s, *tris, Rgb(color, 40));

    for (size_t i = 0; i < n; i++) {
        const ImVec2 a = s[i], b = s[(i + 1) % n];
        int e = i < p.edges.size() ? p.edges[i] : kEdgeDefault;
        if (e == kEdgeDefault) e = ps ? ps->defedge : kEdgeDefault;
        if (e == kEdgeInternal) {
            if (selected) DashedLine(dl, a, b, IM_COL32(150, 150, 150, 120), 1.0f, 4.0f);
            continue;
        }
        const EdgeStyle* es = e >= 0 && e < int(map_.edge_styles.size()) ? &map_.edge_styles[e] : nullptr;
        if (es && es->width == -1 && filled) continue;     // hidden edge of a filled polygon (like the client)
        const float th = es ? std::clamp(float(es->width), 1.0f, 4.0f) : 1.0f;
        const ImU32 col = Rgb(es ? es->color : color, invisible ? 110 : 255);
        if (invisible || (es && es->style != 0)) DashedLine(dl, a, b, col, th);
        else dl->AddLine(a, b, col, th);
    }
    if (selected) dl->AddPolyline(s.data(), int(n), kSelColor, ImDrawFlags_Closed, 2.0f);
    else if (hovered) dl->AddPolyline(s.data(), int(n), kHoverColor, ImDrawFlags_Closed, 1.5f);
}

void EditorApp::DrawPointObject(ImDrawList* dl, const Item& it, int index, bool selected, bool hovered) {
    const ImVec2 c = ToScreen(it.pos);
    const float r = ObjectRadius();
    if (c.x < canvas_pos_.x - r * 2 || c.y < canvas_pos_.y - r * 2 || c.x > canvas_pos_.x + canvas_size_.x + r * 2 ||
        c.y > canvas_pos_.y + canvas_size_.y + r * 2)
        return;
    const ImU32 tc = Rgb(TeamColor(it.team));
    const ImU32 white = IM_COL32(235, 240, 245, 255);
    char label[32] = "";
    const double a = 2 * M_PI * it.dir / kRes;
    const float dx = float(std::cos(a)), dy = float(-std::sin(a));   // screen y is down
    auto at = [&](float fwd, float side) { return ImVec2(c.x + fwd * dx - side * dy, c.y + fwd * dy + side * dx); };

    switch (it.kind) {
    case Kind::Base: {
        ImVec2 tip = at(r, 0), l = at(-r * 0.7f, r * 0.75f), rr = at(-r * 0.7f, -r * 0.75f), back = at(-r * 0.35f, 0);
        ImVec2 pts[4] = {tip, l, back, rr};
        dl->AddConcavePolyFilled(pts, 4, Rgb(TeamColor(it.team), 110));
        dl->AddPolyline(pts, 4, tc, ImDrawFlags_Closed, 1.5f);
        if (it.team >= 0) std::snprintf(label, sizeof(label), "%d", it.team);
        break;
    }
    case Kind::Fuel: {
        dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.55f), ImVec2(c.x + r * 0.8f, c.y + r * 0.55f),
                          IM_COL32(200, 140, 20, 200), 2);
        dl->AddRect(ImVec2(c.x - r * 0.8f, c.y - r * 0.55f), ImVec2(c.x + r * 0.8f, c.y + r * 0.55f),
                    it.team >= 0 ? tc : IM_COL32(255, 200, 80, 255), 2, 0, 1.5f);
        CenteredText(dl, c, IM_COL32(20, 20, 20, 255), "F");
        if (it.team >= 0) std::snprintf(label, sizeof(label), "%d", it.team);
        break;
    }
    case Kind::Check: {
        int num = 0;
        for (int i = 0; i <= index && i < int(map_.items.size()); i++) num += map_.items[i].kind == Kind::Check;
        dl->AddCircle(c, r * 0.9f, IM_COL32(90, 230, 120, 255), 24, 2.0f);
        char t[8];
        std::snprintf(t, sizeof(t), "%d", num);
        CenteredText(dl, c, IM_COL32(90, 230, 120, 255), t);
        break;
    }
    case Kind::Cannon: {
        dl->AddCircleFilled(c, r * 0.35f, it.team >= 0 ? tc : white);
        dl->AddLine(c, at(r * 0.9f, 0), it.team >= 0 ? tc : white, 2.5f);
        if (it.team >= 0) std::snprintf(label, sizeof(label), "%d", it.team);
        break;
    }
    case Kind::Target: {
        dl->AddRect(ImVec2(c.x - r * 0.35f, c.y - r * 0.35f), ImVec2(c.x + r * 0.35f, c.y + r * 0.35f),
                    it.team >= 0 ? tc : IM_COL32(255, 140, 40, 255), 0, 0, 2.0f);
        if (it.team >= 0) std::snprintf(label, sizeof(label), "%d", it.team);
        break;
    }
    case Kind::Ball:
        dl->AddCircleFilled(c, r * 0.6f, Rgb(TeamColor(it.team), 200));
        dl->AddCircle(c, r * 0.6f, white, 20, 1.5f);
        if (it.team >= 0) std::snprintf(label, sizeof(label), "%d", it.team);
        break;
    case Kind::Grav: {
        const ImU32 gc = it.value < 0 ? IM_COL32(190, 120, 255, 255) : IM_COL32(120, 180, 255, 255);
        dl->AddCircle(c, r * 0.9f, gc, 24, 1.5f);
        static const char* sym[] = {"+", "-", "cw", "ccw", "^", "v", ">", "<"};
        CenteredText(dl, c, gc, sym[std::clamp(it.type, 0, 7)]);
        break;
    }
    case Kind::Wormhole: {
        const ImU32 wc = IM_COL32(60, 230, 240, 255);
        dl->AddCircle(c, r * 0.8f, wc, 24, 1.5f);
        dl->AddCircle(c, r * 0.45f, wc, 18, 1.0f);
        static const char* sym[] = {"", "in", "out", "fix"};
        std::snprintf(label, sizeof(label), "%s", sym[std::clamp(it.type, 0, 3)]);
        break;
    }
    case Kind::ItemConc:
    case Kind::AsteroidConc: {
        const ImU32 col = it.kind == Kind::ItemConc ? IM_COL32(240, 100, 220, 255) : IM_COL32(200, 160, 110, 255);
        ImVec2 d[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
        dl->AddPolyline(d, 4, col, ImDrawFlags_Closed, 1.5f);
        CenteredText(dl, c, col, it.kind == Kind::ItemConc ? "I" : "A");
        break;
    }
    default:
        break;
    }
    if (show_labels_ && label[0]) dl->AddText(ImVec2(c.x + r * 0.8f, c.y - r * 1.3f), white, label);
    if (selected) dl->AddCircle(c, r + 3, kSelColor, 24, 2.0f);
    else if (hovered) dl->AddCircle(c, r + 3, kHoverColor, 24, 1.5f);
}

void EditorApp::DrawMap(ImDrawList* dl) {
    const int n = int(map_.items.size());
    // polygons first, point objects on top so they stay visible inside walls and areas
    for (int i = 0; i < n; i++) {
        const Item& it = map_.items[i];
        for (int pi = 0; pi < int(it.polys.size()); pi++)
            DrawPolygon(dl, it.polys[pi], it.kind, IsSelected(i), hover_item_ == i,
                        it.polys[pi].pts.size() >= 3 ? &Triangles(i, pi) : nullptr);
    }
    // edge wrap: polygons reaching over the border are shown as a faint, non-selectable outline on the opposite side
    if (map_.OptionBool("edgeWrap")) {
        const double mw = double(map_.WidthPx()) * kClick, mh = double(map_.HeightPx()) * kClick;
        static std::vector<ImVec2> g;
        for (int i = 0; i < n; i++) {
            for (const Polygon& p : map_.items[i].polys) {
                if (p.pts.size() < 2) continue;
                double lx = 1e18, hx = -1e18, ly = 1e18, hy = -1e18;
                for (const Pt& q : p.pts) {
                    lx = std::min(lx, double(q.x));
                    hx = std::max(hx, double(q.x));
                    ly = std::min(ly, double(q.y));
                    hy = std::max(hy, double(q.y));
                }
                if (lx >= 0 && hx <= mw && ly >= 0 && hy <= mh) continue;   // fully inside, no wrapped copy
                for (int sy = -2; sy <= 2; sy++)
                    for (int sx = -2; sx <= 2; sx++) {
                        if (!sx && !sy) continue;
                        const double ox = sx * mw, oy = sy * mh;
                        if (hx + ox < 0 || lx + ox > mw || hy + oy < 0 || ly + oy > mh) continue;
                        g.resize(p.pts.size());
                        for (size_t k = 0; k < p.pts.size(); k++)
                            g[k] = ToScreen(p.pts[k].x + ox, p.pts[k].y + oy);
                        dl->AddPolyline(g.data(), int(g.size()), IM_COL32(150, 170, 200, 110), ImDrawFlags_Closed, 1.0f);
                    }
            }
        }
    }
    // smoothing preview
    if (show_smooth_) {
        static std::vector<ImVec2> g;
        for (const auto& r : smooth_preview_) {
            g.resize(r.p.pts.size());
            for (size_t k = 0; k < r.p.pts.size(); k++) g[k] = ToScreen(r.p.pts[k]);
            dl->AddPolyline(g.data(), int(g.size()), IM_COL32(255, 210, 60, 230), ImDrawFlags_Closed, 1.5f);
        }
    }
    for (int i = 0; i < n; i++)
        if (KindHasPos(map_.items[i].kind))
            DrawPointObject(dl, map_.items[i], i, IsSelected(i), hover_item_ == i);

    // vertex handles of a single selected polygon object
    if (sel_.size() == 1 && tool_ == Tool::Select) {
        const Item& it = map_.items[sel_[0]];
        for (int pi = 0; pi < int(it.polys.size()); pi++) {
            const auto& p = it.polys[pi];
            if (sel_edge_.Valid() && sel_edge_.poly == pi) {
                const ImVec2 a = ToScreen(p.pts[sel_edge_.index]), b = ToScreen(p.pts[(sel_edge_.index + 1) % p.pts.size()]);
                dl->AddLine(a, b, IM_COL32(80, 255, 140, 255), 4.0f);
            }
            for (int k = 0; k < int(p.pts.size()); k++) {
                const ImVec2 v = ToScreen(p.pts[k]);
                const bool sv = sel_vertex_.Valid() && sel_vertex_.poly == pi && sel_vertex_.index == k;
                dl->AddRectFilled(ImVec2(v.x - 4, v.y - 4), ImVec2(v.x + 4, v.y + 4),
                                  sv ? IM_COL32(80, 255, 140, 255) : IM_COL32(20, 24, 30, 255));
                dl->AddRect(ImVec2(v.x - 4, v.y - 4), ImVec2(v.x + 4, v.y + 4), kSelColor);
            }
        }
    }
}

void EditorApp::DrawToolPreview(ImDrawList* dl) {
    const ImVec2 m = ImGui::GetIO().MousePos;
    const Pt snapped = Snap(mouse_wx_, mouse_wy_);
    const ImVec2 sp = ToScreen(snapped);
    const ImU32 pc = IM_COL32(120, 220, 255, 255);

    if (drag_ == Drag::Box) {
        const ImVec2 a = ToScreen(drag_start_wx_, drag_start_wy_);
        dl->AddRectFilled(ImVec2(std::min(a.x, m.x), std::min(a.y, m.y)), ImVec2(std::max(a.x, m.x), std::max(a.y, m.y)),
                          IM_COL32(90, 160, 255, 40));
        dl->AddRect(ImVec2(std::min(a.x, m.x), std::min(a.y, m.y)), ImVec2(std::max(a.x, m.x), std::max(a.y, m.y)),
                    IM_COL32(90, 160, 255, 200));
    }
    if (!mouse_in_canvas_ && drag_ == Drag::None) return;

    if (tool_ != Tool::Select) {
        dl->AddLine(ImVec2(sp.x - 6, sp.y), ImVec2(sp.x + 6, sp.y), pc);
        dl->AddLine(ImVec2(sp.x, sp.y - 6), ImVec2(sp.x, sp.y + 6), pc);
    }
    switch (tool_) {
    case Tool::Polygon:
        if (!draw_pts_.empty()) {
            for (size_t i = 0; i + 1 < draw_pts_.size(); i++)
                dl->AddLine(ToScreen(draw_pts_[i]), ToScreen(draw_pts_[i + 1]), pc, 2.0f);
            dl->AddLine(ToScreen(draw_pts_.back()), sp, IM_COL32(120, 220, 255, 150), 1.5f);
            if (draw_pts_.size() >= 2) DashedLine(dl, sp, ToScreen(draw_pts_[0]), IM_COL32(120, 220, 255, 90), 1.0f);
            for (const auto& p : draw_pts_) dl->AddCircleFilled(ToScreen(p), 3.5f, pc);
            if (draw_pts_.size() >= 3 && Dist(ToScreen(draw_pts_[0]), m) < 9)
                dl->AddCircle(ToScreen(draw_pts_[0]), 8, IM_COL32(80, 255, 140, 255), 16, 2.0f);
        }
        break;
    case Tool::Rect:
    case Tool::Ellipse:
        if (drag_ == Drag::Shape) {
            const ImVec2 a = ToScreen(drag_start_wx_, drag_start_wy_);
            const ImVec2 lo(std::min(a.x, sp.x), std::min(a.y, sp.y)), hi(std::max(a.x, sp.x), std::max(a.y, sp.y));
            if (tool_ == Tool::Rect) dl->AddRect(lo, hi, pc, 0, 0, 2.0f);
            else dl->AddEllipse(ImVec2((lo.x + hi.x) / 2, (lo.y + hi.y) / 2), ImVec2((hi.x - lo.x) / 2, (hi.y - lo.y) / 2),
                                pc, 0, ellipse_segments_, 2.0f);
        }
        break;
    case Tool::Place: {
        Item ghost;
        ghost.kind = place_kind_;
        ghost.pos = drag_ == Drag::Place ? drag_anchor_ : snapped;
        ghost.team = place_teams_[int(place_kind_)];
        ghost.dir = place_dir_;
        ghost.type = place_kind_ == Kind::Grav ? place_grav_type_ : place_worm_type_;
        ghost.value = place_grav_force_;
        if (drag_ == Drag::Place && KindHasDir(place_kind_) && Dist(m, drag_start_s_) > 8) {
            const double a = std::atan2(mouse_wy_ - drag_anchor_.y, mouse_wx_ - drag_anchor_.x);
            ghost.dir = ((int(std::lround(a / (2 * M_PI) * kRes)) % kRes) + kRes) % kRes;
            dl->AddLine(ToScreen(ghost.pos), m, IM_COL32(120, 220, 255, 150), 1.5f);
        }
        DrawPointObject(dl, ghost, 0, false, true);
        break;
    }
    default:
        break;
    }
}
