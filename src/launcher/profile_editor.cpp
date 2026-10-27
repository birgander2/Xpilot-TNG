#include "profile_editor.h"
#include "client_profile.h"

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
const ImVec4 kWarn(0.95f, 0.75f, 0.30f, 1.0f);

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
        {"Voice", {
            {"keyVoiceTeam", "Push-to-talk, team channel", "Alt_L"},
            {"keyVoiceAll", "Push-to-talk, all channel", "Insert"},
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

void ProfileEditor::Load(const std::vector<std::string>& ship_names, bool keep_unsaved) {
    const ProfileEditor unsaved = *this;   // the edits, if they are to be kept
    const bool had_edits = loaded_;
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

    auto get_bool = [&](const char* key, bool def) {
        std::string v = get(key, def ? "true" : "false");
        for (char& ch : v) ch = char(tolower((unsigned char)ch));
        if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
        if (v == "false" || v == "no" || v == "off" || v == "0") return false;
        return def;
    };
    auto get_int = [&](const char* key, int def, int lo, int hi) {
        std::string v = get(key, std::to_string(def));
        char* end = nullptr;
        long n = std::strtol(v.c_str(), &end, 10);
        return end == v.c_str() ? def : int(std::clamp<long>(n, lo, hi));
    };
    audio_.sound = get_bool("sound", true);
    audio_.max_volume = get_int("maxVolume", 100, 0, 100);
    audio_.voice_chat = get_bool("voiceChat", true);
    audio_.voice_activation = get_bool("voiceActivation", true);
    audio_.voice_activation_team = get_bool("voiceActivationTeam", true);
    audio_.voice_level = get_int("voiceActivationLevel", -40, -80, 0);
    audio_.voice_volume = get_int("voiceVolume", 100, 0, 200);
    audio_.voice_mic_gain = get_int("voiceMicGain", 100, 0, 400);
    {
        const int jb = get_int("maxJitterBuffer", 0, 0, 500);
        net_.jitter_buffer = jb > 0;
        net_.jitter_ms = jb > 0 ? jb : 150;
    }
    net_.mouse_prediction = get_bool("dirPrediction", true);
    net_.key_turn_mouse = get_bool("keyTurnMouse", false);
    net_.key_turn_rate = get_int("keyTurnRate", 630, 30, 2000);

    shown_.clear();
    // snapshot of everything the editor can write, to detect edits
    for (const auto& kv : AudioValues()) shown_[kv.first] = kv.second;
    for (const auto& kv : NetworkValues()) shown_[kv.first] = kv.second;
    loaded_ = true;
    shown_["name"] = name_;
    shown_["shipShape"] = ship_ >= 0 ? ship_names_[ship_] : rc_ship_;
    int n = 0;
    for (const auto& g : KeyGroups())
        for (const auto& k : g.keys) shown_[k.option] = key_values_[n++];
    for (int i = 0; i < kButtons; ++i) shown_["pointerButton" + std::to_string(i + 1)] = Join(button_actions_[i]);
    for (int i = 0; i < kBanks; ++i) shown_["modifierBank" + std::to_string(i + 1)] = banks_[i].Serialize();

    if (keep_unsaved && had_edits) {   // shown_ stays what the xpilotrc has
        std::memcpy(name_, unsaved.name_, sizeof name_);
        ship_ = unsaved.ship_;
        key_values_ = unsaved.key_values_;
        for (int i = 0; i < kButtons; ++i) button_actions_[i] = unsaved.button_actions_[i];
        for (int i = 0; i < kBanks; ++i) banks_[i] = unsaved.banks_[i];
        audio_ = unsaved.audio_;
        net_ = unsaved.net_;
    }
}

std::vector<std::pair<std::string, std::string>> ProfileEditor::AudioValues() const {
    auto b = [](bool v) { return std::string(v ? "true" : "false"); };
    return {
        {"sound", b(audio_.sound)},
        {"maxVolume", std::to_string(audio_.max_volume)},
        {"voiceChat", b(audio_.voice_chat)},
        {"voiceActivation", b(audio_.voice_activation)},
        {"voiceActivationTeam", b(audio_.voice_activation_team)},
        {"voiceActivationLevel", std::to_string(audio_.voice_level)},
        {"voiceVolume", std::to_string(audio_.voice_volume)},
        {"voiceMicGain", std::to_string(audio_.voice_mic_gain)},
    };
}

std::vector<std::pair<std::string, std::string>> ProfileEditor::NetworkValues() const {
    auto b = [](bool v) { return std::string(v ? "true" : "false"); };
    return {
        {"maxJitterBuffer", std::to_string(net_.jitter_buffer ? net_.jitter_ms : 0)},
        {"dirPrediction", b(net_.mouse_prediction)},
        {"keyTurnMouse", b(net_.key_turn_mouse)},
        {"keyTurnRate", std::to_string(net_.key_turn_rate)},
    };
}

std::vector<std::pair<std::string, std::string>> ProfileEditor::Changes() const {
    std::vector<std::pair<std::string, std::string>> cur, changed;
    for (const auto& kv : AudioValues()) cur.push_back(kv);
    for (const auto& kv : NetworkValues()) cur.push_back(kv);
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

std::vector<std::string> ProfileEditor::UnsavedArgs() const {
    std::vector<std::string> args;
    if (!loaded_) return args;   // editor never opened: the xpilotrc is the truth
    for (const auto& kv : Changes()) {
        if (kv.first == "name" || kv.first == "shipShape") continue;   // see ApplyIdentityTo()
        args.push_back("-" + kv.first);
        args.push_back(kv.second);
    }
    return args;
}

void ProfileEditor::ApplyIdentityTo(ClientProfile& profile) const {
    if (!loaded_) return;
    auto it = shown_.find("name");
    if (it != shown_.end() && it->second != name_)
        std::snprintf(profile.name, sizeof profile.name, "%s", name_);
    it = shown_.find("shipShape");
    if (it != shown_.end() && it->second != (ship_ >= 0 ? ship_names_[ship_] : rc_ship_))
        profile.ship_choice = ship_;
}

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
        // "Label: [value]" - label first, then the choice
        auto label = [](const char* text, const char* tip = nullptr) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(text);
            if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            ImGui::SameLine();
        };
        const char* nuc[] = {"off", "nuclear", "full nuclear"};
        label("Nuclear:", "Nuclear warhead for mines and missiles (full nuclear needs more fuel)");
        ImGui::SetNextItemWidth(120);
        ImGui::Combo("##nuclear", &m.nuclear, nuc, 3);
        ImGui::SameLine(0, 24);
        label("Cluster:", "Mines and missiles explode into cluster debris");
        ImGui::Checkbox("##cluster", &m.cluster);
        ImGui::SameLine(0, 24);
        label("Implosion:", "Explosion pulls things in instead of pushing them away");
        ImGui::Checkbox("##implosion", &m.implosion);
        ImGui::SameLine(0, 24);
        const char* las[] = {"normal", "stun", "blind"};
        label("Laser:", "Normal: lethal laser. Stun: paralyses the target. Blind: blinds the target (both replace the lethal hit)");
        ImGui::SetNextItemWidth(95);
        ImGui::Combo("##laser", &m.laser, las, 3);

        label("Velocity:", "Speed of the cluster debris fragments");
        ImGui::SetNextItemWidth(80);
        ImGui::SliderInt("##velocity", &m.velocity, 0, 3, "%d");
        ImGui::SameLine(0, 24);
        label("Split:", "Split mines and missiles into several smaller ones");
        ImGui::SetNextItemWidth(80);
        ImGui::SliderInt("##mini", &m.mini, 1, 4, "%d");
        ImGui::SameLine(0, 24);
        label("Spread:", "Spread angle of the mines / missiles");
        ImGui::SetNextItemWidth(80);
        ImGui::SliderInt("##spread", &m.spread, 0, 3, "%d");
        ImGui::SameLine(0, 24);
        label("Power:", "Increases speed and agility of missiles (but fewer explosion fragments)");
        ImGui::SetNextItemWidth(80);
        ImGui::SliderInt("##power", &m.power, 0, 3, "%d");
        ImGui::Unindent(24);
        ImGui::Separator();
        ImGui::PopID();
    }
}

