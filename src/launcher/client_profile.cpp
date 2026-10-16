#include "client_profile.h"

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

// Value of "xpilot.<wanted>: ..." in ~/.xpilotrc (empty if unavailable)
std::string ReadXpilotrcValue(const char* wanted) {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return "";
    std::ifstream in(std::string(home) + "/.xpilotrc");
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

// Login name of the current account (empty if unavailable)
std::string CurrentUserName() {
    if (const passwd* pw = getpwuid(getuid())) return pw->pw_name;
    const char* user = std::getenv("USER");
    return user ? user : "";
}

} // namespace

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
    args.push_back("-port");
    args.push_back(std::to_string(port));
    args.push_back(host);
    return args;
}
