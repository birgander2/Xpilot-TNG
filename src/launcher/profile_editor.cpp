#include "profile_editor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include <imgui.h>

#include "xpilotrc.h"

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.50f, 1.0f);
const ImVec4 kBad(0.95f, 0.40f, 0.40f, 1.0f);

// Key actions of the client (src/client/event.c) with their built-in defaults.
struct KeyAction {
    const char* option;
    const char* label;
    const char* def;
};
struct KeyGroup {
    const char* title;
    std::vector<KeyAction> keys;
};

const std::vector<KeyGroup>& KeyGroups() {
    static const std::vector<KeyGroup> groups = {
        {"Flight", {
            {"keyTurnLeft", "Turn left", "a"},
            {"keyTurnRight", "Turn right", "s"},
            {"keyThrust", "Thrust", "Shift_R Shift_L"},
            {"keyShield", "Shield", "space"},
            {"keyRefuel", "Refuel", "f Control_L Control_R"},
            {"keyRepair", "Repair", "f"},
            {"keyCloak", "Cloak", "Delete BackSpace"},
            {"keyEmergencyThrust", "Emergency thrust", "j"},
            {"keyEmergencyShield", "Emergency shield", "Caps_Lock"},
            {"keyPhasing", "Phasing", "p"},
            {"keyDeflector", "Deflector", "o"},
            {"keyHyperJump", "Hyperjump", "q"},
            {"keySelfDestruct", "Self destruct", "End"},
            {"keyChangeHome", "Change home base", "Home"},
            {"keyIncreasePower", "Increase power", "KP_Multiply"},
            {"keyDecreasePower", "Decrease power", "KP_Divide"},
            {"keyIncreaseTurnspeed", "Increase turnspeed", "KP_Add"},
            {"keyDecreaseTurnspeed", "Decrease turnspeed", "KP_Subtract"},
            {"keyToggleAutoPilot", "Autopilot", "9"},
        }},
        {"Weapons", {
            {"keyFireShot", "Fire shot", "Return Linefeed"},
            {"keyFireMissile", "Fire missile", "backslash"},
            {"keyFireTorpedo", "Fire torpedo", "quoteright"},
            {"keyFireHeat", "Fire heat seeker", "semicolon"},
            {"keyFireLaser", "Fire laser", "slash"},
            {"keyDropMine", "Drop mine", "Tab"},
            {"keyDetachMine", "Detach mine", "bracketright"},
            {"keyDetonateMines", "Detonate mines", "equal"},
            {"keyEcm", "ECM", "bracketleft"},
            {"keyTransporter", "Transporter", "t"},
            {"keyTractorBeam", "Tractor beam", "comma"},
            {"keyPressorBeam", "Pressor beam", "period"},
            {"keyConnector", "Connector", "f Control_L Control_R"},
            {"keyDropBall", "Drop ball", "d"},
        }},
        {"Modifiers", {
            {"keyToggleNuclear", "Nuclear", "n"},
            {"keyToggleCluster", "Cluster", "c"},
            {"keyToggleImplosion", "Implosion", "i"},
            {"keyToggleVelocity", "Velocity", "v"},
            {"keyToggleMini", "Mini", "x"},
            {"keyToggleSpread", "Spread", "z"},
            {"keyTogglePower", "Power", "b"},
            {"keyToggleLaser", "Laser (stun/blind)", "l"},
            {"keyClearModifiers", "Clear modifiers", "k"},
            {"keyLoadModifiers1", "Load modifier bank 1", "1"},
            {"keyLoadModifiers2", "Load modifier bank 2", "2"},
            {"keyLoadModifiers3", "Load modifier bank 3", "3"},
            {"keyLoadModifiers4", "Load modifier bank 4", "4"},
            {"keyReprogram", "Reprogram bank (hold)", "quoteleft"},
        }},
        {"Locks & items", {
            {"keyLockClose", "Lock closest", "Up"},
            {"keyLockNextClose", "Lock next closest", "Down"},
            {"keyLockNext", "Lock next", "Next Right"},
            {"keyLockPrev", "Lock previous", "Prior Right"},
            {"keyLoadLock1", "Load lock bank 1", "5"},
            {"keyLoadLock2", "Load lock bank 2", "6"},
            {"keyLoadLock3", "Load lock bank 3", "7"},
            {"keyLoadLock4", "Load lock bank 4", "8"},
            {"keyTankNext", "Next tank", "e"},
            {"keyTankPrev", "Previous tank", "w"},
            {"keyTankDetach", "Detach tank", "r"},
            {"keySelectItem", "Select item", "KP_0 KP_Insert"},
            {"keyLoseItem", "Lose item", "KP_Delete KP_Decimal"},
        }},
        {"Interface", {
            {"keyTalk", "Talk", "m"},
            {"keyIdMode", "ID mode", "u"},
            {"keyPause", "Pause", "Pause"},
            {"keyToggleCompass", "Compass", "KP_7"},
            {"keyToggleOwnedItems", "Owned items", "KP_8"},
            {"keyToggleMessages", "Messages", "KP_9"},
            {"keyToggleRadarScore", "Radar / score", "F12"},
            {"keyToggleFullScreen", "Fullscreen", "F11"},
            {"keyToggleSound", "Sound", ""},
            {"keyToggleRecord", "Record session", "KP_5"},
            {"keySwapSettings", "Swap settings", ""},
            {"keySwapScaleFactor", "Swap scale factor", ""},
            {"keyPointerControl", "Pointer control", "KP_Enter"},
            {"keyPrintMessagesStdout", "Print messages to stdout", "Print"},
            {"keyYes", "Yes (prompts)", "y"},
            {"keyNo", "No (prompts)", "n"},
            {"keyExit", "Exit", "Escape"},
        }},
    };
    return groups;
}

