#include "ship_widgets.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ss {

void DrawThumb(ImDrawList* dl, const ShipShape& s, ImVec2 p0, float size, ImU32 col) {
    const auto& h = s.hull();
    if (h.size() < 2) return;
    const float sc = size / 32.0f;
    const ImVec2 c{p0.x + size * 0.5f, p0.y + size * 0.5f};
    std::vector<ImVec2> pts;
    pts.reserve(h.size());
    for (IPt p : h) pts.push_back({c.x + p.x * sc, c.y - p.y * sc});
    dl->AddPolyline(pts.data(), (int)pts.size(), col, ImDrawFlags_Closed, 1.2f);
}

bool ContainsNoCase(const std::string& hay, const char* needle) {
    if (!*needle) return true;
    auto it = std::search(hay.begin(), hay.end(), needle, needle + std::strlen(needle),
                          [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != hay.end();
}

bool ShipRow(const Entry& e, bool selected, ImFont* small_font) {
    const float row_h = kShipRowH;
    ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, row_h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({p.x + 2, p.y + 2}, {p.x + row_h - 2, p.y + row_h - 2}, IM_COL32(5, 7, 12, 255), 3);
    DrawThumb(dl, e.shape, {p.x + 5, p.y + 5}, row_h - 10,
              e.valid ? IM_COL32(220, 228, 240, 255) : IM_COL32(255, 120, 80, 255));
    dl->AddText({p.x + row_h + 6, p.y + 4}, ImGui::GetColorU32(ImGuiCol_Text),
                e.shape.name.empty() ? "(unnamed)" : e.shape.name.c_str());
    std::string sub = e.valid ? e.shape.author : "INVALID  " + e.shape.author;
    ImFont* f = small_font ? small_font : ImGui::GetFont();
    ImGui::PushFont(f);
    dl->AddText({p.x + row_h + 6, p.y + 24},
                e.valid ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : IM_COL32(255, 130, 90, 255), sub.c_str());
    ImGui::PopFont();
    return clicked;
}

bool ShipPicker::Draw(const char* id, const ShipShape* current, bool current_valid, const std::vector<Entry>& stock,
                      const std::vector<Entry>& mine, ShipShape* chosen, float width, ImFont* small_font) {
    bool picked = false;
    ImGui::PushID(id);

    // the button: thumbnail + name
    const float bh = kShipRowH;
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##pick", ImVec2(width, bh))) {
        ImGui::OpenPopup("picker");
        filter_[0] = 0;
        focus_filter_ = true;
    }
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({p.x + 3, p.y + 3}, {p.x + bh - 3, p.y + bh - 3}, IM_COL32(5, 7, 12, 255), 3);
        if (current) {
            DrawThumb(dl, *current, {p.x + 6, p.y + 6}, bh - 12,
                      current_valid ? IM_COL32(220, 228, 240, 255) : IM_COL32(255, 120, 80, 255));
            dl->AddText({p.x + bh + 6, p.y + 5}, ImGui::GetColorU32(ImGuiCol_Text),
                        current->name.empty() ? "(unnamed)" : current->name.c_str());
            ImGui::PushFont(small_font ? small_font : ImGui::GetFont());
            std::string sub = current_valid ? current->author : "INVALID: the client flies the default ship";
            dl->AddText({p.x + bh + 6, p.y + 25},
                        current_valid ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : IM_COL32(255, 130, 90, 255),
                        sub.c_str());
            ImGui::PopFont();
        } else {
            dl->AddText({p.x + bh + 6, p.y + (bh - ImGui::GetTextLineHeight()) * 0.5f},
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), "(client default ship)");
        }
        dl->AddText({p.x + width - 22, p.y + (bh - ImGui::GetTextLineHeight()) * 0.5f},
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), "v");
    }

    {   // right below the button (always: it follows the button), as tall as the window allows
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float y = p.y + bh + 2;
        const float h = std::clamp(vp->Pos.y + vp->Size.y - y - 8, 200.0f, 440.0f);
        ImGui::SetNextWindowPos(ImVec2(p.x, y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(std::max(width, 320.0f), h), ImGuiCond_Always);
    }
    if (ImGui::BeginPopup("picker", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        if (focus_filter_) { ImGui::SetKeyboardFocusHere(); focus_filter_ = false; }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search name / author", filter_, sizeof filter_);
        if (ImGui::BeginTabBar("tabs")) {
            auto list = [&](const char* label, const std::vector<Entry>& entries, const char* empty_hint) {
                if (!ImGui::BeginTabItem(label)) return;
                ImGui::TextDisabled("%d ships", (int)entries.size());
                if (entries.empty()) ImGui::TextWrapped("%s", empty_hint);
                ImGui::BeginChild("rows", ImVec2(0, 0));
                std::vector<int> shown;
                for (int i = 0; i < (int)entries.size(); ++i)
                    if (ContainsNoCase(entries[i].shape.name, filter_) || ContainsNoCase(entries[i].shape.author, filter_))
                        shown.push_back(i);
                ImGuiListClipper clip;
                clip.Begin((int)shown.size(), kShipRowH + ImGui::GetStyle().ItemSpacing.y);
                while (clip.Step())
                    for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                        const Entry& e = entries[shown[r]];
                        ImGui::PushID(shown[r]);
                        if (ShipRow(e, current && e.shape == *current, small_font)) {
                            *chosen = e.shape;
                            picked = true;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopID();
                    }
                ImGui::EndChild();
                ImGui::EndTabItem();
            };
            list("Stock", stock, "Stock ships (lib/shipshapes.cfg) not found.");
            list("My collection", mine, "Your collection is empty. Create ships with \"Edit shipshape\".");
            ImGui::EndTabBar();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return picked;
}

} // namespace ss
