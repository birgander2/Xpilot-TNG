#include "options_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool ValidKey(const std::string& k) {
    if (k.empty() || !std::isalpha(static_cast<unsigned char>(k[0]))) return false;
    for (char c : k)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    return true;
}

// Option spellings used in map files / on the command line that differ from the
// canonical option name of the server.
const std::map<std::string, std::string>& Aliases() {
    static const std::map<std::string, std::string> a = {
        {"robots", "maxrobots"},
        {"smartrobots", "maxsmartrobots"},
        {"fps", "framespersecond"},
        {"port", "contactport"},
        {"teams", "teamplay"},
        {"map", "mapfilename"},
        {"lives", "worldlives"},
        {"reportmeta", "reporttometaserver"},
    };
    return a;
}

} // namespace

std::string OptionsManager::Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool OptionsManager::ParseBool(const std::string& s, bool def) {
    std::string l = Lower(Trim(s));
    if (l == "yes" || l == "true" || l == "on" || l == "1") return true;
    if (l == "no" || l == "false" || l == "off" || l == "0") return false;
    return def;
}

bool OptionsManager::SameValue(const std::string& a, const std::string& b) {
    std::string x = Trim(a), y = Trim(b);
    if (x == y) return true;
    // booleans
    std::string lx = Lower(x), ly = Lower(y);
    static const char* kBool[] = {"yes", "no", "true", "false", "on", "off"};
    auto is_bool = [](const std::string& s) {
        for (const char* b : kBool) if (s == b) return true;
        return false;
    };
    if (is_bool(lx) && is_bool(ly)) return ParseBool(lx) == ParseBool(ly);
    // numbers
    char *ex = nullptr, *ey = nullptr;
    double dx = std::strtod(x.c_str(), &ex);
    double dy = std::strtod(y.c_str(), &ey);
    if (!x.empty() && !y.empty() && *ex == '\0' && *ey == '\0')
        return std::fabs(dx - dy) <= 1e-9 * std::max(1.0, std::max(std::fabs(dx), std::fabs(dy)));
    return lx == ly;
}

// --- ports -----------------------------------------------------------------

bool OptionsManager::IsPortFree(int port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    int res = bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    close(sock);
    return res == 0;
}

int OptionsManager::FindFreePort(int start_port) {
    for (int p = start_port; p < start_port + 100; ++p)
        if (IsPortFree(p)) return p;
    return start_port;
}

// --- baseline --------------------------------------------------------------

OptionsManager::OptionsManager() = default;

std::string OptionsManager::Canonical(const std::string& key) const {
    std::string l = Lower(Trim(key));
    auto al = Aliases().find(l);
    if (al != Aliases().end()) l = al->second;
    auto it = base_.find(l);
    return it == base_.end() ? "" : it->second.name;
}

bool OptionsManager::LoadBaseline(const std::string& server_bin, const std::string& defaults_path) {
    base_.clear();

    std::string cmd = "'" + server_bin + "' -dump 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return false;
    char buf[4096];
    while (fgets(buf, sizeof(buf), p)) {
        std::string line = Trim(buf);
        if (line.empty() || line[0] == '#') continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = Trim(line.substr(0, colon));
        if (!ValidKey(key)) continue;
        Entry e;
        e.name = key;
        e.value = Trim(line.substr(colon + 1));
        e.is_bool = (e.value == "true" || e.value == "false");
        base_[Lower(key)] = e;
    }
    pclose(p);
    if (base_.empty()) return false;

    // lib/defaults.txt is honoured by a normal server start, so it is part of the baseline.
    ParseDefaultsFile(defaults_path);

    current_.clear();
    for (auto& [k, e] : base_) current_[k] = e.value;
    touched_.clear();
    return true;
}

void OptionsManager::ParseDefaultsFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return;
    std::string line, skip_until;
    while (std::getline(in, line)) {
        if (!skip_until.empty()) {
            if (Trim(line) == skip_until) skip_until.clear();
            continue;
        }
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t colon = t.find(':');
        if (colon == std::string::npos) continue;
        std::string key = Trim(t.substr(0, colon));
        std::string val = Trim(t.substr(colon + 1));

        size_t ml = val.find("\\multiline:");
        if (ml != std::string::npos) {           // define: / multiline blocks are not plain values
            skip_until = Trim(val.substr(ml + 11));
            continue;
        }
        if (val.rfind("\\override:", 0) == 0) val = Trim(val.substr(10));
        if (Lower(key) == "define") continue;

        std::string canon = Canonical(key);
        if (canon.empty()) continue;
        auto& e = base_[Lower(canon)];
        e.value = e.is_bool ? (ParseBool(val) ? "true" : "false") : val;
    }
}

// --- map / current values --------------------------------------------------

void OptionsManager::SelectMap(const MapInfo& map) {
    map_values_.clear();
    map_raw_.clear();
    touched_.clear();

    for (const auto& [k, v] : map.map_options) {
        std::string lk = Lower(k);
        if (lk == "mapname" || lk == "mapauthor" || lk == "mapwidth" || lk == "mapheight" ||
            lk == "mapdata")
            continue;
        map_raw_.emplace_back(k, v);
        std::string canon = Canonical(k);
        if (canon.empty()) continue;
        const Entry& e = base_.at(Lower(canon));
        map_values_[Lower(canon)] = e.is_bool ? (ParseBool(v) ? "true" : "false") : v;
    }

    current_.clear();
    for (auto& [k, e] : base_) current_[k] = e.value;
    for (auto& [k, v] : map_values_) current_[k] = v;
}

