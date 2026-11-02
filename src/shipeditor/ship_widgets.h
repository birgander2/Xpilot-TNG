#pragma once
// Dear ImGui widgets for ship shapes, shared by the ship editor and the launcher's
// profile editor: thumbnails, list rows and a ship picker.

#include <imgui.h>

#include <string>
#include <vector>

#include "ship_files.h"
#include "ship_shape.h"

namespace ss {

constexpr float kShipRowH = 46;

// Hull outline of s inside the square [p0, p0 + size]
void DrawThumb(ImDrawList* dl, const ShipShape& s, ImVec2 p0, float size, ImU32 col);

// Case-insensitive substring test ("" matches everything)
bool ContainsNoCase(const std::string& hay, const char* needle);

// One list row: thumbnail, name, author (invalid ships in orange). The row is a
// Selectable, so BeginPopupContextItem() and IsItemHovered() work right after it.
// Returns true when it was clicked. small_font may be nullptr.
bool ShipRow(const Entry& e, bool selected, ImFont* small_font);

// Button showing the current ship; opens a popup with a search field and the tabs
// "Stock" and "My collection", rows as in the ship editor. The popup sits right below
// the button and cannot be moved or resized.
class ShipPicker {
public:
    // current may be nullptr (no ship). Returns true when a ship was picked
    // (written to *chosen).
    bool Draw(const char* id, const ShipShape* current, bool current_valid, const std::vector<Entry>& stock,
              const std::vector<Entry>& mine, ShipShape* chosen, float width, ImFont* small_font = nullptr);

private:
    char filter_[64] = "";
    bool focus_filter_ = false;
};

} // namespace ss
