#pragma once

#include <string>
#include <vector>
#include "map_model.h"

namespace xpe {

struct LoadResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;  // things that were dropped or look broken
    bool was_xpd = false;               // .xpd bundle: only the XML part was read
};

// Reads .xp2 files (plain or gzip compressed) and the map part of .xpd bundles.
LoadResult LoadMap(const std::string& path, Map& out);

std::string SerializeMap(const Map& map);

// Writes the map atomically (temp file + rename).
bool SaveMap(const std::string& path, const Map& map, std::string* error);

} // namespace xpe