void ProfileEditor::RenderAudio() {
    static const char* kModes[] = {"Push-to-talk only (keys: Keyboard > Voice)", "Voice activation"};

    ImGui::Spacing();
    ImGui::SeparatorText("Game sounds");
    ImGui::Checkbox("Sound", &audio_.sound);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sounds of the game (xpilot.sound)");
    ImGui::BeginDisabled(!audio_.sound);
    ImGui::SetNextItemWidth(280);
    ImGui::SliderInt("Volume", &audio_.max_volume, 0, 100, "%d%%");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volume of the game sounds (xpilot.maxVolume)");
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::SeparatorText("Voice chat");
    ImGui::Checkbox("Voice chat", &audio_.voice_chat);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hear other players and talk with a microphone (xpilot.voiceChat)");
    ImGui::BeginDisabled(!audio_.voice_chat);
    ImGui::SetNextItemWidth(280);
    int mode = audio_.voice_activation ? 1 : 0;
    if (ImGui::Combo("Sending", &mode, kModes, IM_ARRAYSIZE(kModes))) audio_.voice_activation = (mode == 1);
    if (audio_.voice_activation) {
        ImGui::SetNextItemWidth(280);
        ImGui::SliderInt("Activation level", &audio_.voice_level, -80, 0, "%d dB");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Microphone level that opens the channel.\nRaise it if background noise triggers it.");
        ImGui::Checkbox("Voice activation talks to the team", &audio_.voice_activation_team);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off: voice activation uses the channel everybody nearby hears");
    }
    ImGui::SetNextItemWidth(280);
    ImGui::SliderInt("Voice volume", &audio_.voice_volume, 0, 200, "%d%%");
    ImGui::SetNextItemWidth(280);
    ImGui::SliderInt("Microphone gain", &audio_.voice_mic_gain, 0, 400, "%d%%");
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextDisabled("Voice chat needs an XPilot TNG server 5.0.2 or newer.");
    ImGui::TextDisabled("Mute a player in game with \\ignore <name>.");
}