const KeyAction* FindAction(const std::string& option) {
    for (const auto& g : KeyGroups())
        for (const auto& k : g.keys)
            if (strcasecmp(k.option, option.c_str()) == 0) return &k;
    return nullptr;
}

const char* const kButtonDefaults[] = {"keyFireShot", "keyThrust", "keyThrust", "", ""};

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isspace((unsigned char)s[i])) ++i;
        size_t b = i;
        while (i < s.size() && !isspace((unsigned char)s[i])) ++i;
        if (i > b) out.push_back(s.substr(b, i - b));
    }
    return out;
}

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& t : v) out += (out.empty() ? "" : " ") + t;
    return out;
}

// Key name as understood by the client (String_to_xp_keysym), empty for keys that cannot be bound.
std::string KeyName(ImGuiKey k) {
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return std::string(1, char('a' + (k - ImGuiKey_A)));
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return std::string(1, char('0' + (k - ImGuiKey_0)));
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) return "F" + std::to_string(k - ImGuiKey_F1 + 1);
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return "KP_" + std::to_string(k - ImGuiKey_Keypad0);
    switch (k) {
        case ImGuiKey_Tab: return "Tab";
        case ImGuiKey_LeftArrow: return "Left";
        case ImGuiKey_RightArrow: return "Right";
        case ImGuiKey_UpArrow: return "Up";
        case ImGuiKey_DownArrow: return "Down";
        case ImGuiKey_PageUp: return "Prior";
        case ImGuiKey_PageDown: return "Next";
        case ImGuiKey_Home: return "Home";
        case ImGuiKey_End: return "End";
        case ImGuiKey_Insert: return "Insert";
        case ImGuiKey_Delete: return "Delete";
        case ImGuiKey_Backspace: return "BackSpace";
        case ImGuiKey_Space: return "space";
        case ImGuiKey_Enter: return "Return";
        case ImGuiKey_Pause: return "Pause";
        case ImGuiKey_PrintScreen: return "Print";
        case ImGuiKey_ScrollLock: return "Scroll_Lock";
        case ImGuiKey_NumLock: return "Num_Lock";
        case ImGuiKey_CapsLock: return "Caps_Lock";
        case ImGuiKey_Apostrophe: return "quoteright";
        case ImGuiKey_Comma: return "comma";
        case ImGuiKey_Minus: return "minus";
        case ImGuiKey_Period: return "period";
        case ImGuiKey_Slash: return "slash";
        case ImGuiKey_Semicolon: return "semicolon";
        case ImGuiKey_Equal: return "equal";
        case ImGuiKey_LeftBracket: return "bracketleft";
        case ImGuiKey_Backslash: return "backslash";
        case ImGuiKey_RightBracket: return "bracketright";
        case ImGuiKey_GraveAccent: return "quoteleft";
        case ImGuiKey_KeypadEnter: return "KP_Enter";
        case ImGuiKey_KeypadMultiply: return "KP_Multiply";
        case ImGuiKey_KeypadAdd: return "KP_Add";
        case ImGuiKey_KeypadSubtract: return "KP_Subtract";
        case ImGuiKey_KeypadDecimal: return "KP_Decimal";
        case ImGuiKey_KeypadDivide: return "KP_Divide";
        case ImGuiKey_LeftShift: return "Shift_L";
        case ImGuiKey_RightShift: return "Shift_R";
        case ImGuiKey_LeftCtrl: return "Control_L";
        case ImGuiKey_RightCtrl: return "Control_R";
        case ImGuiKey_LeftAlt: return "Alt_L";
        case ImGuiKey_RightAlt: return "Alt_R";
        default: return "";
    }
}

