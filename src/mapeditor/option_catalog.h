#pragma once

#include <map>
#include <string>
#include <vector>

namespace xpe {

// Server options as reported by "xpilot-tng-server -help" and "-dump".
struct OptionInfo {
    std::string name;                   // canonical spelling
    std::vector<std::string> aliases;
    std::string type;                   // bool, integer, real, string, list, position
    std::string help;
    std::string def;                    // compiled default
    std::string category;
    bool map_ok = true;                 // may be set in a map file
    bool invisible = false;
};

class OptionCatalog {
public:
    // Runs the server binary; returns false if it could not be read.
    bool Load(const std::string& server_bin);
    bool Loaded() const { return !all_.empty(); }

    const OptionInfo* Find(const std::string& name) const;     // name or alias, any case
    const std::vector<OptionInfo>& All() const { return all_; }
    static const std::vector<std::string>& Categories();

private:
    std::vector<OptionInfo> all_;
    std::map<std::string, size_t> index_;  // lower-case name/alias -> all_ index
};

std::string Lower(std::string s);
bool IsTrue(const std::string& v);

} // namespace xpe
