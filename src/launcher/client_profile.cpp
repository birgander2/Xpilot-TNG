#include "client_profile.h"
#include "xpilotrc.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include <pwd.h>
#include <strings.h>
#include <unistd.h>

namespace {

// Value of "xpilot.<wanted>: ..." in the xpilotrc (empty if unavailable)
std::string ReadXpilotrcValue(const char* wanted) {
    std::string rc = XpilotrcPath();
    if (rc.empty()) return "";
    std::ifstream in(rc);
    std::string line, result;
    auto trim = [](std::string& t) {
        size_t b = t.find_first_not_of(" \t\r\n");
        size_t e = t.find_last_not_of(" \t\r\n");
        t = b == std::string::npos ? "" : t.substr(b, e - b + 1);
    };
    while (std::getline(in, line)) {
        if (line.size() < 7 || strncasecmp(line.c_str(), "xpilot", 6) != 0) continue;
        if (line[6] != '.' && line[6] != '*') continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(7, colon - 7), val = line.substr(colon + 1);
        trim(key);
        if (strcasecmp(key.c_str(), wanted) != 0) continue;
        val = val.substr(0, val.find(';'));
        trim(val);
        result = val; // last definition wins
    }
    return result;
}

// Name ("NM:" / "name:" field) of a ship shape definition, empty if there is none
std::string ShipShapeName(const std::string& shape) {
    size_t p = shape.find("(NM:");
    if (p == std::string::npos) p = shape.find("(name:");
    if (p == std::string::npos) return "";
    size_t b = shape.find(':', p) + 1, e = shape.find(')', b);
    if (e == std::string::npos) return "";
    b = shape.find_first_not_of(' ', b);
    return (b == std::string::npos || b >= e) ? "" : shape.substr(b, e - b);
}

// Ship names from a shipshapes file, in file order
std::vector<std::string> ReadShipShapeNames(const char* path) {
    std::vector<std::string> names;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::string name = ShipShapeName(line);
        if (!name.empty()) names.push_back(name);
    }
    return names;
}

// Boolean xpilotrc value, def if missing or unparsable
bool XpilotrcBool(const char* key, bool def) {
    std::string v = ReadXpilotrcValue(key);
    for (char& c : v) c = char(std::tolower((unsigned char)c));
    if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return false;
    return def;
}

// Integer xpilotrc value clamped to [lo, hi], def if missing or unparsable
int XpilotrcInt(const char* key, int def, int lo, int hi) {
    std::string v = ReadXpilotrcValue(key);
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    if (v.empty() || end == v.c_str()) return def;
    return int(std::clamp<long>(n, lo, hi));
}

// Login name of the current account (empty if unavailable)
std::string CurrentUserName() {
    if (const passwd* pw = getpwuid(getuid())) return pw->pw_name;
    const char* user = std::getenv("USER");
    return user ? user : "";
}

} // namespace

std::string XpilotrcValue(const char* key) { return ReadXpilotrcValue(key); }

void ClientProfile::Load(const char* shipshapes_path) {
    rc_name_ = ReadXpilotrcValue("name");
    std::snprintf(name, sizeof name, "%s", (rc_name_.empty() ? CurrentUserName() : rc_name_).c_str());

    // preselect the ship configured in ~/.xpilotrc (full definitions are matched by their name)
    ship_names = ReadShipShapeNames(shipshapes_path);
    // ~/.xpilotrc holds either a full definition "(NM:...)(SH:...)" or just a ship name
    std::string rc_shape = ReadXpilotrcValue("shipShape");
    if (std::string nm = ShipShapeName(rc_shape); !nm.empty()) rc_shape = nm;
    rc_has_ship = !rc_shape.empty();
    auto it = std::find(ship_names.begin(), ship_names.end(), rc_shape);
    if (it != ship_names.end()) ship_choice = int(it - ship_names.begin());

    // newbie help: ~/.xpilotrc wins, otherwise the client default (true) stays
    std::string rc_newbie = ReadXpilotrcValue("newbie");
    for (char& c : rc_newbie) c = char(std::tolower((unsigned char)c));
    if (rc_newbie == "false" || rc_newbie == "no" || rc_newbie == "off" || rc_newbie == "0") newbie = false;

    const VoiceSettings def;
    voice.chat = XpilotrcBool("voiceChat", def.chat);
    voice.activation = XpilotrcBool("voiceActivation", def.activation);
    voice.activation_team = XpilotrcBool("voiceActivationTeam", def.activation_team);
    voice.activation_level = XpilotrcInt("voiceActivationLevel", def.activation_level, -80, 0);
    voice.volume = XpilotrcInt("voiceVolume", def.volume, 0, 200);
    voice.mic_gain = XpilotrcInt("voiceMicGain", def.mic_gain, 0, 400);
    rc_voice_ = voice;
}

std::vector<std::string> ClientProfile::BuildArgs(int port, const std::string& host) const {
    std::vector<std::string> args;
    const std::string player_name = name[0] ? std::string(name) : rc_name_;
    if (!player_name.empty()) { args.push_back("-name"); args.push_back(player_name); }
    if (ship_choice >= 0 && ship_choice < int(ship_names.size())) {
        args.push_back("-shipShape");
        args.push_back(ship_names[ship_choice]);
    }
    args.push_back("-newbie");
    args.push_back(newbie ? "true" : "false");

    // voice: only what the GUI changed, the client reads the rest itself
    auto opt_bool = [&](const char* opt, bool v, bool rc) {
        if (v == rc) return;
        args.push_back(opt);
        args.push_back(v ? "true" : "false");
    };
    auto opt_int = [&](const char* opt, int v, int rc) {
        if (v == rc) return;
        args.push_back(opt);
        args.push_back(std::to_string(v));
    };
    opt_bool("-voiceChat", voice.chat, rc_voice_.chat);
    opt_bool("-voiceActivation", voice.activation, rc_voice_.activation);
    opt_bool("-voiceActivationTeam", voice.activation_team, rc_voice_.activation_team);
    opt_int("-voiceActivationLevel", voice.activation_level, rc_voice_.activation_level);
    opt_int("-voiceVolume", voice.volume, rc_voice_.volume);
    opt_int("-voiceMicGain", voice.mic_gain, rc_voice_.mic_gain);

    args.push_back("-port");
    args.push_back(std::to_string(port));
    args.push_back(host);
    return args;
}