// Key pressed this frame (Esc = cancel), empty if none.
std::string PressedKeyName(bool* cancel) {
    *cancel = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { *cancel = true; return ""; }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        if (!ImGui::IsKeyPressed(ImGuiKey(k), false)) continue;
        std::string n = KeyName(ImGuiKey(k));
        if (!n.empty()) return n;
    }
    return "";
}

// Name of a full "(NM:...)" ship definition, else the text itself.
std::string ShipName(const std::string& shape) {
    size_t p = shape.find("(NM:");
    if (p == std::string::npos) p = shape.find("(name:");
    if (p == std::string::npos) return shape;
    size_t b = shape.find(':', p) + 1, e = shape.find(')', b);
    if (e == std::string::npos) return shape;
    b = shape.find_first_not_of(' ', b);
    return b == std::string::npos || b >= e ? "" : shape.substr(b, e - b);
}

int Digits(const std::string& s, size_t* i, int lo, int hi) {
    int n = 0;
    while (*i < s.size() && isdigit((unsigned char)s[*i])) n = std::min(n * 10 + (s[(*i)++] - '0'), 1000);
    return std::clamp(n, lo, hi);
}

} // namespace

// ---------------------------------------------------------------------------
// modifier banks (same syntax as Player_set_modbank in src/server/modifiers.c)
// ---------------------------------------------------------------------------

ProfileEditor::ModBank ProfileEditor::ModBank::Parse(const std::string& s) {
    ModBank m;
    for (size_t i = 0; i < s.size();) {
        char c = char(tolower((unsigned char)s[i++]));
        switch (c) {
            case 'f': if (i < s.size() && tolower((unsigned char)s[i]) == 'n') m.nuclear = 2; break;
            case 'n': if (m.nuclear == 0) m.nuclear = 1; break;
            case 'c': m.cluster = true; break;
            case 'i': m.implosion = true; break;
            case 'v': m.velocity = Digits(s, &i, 0, 3); break;
            case 'x': m.mini = Digits(s, &i, 1, 4); break;
            case 'z': m.spread = Digits(s, &i, 0, 3); break;
            case 'b': m.power = Digits(s, &i, 0, 3); break;
            case 'l':
                if (i < s.size() && tolower((unsigned char)s[i]) == 's') m.laser = 1;
                if (i < s.size() && tolower((unsigned char)s[i]) == 'b') m.laser = 2;
                break;
            default: break;
        }
    }
    return m;
}

