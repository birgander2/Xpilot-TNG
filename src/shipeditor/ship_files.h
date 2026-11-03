#pragma once
// Ship collections (shipshapes.txt style files); the xpilotrc is handled by
// src/launcher/xpilotrc.cpp, which this code is built together with.

#include <string>
#include <utility>
#include <vector>

#include "ship_shape.h"

namespace ss {

struct Entry {
    ShipShape shape;
    bool valid = false;
};

// One ship per line, everything before the first '(' is ignored
// (same as the client's shipShapeFile reader).
bool LoadShipFile(const std::string& path, std::vector<Entry>* out, std::string* error);
// Writes "xpilot.shipShape: <definition>" lines, usable as shipShapeFile.
bool SaveShipFile(const std::string& path, const std::vector<Entry>& entries, std::string* error);

// Directory for the personal collection (next to the xpilotrc):
// $XDG_CONFIG_HOME/xpilot-tng, ~/.config/xpilot-tng, Windows: %APPDATA%/xpilot-tng
std::string ConfigDir();
// The personal collection file (ConfigDir()/shipshapes.txt)
std::string CollectionPath();

// Value of "xpilot.<key>:" (last one wins); false if not defined
bool XpilotrcGet(const std::string& key, std::string* value);
// Replace or append "xpilot.<key>: <value>"; other lines stay untouched.
// The first write keeps a copy as <xpilotrc>.bak.
bool XpilotrcSet(const std::string& key, const std::string& value, std::string* error);

// Directory holding the game data (lib/), empty if not found
std::string FindDataDir(const std::string& app_dir);

} // namespace ss
