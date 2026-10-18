#pragma once

#include <string>
#include <utility>
#include <vector>

// Same lookup as the client (Xpilotrc_get_filename): $XPILOTRC, then
// $XDG_CONFIG_HOME/xpilot-tng/xpilotrc, then legacy ~/.xpilotrc.
std::string XpilotrcPath();

// Sets "xpilot.<key>: <value>" for every pair. An existing definition is replaced in place
// (the last one; earlier duplicates are dropped), new keys are appended. All other lines and
// comments stay untouched. The first save keeps the old file as "<xpilotrc>.bak".
bool XpilotrcWrite(const std::vector<std::pair<std::string, std::string>>& values, std::string* error);

// Value of "xpilot.<key>: ..." (last definition wins). False if the key is not defined at all;
// a defined key may have an empty value (= unbound).
bool XpilotrcLookup(const char* key, std::string* value);