std::string ProfileEditor::ModBank::Serialize() const {
    std::string s;
    if (nuclear == 2) s += "fn";
    else if (nuclear == 1) s += "n";
    if (cluster) s += "c";
    if (implosion) s += "i";
    if (laser == 1) s += "ls";
    if (laser == 2) s += "lb";
    if (velocity) s += "v" + std::to_string(velocity);
    if (mini >= 2) s += "x" + std::to_string(mini);
    if (spread) s += "z" + std::to_string(spread);
    if (power) s += "b" + std::to_string(power);
    return s;
}

// ---------------------------------------------------------------------------
// load / save
// ---------------------------------------------------------------------------

void ProfileEditor::Load(const std::vector<std::string>& ship_names) {
    rc_.clear();
    ship_names_ = ship_names;
    auto get = [&](const std::string& key, const std::string& def) {
        std::string v;
        if (XpilotrcLookup(key.c_str(), &v)) { rc_[key] = v; return v; }
        return def;
    };

    std::snprintf(name_, sizeof name_, "%s", get("name", "").c_str());
    rc_ship_ = ShipName(get("shipShape", ""));
    auto it = std::find(ship_names_.begin(), ship_names_.end(), rc_ship_);
    ship_ = it != ship_names_.end() ? int(it - ship_names_.begin()) : -1;

    key_values_.clear();
    for (const auto& g : KeyGroups())
        for (const auto& k : g.keys) key_values_.push_back(get(k.option, k.def));

    for (int i = 0; i < kButtons; ++i) {
        button_actions_[i].clear();
        for (auto& t : Split(get("pointerButton" + std::to_string(i + 1), kButtonDefaults[i]))) {
            if (strncasecmp(t.c_str(), "key", 3) != 0) t = "key" + t;
            if (const KeyAction* a = FindAction(t)) t = a->option;   // canonical spelling
            button_actions_[i].push_back(t);
        }
    }
    for (int i = 0; i < kBanks; ++i)
        banks_[i] = ModBank::Parse(get("modifierBank" + std::to_string(i + 1), ""));

    shown_.clear();
    // snapshot of everything the editor can write, to detect edits
    shown_["name"] = name_;
    shown_["shipShape"] = ship_ >= 0 ? ship_names_[ship_] : rc_ship_;
    int n = 0;
    for (const auto& g : KeyGroups())
        for (const auto& k : g.keys) shown_[k.option] = key_values_[n++];
    for (int i = 0; i < kButtons; ++i) shown_["pointerButton" + std::to_string(i + 1)] = Join(button_actions_[i]);
    for (int i = 0; i < kBanks; ++i) shown_["modifierBank" + std::to_string(i + 1)] = banks_[i].Serialize();
}

std::vector<std::pair<std::string, std::string>> ProfileEditor::Changes() const {
    std::vector<std::pair<std::string, std::string>> cur, changed;
    cur.emplace_back("name", name_);
    cur.emplace_back("shipShape", ship_ >= 0 ? ship_names_[ship_] : rc_ship_);
    int n = 0;
    for (const auto& g : KeyGroups())
        for (const auto& k : g.keys) cur.emplace_back(k.option, key_values_[n++]);
    for (int i = 0; i < kButtons; ++i) cur.emplace_back("pointerButton" + std::to_string(i + 1), Join(button_actions_[i]));
    for (int i = 0; i < kBanks; ++i) cur.emplace_back("modifierBank" + std::to_string(i + 1), banks_[i].Serialize());
    for (auto& kv : cur) {
        auto it = shown_.find(kv.first);
        if (it == shown_.end() || it->second != kv.second) changed.push_back(kv);
    }
    return changed;
}

bool ProfileEditor::Dirty() const { return !Changes().empty(); }

bool ProfileEditor::Save() {
    std::string err;
    if (!XpilotrcWrite(Changes(), &err)) {
        status_ = "Save failed: " + err;
        status_error_ = true;
        return false;
    }
    Load(ship_names_);
    status_ = "Saved to " + XpilotrcPath();
    status_error_ = false;
    return true;
}