void ProfileEditor::RenderNetwork() {
    ImGui::Spacing();
    ImGui::SeparatorText("Jitter buffer");
    ImGui::Checkbox("Jitter buffer", &net_.jitter_buffer);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Plays incoming frames at a steady rate (xpilot.maxJitterBuffer).\n"
                          "The buffer only grows as far as frames really arrive late and shrinks again\n"
                          "when the connection calms down. On a steady connection it adds almost nothing.");
    ImGui::BeginDisabled(!net_.jitter_buffer);
    ImGui::SetNextItemWidth(280);
    ImGui::SliderInt("Maximum delay", &net_.jitter_ms, 10, 500, "%d ms");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Upper limit of the extra delay. More smooths longer interruptions\n"
                          "(unsteady Wi-Fi), but everything reacts later while the buffer is large.\n"
                          "150 ms is a good start.");
    ImGui::EndDisabled();
    ImGui::TextDisabled("Smooths out stutter on unsteady connections (Wi-Fi, long distance).");

    ImGui::Spacing();
    ImGui::SeparatorText("Turning");
    ImGui::Checkbox("Mouse prediction", &net_.mouse_prediction);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Your ship turns on the screen right away when you move the mouse,\n"
                          "instead of after the round trip to the server (xpilot.dirPrediction).");
    ImGui::Checkbox("Keyboard turns emulate mouse turning", &net_.key_turn_mouse);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("While a turn key is held, the client sends mouse-like turns at a fixed speed\n"
                          "instead of telling the server about the key (xpilot.keyTurnMouse).\n"
                          "The ship turns exactly as far as the key was held, however long the packets\n"
                          "take, and mouse prediction applies. Needs turnResistance 0.");
    ImGui::BeginDisabled(!net_.key_turn_mouse);
    ImGui::SetNextItemWidth(280);
    ImGui::SliderInt("Turn speed", &net_.key_turn_rate, 30, 1000, "%d deg/s");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How fast the ship turns while a key is held (xpilot.keyTurnRate).\n"
                          "Without this option the speed comes from the server settings, usually\n"
                          "about 630 deg/s. Lower is more precise, higher is quicker.");
    ImGui::EndDisabled();
    if (net_.key_turn_mouse && !net_.mouse_prediction)
        ImGui::TextColored(kWarn, "Keyboard turning relies on mouse prediction to look smooth.");
    ImGui::TextDisabled("Might help hiding the lag when turning. Thrust, shots and other ships are not affected.");
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
        if (ImGui::BeginTabItem("Audio")) { RenderAudio(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Network")) { RenderNetwork(); ImGui::EndTabItem(); }
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
