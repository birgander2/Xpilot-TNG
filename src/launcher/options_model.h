#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "map_preview.h"

enum class LaunchMode { Local, Server };

// Holds the effective server options shown in the GUI.
//
//   baseline = compiled defaults (server -dump) <- lib/defaults.txt <- map file
//   current  = baseline + whatever the user changed in the GUI
//
// On launch only options that differ from the baseline are passed on the command
// line. Command line options have the highest priority in the server, so edits win
// over map and defaults file, while untouched values keep behaving like a plain
// server start (defaults.txt is still honoured).
class OptionsManager {
public:
    OptionsManager();

    // Reads compiled defaults (runs "<server_bin> -dump") and defaults.txt.
    // Returns false if the dump could not be read (GUI then works with raw map options only).
    bool LoadBaseline(const std::string& server_bin, const std::string& defaults_path);

    // Switches to a map: discards all GUI edits and loads the map's values.
    void SelectMap(const MapInfo& map);
    // Discards all GUI edits for the current map.
    void ResetAll();

    // --- typed access by canonical option name (case-insensitive) ---------
    bool Known(const std::string& key) const;
    std::string Get(const std::string& key) const;
    bool IsBool(const std::string& key) const;
    bool GetBool(const std::string& key, bool def = false) const;
    int GetInt(const std::string& key, int def = 0) const;
    double GetFloat(const std::string& key, double def = 0.0) const;
    void Set(const std::string& key, const std::string& value);
    void SetBool(const std::string& key, bool v);
    void SetInt(const std::string& key, int v);
    void SetFloat(const std::string& key, double v);

    bool IsModified(const std::string& key) const;   // differs from baseline+map
    void Reset(const std::string& key);
    int ModifiedCount() const;

    // Options defined by the current map file, in original spelling/order of the file.
    const std::vector<std::pair<std::string, std::string>>& MapOptions() const { return map_raw_; }
    // Canonical option name for a (possibly aliased) key, or "" if unknown to the server.
    std::string Canonical(const std::string& key) const;

    // Server command line (without binary). Includes -map and all modified options
    // plus mode specific options, so changes made in the GUI always take effect.
    // minimal: omit mode specific options that equal the baseline (for display only).
    std::vector<std::string> BuildServerArgs(LaunchMode mode, const std::string& map_path,
                                             int port, bool minimal = false) const;

    // Port helpers
    static bool IsPortFree(int port);
    static int FindFreePort(int start_port = 15345);

private:
    struct Entry {
        std::string name;   // canonical spelling, e.g. "maxRobots"
        std::string value;  // baseline value (defaults + defaults.txt)
        bool is_bool = false;
    };

    static std::string Lower(std::string s);
    static bool SameValue(const std::string& a, const std::string& b);
    static bool ParseBool(const std::string& s, bool def = false);
    void ParseDefaultsFile(const std::string& path);

    std::map<std::string, Entry> base_;                 // lower name -> baseline
    std::map<std::string, std::string> map_values_;     // lower name -> value from map
    std::map<std::string, std::string> current_;        // lower name -> current value
    std::set<std::string> touched_;                     // lower names edited in GUI
    std::vector<std::pair<std::string, std::string>> map_raw_;
};
