#include "option_catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace xpe {

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

bool IsTrue(const std::string& v) {
    const std::string l = Lower(v);
    return l == "yes" || l == "true" || l == "on" || l == "1";
}

namespace {

std::string RunCommand(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

std::string ShellQuote(const std::string& s) {
    std::string o = "'";
    for (char c : s) {
        if (c == '\'') o += "'\\''";
        else o += c;
    }
    return o + "'";
}

bool Has(const std::string& lname, std::initializer_list<const char*> keys) {
    for (const char* k : keys)
        if (lname.find(k) != std::string::npos) return true;
    return false;
}

// Rough grouping by name, only used to make the option list browsable.
std::string CategoryOf(const std::string& name) {
    const std::string l = Lower(name);
    if (Has(l, {"map", "edge", "extraborder", "world", "dataurl", "polygonmode"}) && !Has(l, {"lives"}))
        return "Map";
    if (Has(l, {"robot", "bot", "smart"}) && !Has(l, {"smartmissile"})) return "Robots";
    if (Has(l, {"team", "alliance"})) return "Teams";
    if (Has(l, {"ball", "treasure", "ctf", "capturetheflag"})) return "Ball";
    if (Has(l, {"initial", "item", "max", "min"}) &&
        Has(l, {"fuel", "wide", "rear", "afterburn", "cloak", "sensor", "transport", "tank", "mine", "missile",
                "ecm", "laser", "emergency", "tractor", "autopilot", "deflector", "hyper", "phasing", "mirror",
                "armor", "item"}))
        return "Items";
    if (Has(l, {"shot", "missile", "mine", "laser", "nuke", "torp", "cluster", "heat", "pulse", "fire", "modifier",
                "spark", "shield", "kill", "ecm"}))
        return "Weapons";
    if (Has(l, {"cannon", "target", "wormhole", "asteroid", "fuel", "gravity", "grav", "friction"}))
        return "Map objects";
    if (Has(l, {"timing", "race", "lives", "round", "elimination", "tag", "score", "rank", "reset", "match",
                "teamcup"}))
        return "Game modes";
    if (Has(l, {"mass", "speed", "thrust", "bounce", "brake", "power", "turn", "push", "wreck", "debris"}))
        return "Physics";
    if (Has(l, {"radar", "visib", "visible", "identify", "distinguish", "cloaked"})) return "Visibility";
    if (Has(l, {"fps", "port", "meta", "contact", "password", "motd", "greeting", "log", "record", "idle",
                "pause", "quit", "admin", "search", "ip", "lock", "sound"}))
        return "Server";
    return "Other";
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

} // namespace

const std::vector<std::string>& OptionCatalog::Categories() {
    static const std::vector<std::string> cats = {"Map",     "Game modes", "Teams",      "Robots",
                                                  "Ball",    "Weapons",    "Items",      "Map objects",
                                                  "Physics", "Visibility", "Server",     "Other"};
    return cats;
}

bool OptionCatalog::Load(const std::string& server_bin) {
    all_.clear();
    index_.clear();
    const std::string help = RunCommand(ShellQuote(server_bin) + " -help 2>/dev/null");
    if (help.empty()) return false;

    std::istringstream in(help);
    std::string line;
    OptionInfo* cur = nullptr;
    while (std::getline(in, line)) {
        if (line.rfind("    -", 0) == 0) {
            // "    -name or alias <type>"  or  "    -/+name or alias "
            std::string spec = Trim(line.substr(4));
            OptionInfo info;
            info.type = "bool";
            if (spec.rfind("-/+", 0) == 0) spec = spec.substr(3);
            else if (spec[0] == '-') spec = spec.substr(1);
            size_t lt = spec.find('<');
            if (lt != std::string::npos) {
                size_t gt = spec.find('>', lt);
                info.type = spec.substr(lt + 1, gt == std::string::npos ? std::string::npos : gt - lt - 1);
                spec = spec.substr(0, lt);
            }
            std::istringstream ws(spec);
            std::string w;
            bool first = true;
            while (ws >> w) {
                if (w == "or") continue;
                if (first) info.name = w;
                else info.aliases.push_back(w);
                first = false;
            }
            if (info.name.empty() || info.type == "void" || info.name == "help" || info.name == "version" ||
                info.name == "dump") {
                cur = nullptr;
                continue;
            }
            all_.push_back(std::move(info));
            cur = &all_.back();
        } else if (cur && !line.empty() && line[0] == '\t') {
            std::string t = Trim(line);
            if (t.rfind("[ Flags:", 0) == 0) {
                cur->map_ok = t.find("any") != std::string::npos;
                cur->invisible = t.find("invisible") != std::string::npos;
            } else {
                if (!cur->help.empty()) cur->help += "\n";
                cur->help += t;
            }
        } else if (Trim(line).empty()) {
            cur = nullptr;
        }
    }
    for (size_t i = 0; i < all_.size(); i++) {
        all_[i].category = CategoryOf(all_[i].name);
        index_[Lower(all_[i].name)] = i;
        for (const auto& a : all_[i].aliases) index_.emplace(Lower(a), i);
    }

    const std::string dump = RunCommand(ShellQuote(server_bin) + " -dump 2>/dev/null");
    std::istringstream ds(dump);
    while (std::getline(ds, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ' ') continue;
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        auto it = index_.find(Lower(Trim(line.substr(0, c))));
        if (it != index_.end()) all_[it->second].def = Trim(line.substr(c + 1));
    }
    std::sort(all_.begin(), all_.end(),
              [](const OptionInfo& a, const OptionInfo& b) { return Lower(a.name) < Lower(b.name); });
    index_.clear();
    for (size_t i = 0; i < all_.size(); i++) {
        index_[Lower(all_[i].name)] = i;
        for (const auto& a : all_[i].aliases) index_.emplace(Lower(a), i);
    }
    return !all_.empty();
}

const OptionInfo* OptionCatalog::Find(const std::string& name) const {
    auto it = index_.find(Lower(name));
    return it == index_.end() ? nullptr : &all_[it->second];
}

} // namespace xpe