bool ProfileEditor::Capturing() const {
    return capture_ >= 0 || cancelled_frame_ == ImGui::GetFrameCount();
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void ProfileEditor::RenderIdentity() {
    ImGui::Spacing();
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("Name", "(account name)", name_, sizeof name_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Nickname (xpilot.name)");

    ImGui::SetNextItemWidth(260);
    const char* none = rc_ship_.empty() ? "(client default)" : rc_ship_.c_str();
    const char* label = ship_ >= 0 ? ship_names_[ship_].c_str() : none;
    if (ImGui::BeginCombo("Ship shape", label)) {
        if (ImGui::Selectable(none, ship_ < 0)) ship_ = -1;
        for (int i = 0; i < int(ship_names_.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(ship_names_[i].c_str(), ship_ == i)) ship_ = i;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ship from lib/shipshapes.txt (xpilot.shipShape)");
}

void ProfileEditor::RenderKeys() {
    ImGui::Spacing();
    ImGui::TextDisabled("Set = press the new key, Add = bind an additional key. The text can also be edited by hand "
                        "(X11 style key names, separated by spaces). Empty = unbound.");
    ImGui::BeginChild("KeysScroll", ImVec2(0, 0));
    int n = 0;
    for (const auto& g : KeyGroups()) {
        bool open = ImGui::CollapsingHeader(g.title, ImGuiTreeNodeFlags_DefaultOpen);
        if (!open) { n += int(g.keys.size()); continue; }
        if (ImGui::BeginTable(g.title, 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("d", ImGuiTableColumnFlags_WidthFixed, 60);
            for (const auto& k : g.keys) {
                const int idx = n++;
                ImGui::PushID(idx);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(k.label);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("xpilot.%s\nDefault: %s", k.option, k.def[0] ? k.def : "(none)");

                ImGui::TableNextColumn();
                char buf[128];
                std::snprintf(buf, sizeof buf, "%s", key_values_[idx].c_str());
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##v", buf, sizeof buf)) key_values_[idx] = buf;

                ImGui::TableNextColumn();
                const bool cap = capture_ == idx;
                if (ImGui::Button(cap && !capture_add_ ? "press a key..." : "Set", ImVec2(80, 0))) { capture_ = idx; capture_add_ = false; }
                ImGui::SameLine();
                if (ImGui::Button(cap && capture_add_ ? "press..." : "Add", ImVec2(60, 0))) { capture_ = idx; capture_add_ = true; }
                ImGui::TableNextColumn();
                if (ImGui::Button("Clear")) { key_values_[idx].clear(); if (cap) capture_ = -1; }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    if (capture_ >= 0) {
        bool cancel;
        std::string name = PressedKeyName(&cancel);
        if (cancel) { capture_ = -1; cancelled_frame_ = ImGui::GetFrameCount(); }
        else if (!name.empty()) {
            std::string& v = key_values_[capture_];
            if (!capture_add_) v = name;
            else {
                auto tokens = Split(v);
                if (std::find(tokens.begin(), tokens.end(), name) == tokens.end()) v += (v.empty() ? "" : " ") + name;
            }
            capture_ = -1;
        }
    }
}

void ProfileEditor::RenderMouse() {
    ImGui::Spacing();
    ImGui::TextDisabled("Keys that are held while the mouse button is pressed. Mouse steering is always on; "
                        "the ship turns towards the pointer.");
    ImGui::Spacing();
    static const char* const names[kButtons] = {"Button 1 (left)", "Button 2 (middle)", "Button 3 (right)",
                                                "Button 4", "Button 5"};
    for (int b = 0; b < kButtons; ++b) {
        ImGui::PushID(b);
        auto& acts = button_actions_[b];
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%-18s", names[b]);
        ImGui::SameLine(170);
        int remove = -1;
        for (int i = 0; i < int(acts.size()); ++i) {
            ImGui::PushID(i);
            const KeyAction* a = FindAction(acts[i]);
            std::string text = std::string(a ? a->label : acts[i].c_str()) + "  x";
            if (ImGui::SmallButton(text.c_str())) remove = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to remove (%s)", acts[i].c_str());
            ImGui::SameLine();
            ImGui::PopID();
        }
        if (remove >= 0) acts.erase(acts.begin() + remove);
        ImGui::SetNextItemWidth(170);
        if (ImGui::BeginCombo("##add", "+ add action", ImGuiComboFlags_HeightLarge)) {
            for (const auto& g : KeyGroups())
                for (const auto& k : g.keys) {
                    if (std::find(acts.begin(), acts.end(), k.option) != acts.end()) continue;
                    if (ImGui::Selectable(k.label) && acts.size() < 10) acts.push_back(k.option);
                }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
}

void ProfileEditor::RenderModBanks() {
    ImGui::Spacing();
    ImGui::TextDisabled("Modifier sets loaded with the \"Load modifier bank\" keys. In the game you can also "
                        "reprogram a bank by holding the reprogram key.");
    ImGui::Spacing();
    for (int i = 0; i < kBanks; ++i) {
        ImGui::PushID(i);
        ModBank& m = banks_[i];
        std::string text = m.Serialize();
        ImGui::Text("Bank %d", i + 1);
        ImGui::SameLine(80);
        ImGui::TextColored(text.empty() ? ImVec4(0.55f, 0.6f, 0.7f, 1) : ImVec4(0.55f, 0.8f, 1, 1), "%s",
                           text.empty() ? "(empty)" : text.c_str());
        ImGui::Indent(24);
        ImGui::SetNextItemWidth(110);
        const char* nuc[] = {"off", "Nuclear", "Full nuclear"};
        ImGui::Combo("Nuclear", &m.nuclear, nuc, 3);
        ImGui::SameLine(0, 20);
        ImGui::Checkbox("Cluster", &m.cluster);
        ImGui::SameLine(0, 20);
        ImGui::Checkbox("Implosion", &m.implosion);
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(80);
        const char* las[] = {"off", "Stun", "Blind"};
        ImGui::Combo("Laser", &m.laser, las, 3);

        ImGui::SetNextItemWidth(100);
        ImGui::SliderInt("Velocity", &m.velocity, 0, 3, m.velocity ? "%d" : "off");
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(100);
        ImGui::SliderInt("Mini", &m.mini, 1, 4, m.mini > 1 ? "x%d" : "off");
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(100);
        ImGui::SliderInt("Spread", &m.spread, 0, 3, m.spread ? "%d" : "off");
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(100);
        ImGui::SliderInt("Power", &m.power, 0, 3, m.power ? "%d" : "off");
        ImGui::Unindent(24);
        ImGui::Separator();
        ImGui::PopID();
    }
}

bool ProfileEditor::Render(float bottom_reserve) {
    bool saved = false;
    ImGui::BeginChild("ProfileBody", ImVec2(0, -bottom_reserve));
    if (ImGui::BeginTabBar("ProfileTabs")) {
        if (ImGui::BeginTabItem("Player")) { RenderIdentity(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Keyboard")) { RenderKeys(); ImGui::EndTabItem(); }
        else if (capture_ >= 0) capture_ = -1;
        if (ImGui::BeginTabItem("Mouse")) { RenderMouse(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Modifier banks")) { RenderModBanks(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::Separator();
    const bool dirty = Dirty();
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Save", ImVec2(110, 0))) saved = Save();
    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2(110, 0))) { Load(ship_names_); status_.clear(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (!status_.empty()) ImGui::TextColored(status_error_ ? kBad : kGood, "%s", status_.c_str());
    else if (dirty) ImGui::TextDisabled("Unsaved changes");
    else ImGui::TextDisabled("%s", XpilotrcPath().c_str());
    return saved;
}
