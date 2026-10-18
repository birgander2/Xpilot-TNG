#pragma once

#include <string>
#include <vector>
#include <map>
#include <SDL3/SDL.h>

struct MapInfo {
    std::string filename;
    std::string full_path;
    std::string title;
    std::string author;
    std::string description;   // optional <XPilotMap description="...">
    bool has_prio = false;     // <XPilotMap launcherprio="..."> set
    int launcher_prio = 0;
    int width = 0;
    int height = 0;
    int num_bases = 0;
    int num_cannons = 0;
    bool is_xp2 = true;
    SDL_Texture* thumbnail = nullptr;

    // All options parsed from <GeneralOptions> (.xp2) or header lines (.xp)
    std::map<std::string, std::string> map_options;
};

class MapScanner {
public:
    static std::vector<MapInfo> ScanDirectory(const std::string& directory_path, SDL_Renderer* renderer);
    static void FreeThumbnails(std::vector<MapInfo>& maps);

private:
    static bool LoadXP2Map(MapInfo& info, SDL_Renderer* renderer);
    static bool LoadXPMap(MapInfo& info, SDL_Renderer* renderer);
};
