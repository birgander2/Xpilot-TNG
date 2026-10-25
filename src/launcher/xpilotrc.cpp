#include "xpilotrc.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <strings.h>
#include <unistd.h>

std::string XpilotrcPath() {
    if (const char* env = std::getenv("XPILOTRC")) return env;
#ifdef _WIN32
    // Same location as the client (Xpilotrc_get_filename)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        return std::string(appdata) + "/xpilot-tng/xpilotrc";
#endif
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::string path;
    if (xdg && xdg[0] == '/') path = std::string(xdg) + "/xpilot-tng/xpilotrc";
    else if (home && *home) path = std::string(home) + "/.config/xpilot-tng/xpilotrc";
    else return "";
    if (access(path.c_str(), F_OK) != 0 && home && *home) {
        std::string legacy = std::string(home) + "/.xpilotrc";
        if (access(legacy.c_str(), F_OK) == 0) return legacy;
    }
    return path;
}

namespace {

// Option name of an "xpilot.<name>: ..." line, empty for anything else
std::string LineKey(const std::string& line) {
    if (line.size() < 7 || strncasecmp(line.c_str(), "xpilot", 6) != 0) return "";
    if (line[6] != '.' && line[6] != '*') return "";
    size_t colon = line.find(':');
    if (colon == std::string::npos) return "";
    std::string key = line.substr(7, colon - 7);
    size_t b = key.find_first_not_of(" \t"), e = key.find_last_not_of(" \t");
    return b == std::string::npos ? "" : key.substr(b, e - b + 1);
}

} // namespace

bool XpilotrcWrite(const std::vector<std::pair<std::string, std::string>>& values, std::string* error) {
    namespace fs = std::filesystem;
    const std::string path = XpilotrcPath();
    if (path.empty()) { if (error) *error = "no xpilotrc location (HOME not set)"; return false; }

    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    for (const auto& [key, value] : values) {
        int last = -1;
        for (int i = 0; i < int(lines.size()); ++i)
            if (strcasecmp(LineKey(lines[i]).c_str(), key.c_str()) == 0) last = i;
        std::string text = "xpilot." + key + ":" + (value.empty() ? "" : " " + value);
        if (last < 0) { lines.push_back(text); continue; }
        for (int i = last - 1; i >= 0; --i)   // drop earlier duplicates
            if (strcasecmp(LineKey(lines[i]).c_str(), key.c_str()) == 0) { lines.erase(lines.begin() + i); --last; }
        lines[last] = text;
    }

    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    if (fs::exists(p) && !fs::exists(path + ".bak")) fs::copy_file(p, path + ".bak", ec);

    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        for (const auto& l : lines) out << l << '\n';
        if (!out.good()) { if (error) *error = "cannot write " + tmp; return false; }
    }
    fs::rename(tmp, p, ec);
    if (ec) { if (error) *error = "cannot replace " + path + ": " + ec.message(); return false; }
    return true;
}

bool XpilotrcLookup(const char* key, std::string* value) {
    std::ifstream in(XpilotrcPath());
    std::string line;
    bool found = false;
    while (std::getline(in, line)) {
        if (strcasecmp(LineKey(line).c_str(), key) != 0) continue;
        std::string val = line.substr(line.find(':') + 1);
        val = val.substr(0, val.find(';'));
        size_t b = val.find_first_not_of(" \t\r\n"), e = val.find_last_not_of(" \t\r\n");
        *value = b == std::string::npos ? "" : val.substr(b, e - b + 1);
        found = true;
    }
    return found;
}
