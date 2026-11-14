#include "ship_files.h"
#include "xpilotrc.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ss {

bool LoadShipFile(const std::string& path, std::vector<Entry>* out, std::string* error) {
    std::ifstream in(path);
    if (!in) { if (error) *error = "cannot open " + path; return false; }
    out->clear();
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t p = line.find('(');
        if (p == std::string::npos) continue;
        std::string head = line.substr(0, p);
        size_t nb = head.find_first_not_of(" \t");
        if (nb != std::string::npos && (head[nb] == '#' || head[nb] == ';' || head[nb] == '!')) continue;
        Entry e;
        if (!Parse(line, &e.shape)) continue;
        if (e.shape.hull().empty()) continue;
        e.valid = Validate(e.shape).valid;
        out->push_back(std::move(e));
    }
    return true;
}

bool SaveShipFile(const std::string& path, const std::vector<Entry>& entries, std::string* error) {
    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) { if (error) *error = "cannot write " + tmp; return false; }
        out << "# XPilot TNG ship collection (xpilot-tng-shipeditor).\n"
               "# Usable as shipShapeFile: one ship per line, selected by its NM: name.\n";
        for (const auto& e : entries) out << "xpilot.shipShape: " << Serialize(e.shape) << '\n';
        if (!out.good()) { if (error) *error = "cannot write " + tmp; return false; }
    }
    fs::rename(tmp, p, ec);
    if (ec) { if (error) *error = "cannot replace " + path + ": " + ec.message(); return false; }
    return true;
}

std::string ConfigDir() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        return std::string(appdata) + "/xpilot-tng";
#endif
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && xdg[0] == '/') return std::string(xdg) + "/xpilot-tng";
    if (home && *home) return std::string(home) + "/.config/xpilot-tng";
    return ".";
}

std::string CollectionPath() {
    std::string path = ConfigDir() + "/shipshapes.cfg";
    // one-time migration of the former shipshapes.txt
    std::error_code ec;
    std::string old = ConfigDir() + "/shipshapes.txt";
    if (!fs::exists(path, ec) && fs::exists(old, ec)) fs::rename(old, path, ec);
    return path;
}

bool XpilotrcGet(const std::string& key, std::string* value) {
    return XpilotrcLookup(key.c_str(), value);
}

bool XpilotrcSet(const std::string& key, const std::string& value, std::string* error) {
    return XpilotrcWrite({{key, value}}, error);
}

std::string FindDataDir(const std::string& app_dir) {
    std::vector<std::string> cands;
    if (const char* env = std::getenv("XPILOT_DATADIR"); env && *env) cands.push_back(env);
    for (const char* rel : {"/../../lib", "/../lib", "/lib", "/../share/xpilot-tng"})
        cands.push_back(app_dir + rel);
    cands.push_back("lib");
    cands.push_back("../../lib");
#ifdef CONF_DATADIR
    cands.push_back(CONF_DATADIR);
#endif
    cands.push_back("/usr/local/share/xpilot-tng");
    cands.push_back("/usr/share/xpilot-tng");
    for (const auto& c : cands) {
        std::error_code ec;
        if (fs::exists(fs::path(c) / "shipshapes.cfg", ec)) return fs::weakly_canonical(c, ec).string();
    }
    return "";
}

} // namespace ss