bool OptionsManager::Known(const std::string& key) const { return !Canonical(key).empty(); }

std::string OptionsManager::Get(const std::string& key) const {
    std::string c = Canonical(key);
    if (c.empty()) return "";
    auto it = current_.find(Lower(c));
    return it == current_.end() ? "" : it->second;
}

bool OptionsManager::IsBool(const std::string& key) const {
    auto it = base_.find(Lower(key));
    return it != base_.end() && it->second.is_bool;
}

bool OptionsManager::GetBool(const std::string& key, bool def) const {
    std::string v = Get(key);
    return v.empty() ? def : ParseBool(v, def);
}

int OptionsManager::GetInt(const std::string& key, int def) const {
    std::string v = Get(key);
    if (v.empty()) return def;
    char* end = nullptr;
    double d = std::strtod(v.c_str(), &end);
    return (end == v.c_str()) ? def : static_cast<int>(std::lround(d));
}

double OptionsManager::GetFloat(const std::string& key, double def) const {
    std::string v = Get(key);
    if (v.empty()) return def;
    char* end = nullptr;
    double d = std::strtod(v.c_str(), &end);
    return (end == v.c_str()) ? def : d;
}

void OptionsManager::Set(const std::string& key, const std::string& value) {
    std::string c = Canonical(key);
    if (c.empty()) return;
    std::string lc = Lower(c);
    const Entry& e = base_.at(lc);
    std::string v = e.is_bool ? (ParseBool(value) ? "true" : "false") : value;
    current_[lc] = v;

    auto m = map_values_.find(lc);
    const std::string& reference = (m != map_values_.end()) ? m->second : e.value;
    if (SameValue(v, reference)) touched_.erase(lc);
    else touched_.insert(lc);

    if (lc == "worldlives") {
        char* end = nullptr;
        double d = std::strtod(v.c_str(), &end);
        int lives = (end != v.c_str()) ? static_cast<int>(std::lround(d)) : 0;
        SetBool("limitedLives", lives > 0);
    }
}

void OptionsManager::SetBool(const std::string& key, bool v) { Set(key, v ? "true" : "false"); }
void OptionsManager::SetInt(const std::string& key, int v) { Set(key, std::to_string(v)); }
void OptionsManager::SetFloat(const std::string& key, double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    Set(key, buf);
}

bool OptionsManager::IsModified(const std::string& key) const {
    std::string c = Canonical(key);
    return !c.empty() && touched_.count(Lower(c)) > 0;
}

void OptionsManager::Reset(const std::string& key) {
    std::string c = Canonical(key);
    if (c.empty()) return;
    std::string lc = Lower(c);
    auto m = map_values_.find(lc);
    current_[lc] = (m != map_values_.end()) ? m->second : base_.at(lc).value;
    touched_.erase(lc);
    if (lc == "worldlives") {
        Reset("limitedLives");
    }
}

// --- launch ----------------------------------------------------------------

std::vector<std::string> OptionsManager::BuildServerArgs(LaunchMode mode, const std::string& map_path,
                                                         int port, bool minimal) const {
    // key (lower) -> argument vector; ordered so the command line is deterministic
    std::map<std::string, std::vector<std::string>> opts;

    auto emit = [&](const std::string& lc, const std::string& value) {
        const Entry& e = base_.at(lc);
        if (e.is_bool) opts[lc] = {std::string(ParseBool(value) ? "-" : "+") + e.name};
        else opts[lc] = {"-" + e.name, value};
    };

    for (const auto& lc : touched_) {
        auto it = current_.find(lc);
        if (it != current_.end() && base_.count(lc)) emit(lc, it->second);
    }

    // Mode specific options (always set explicitly, independent of defaults/map).
    // In minimal mode (display only) values equal to the baseline are left out.
    auto is_default = [&](const std::string& lc, const std::string& value) {
        return minimal && base_.count(lc) && SameValue(value, base_.at(lc).value);
    };
    if (!is_default("contactport", std::to_string(port)))
        opts["contactport"] = {"-contactPort", std::to_string(port)};
    if (base_.count("noquit")) {
        bool keep_running = (mode == LaunchMode::Server);   // local: quit when everybody left
        if (!is_default("noquit", keep_running ? "true" : "false"))
            opts["noquit"] = {keep_running ? "-noQuit" : "+noQuit"};
    }
    if (base_.count("reporttometaserver")) {
        std::string v = (mode == LaunchMode::Local) ? "false" : Get("reportToMetaServer");
        if (!is_default("reporttometaserver", v)) emit("reporttometaserver", v);
    }

    std::vector<std::string> args;
    if (!map_path.empty()) {
        args.push_back("-mapFileName");
        args.push_back(map_path);
    }
    for (const auto& [k, v] : opts) args.insert(args.end(), v.begin(), v.end());
    return args;
}
