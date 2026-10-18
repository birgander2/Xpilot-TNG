#include "map_preview.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <expat.h>

namespace fs = std::filesystem;

namespace {

struct Point2D {
    double x = 0;
    double y = 0;
};

struct Polyline {
    std::string style;
    std::vector<Point2D> pts;
};

struct PolyStyleInfo {
    std::string id;
    int flags = 0;
};

struct XP2ParseContext {
    MapInfo* info = nullptr;
    std::unordered_map<std::string, PolyStyleInfo> styles;
    std::vector<Polyline> polygons;
    Polyline current_poly;
    bool in_polygon = false;
    Point2D last_poly_pt{0, 0};

    std::vector<Point2D> bases;
    std::vector<Point2D> cannons;
    std::vector<Point2D> fuels;
};

static void XMLCALL xp2_start_element(void* user_data, const XML_Char* name, const XML_Char** attrs) {
    auto* ctx = static_cast<XP2ParseContext*>(user_data);

    auto get_attr = [&](const char* key) -> const char* {
        for (int i = 0; attrs[i]; i += 2) {
            if (strcasecmp(attrs[i], key) == 0) return attrs[i + 1];
        }
        return nullptr;
    };

    if (strcasecmp(name, "XPilotMap") == 0) {
        const char* desc = get_attr("description");
        if (desc) ctx->info->description = desc;
        const char* prio = get_attr("launcherprio");
        if (prio) { ctx->info->has_prio = true; ctx->info->launcher_prio = std::atoi(prio); }
    } else if (strcasecmp(name, "Option") == 0) {
        const char* opt_name = get_attr("name");
        const char* opt_val = get_attr("value");
        if (opt_name && opt_val) {
            std::string val_str = opt_val;
            if (val_str.size() >= 2 && (val_str.front() == '\'' || val_str.front() == '"')) {
                val_str = val_str.substr(1, val_str.size() - 2);
            }
            ctx->info->map_options[opt_name] = val_str;

            if (strcasecmp(opt_name, "mapname") == 0) {
                ctx->info->title = val_str;
            } else if (strcasecmp(opt_name, "mapauthor") == 0) {
                ctx->info->author = val_str;
            } else if (strcasecmp(opt_name, "mapwidth") == 0) {
                ctx->info->width = std::atoi(val_str.c_str());
            } else if (strcasecmp(opt_name, "mapheight") == 0) {
                ctx->info->height = std::atoi(val_str.c_str());
            }
        }
    } else if (strcasecmp(name, "Polystyle") == 0) {
        const char* id = get_attr("id");
        const char* flags_str = get_attr("flags");
        if (id) {
            PolyStyleInfo psi;
            psi.id = id;
            if (flags_str) psi.flags = std::atoi(flags_str);
            ctx->styles[id] = psi;
        }
    } else if (strcasecmp(name, "Polygon") == 0) {
        ctx->in_polygon = true;
        ctx->current_poly.pts.clear();
        const char* style = get_attr("style");
        ctx->current_poly.style = style ? style : "";
        const char* x_str = get_attr("x");
        const char* y_str = get_attr("y");
        double x = x_str ? std::atof(x_str) : 0.0;
        double y = y_str ? std::atof(y_str) : 0.0;
        ctx->last_poly_pt = {x, y};
        ctx->current_poly.pts.push_back({x, y});
    } else if (strcasecmp(name, "Offset") == 0 && ctx->in_polygon) {
        const char* ox_str = get_attr("x");
        const char* oy_str = get_attr("y");
        double ox = ox_str ? std::atof(ox_str) : 0.0;
        double oy = oy_str ? std::atof(oy_str) : 0.0;
        Point2D new_pt = {ctx->last_poly_pt.x + ox, ctx->last_poly_pt.y + oy};
        ctx->current_poly.pts.push_back(new_pt);
        ctx->last_poly_pt = new_pt;
    } else if (strcasecmp(name, "Base") == 0) {
        const char* x_str = get_attr("x");
        const char* y_str = get_attr("y");
        if (x_str && y_str) {
            double x = std::atof(x_str);
            double y = std::atof(y_str);
            ctx->bases.push_back({x, y});
        }
    } else if (strcasecmp(name, "Cannon") == 0) {
        const char* x_str = get_attr("x");
        const char* y_str = get_attr("y");
        if (x_str && y_str) {
            double x = std::atof(x_str);
            double y = std::atof(y_str);
            ctx->cannons.push_back({x, y});
        }
    } else if (strcasecmp(name, "Fuel") == 0) {
        const char* x_str = get_attr("x");
        const char* y_str = get_attr("y");
        if (x_str && y_str) {
            double x = std::atof(x_str);
            double y = std::atof(y_str);
            ctx->fuels.push_back({x, y});
        }
    }
}

static void XMLCALL xp2_end_element(void* user_data, const XML_Char* name) {
    auto* ctx = static_cast<XP2ParseContext*>(user_data);
    if (strcasecmp(name, "Polygon") == 0) {
        ctx->in_polygon = false;
        if (!ctx->current_poly.pts.empty()) {
            ctx->polygons.push_back(ctx->current_poly);
        }
    }
}

static void PutPixel(SDL_Surface* surf, int x, int y, uint32_t color) {
    if (x < 0 || x >= surf->w || y < 0 || y >= surf->h) return;
    uint32_t* pixels = reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(surf->pixels) + y * surf->pitch);
    pixels[x] = color;
}

static void DrawLineBresenham(SDL_Surface* surf, int x0, int y0, int x1, int y1, uint32_t color,
                              int clip_x0 = 0, int clip_y0 = 0, int clip_x1 = 255, int clip_y1 = 255) {
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        if (x0 >= clip_x0 && x0 <= clip_x1 && y0 >= clip_y0 && y0 <= clip_y1) {
            PutPixel(surf, x0, y0, color);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void FillPolygon(SDL_Surface* surf, const std::vector<Point2D>& pts, int bx0, int by0, int bx1, int by1, uint32_t color) {
    if (pts.size() < 3) return;

    double min_y = pts[0].y;
    double max_y = pts[0].y;
    for (size_t i = 1; i < pts.size(); ++i) {
        if (pts[i].y < min_y) min_y = pts[i].y;
        if (pts[i].y > max_y) max_y = pts[i].y;
    }

    int start_y = std::max(by0, static_cast<int>(std::floor(min_y)));
    int end_y = std::min(by1, static_cast<int>(std::ceil(max_y)));
    if (start_y > end_y) return;

    size_t n = pts.size();
    std::vector<double> intersections;
    intersections.reserve(16);

    for (int y = start_y; y <= end_y; ++y) {
        double scan_y = y + 0.5;
        intersections.clear();

        for (size_t i = 0; i < n; ++i) {
            size_t next_i = (i + 1) % n;
            const auto& p0 = pts[i];
            const auto& p1 = pts[next_i];

            if ((p0.y <= scan_y && p1.y > scan_y) || (p1.y <= scan_y && p0.y > scan_y)) {
                double t = (scan_y - p0.y) / (p1.y - p0.y);
                intersections.push_back(p0.x + t * (p1.x - p0.x));
            }
        }

        std::sort(intersections.begin(), intersections.end());

        for (size_t k = 0; k + 1 < intersections.size(); k += 2) {
            int x_start = std::max(bx0, static_cast<int>(std::ceil(intersections[k])));
            int x_end = std::min(bx1, static_cast<int>(std::floor(intersections[k + 1])));
            for (int x = x_start; x <= x_end; ++x) {
                PutPixel(surf, x, y, color);
            }
        }
    }
}

static void DrawFilledCircle(SDL_Surface* surf, int cx, int cy, int radius, uint32_t color) {
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) {
                PutPixel(surf, cx + x, cy + y, color);
            }
        }
    }
}

static void FillRect(SDL_Surface* surf, int x0, int y0, int x1, int y1, uint32_t color) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            PutPixel(surf, x, y, color);
        }
    }
}

static void FillTriangle(SDL_Surface* surf, int x0, int y0, int x1, int y1, char slope_type, uint32_t color) {
    int w = x1 - x0;
    int h = y1 - y0;
    if (w <= 1 || h <= 1) {
        FillRect(surf, x0, y0, x1, y1, color);
        return;
    }
    for (int py = y0; py < y1; ++py) {
        double fy = (py - y0 + 0.5) / h;
        for (int px = x0; px < x1; ++px) {
            double fx = (px - x0 + 0.5) / w;
            bool inside = false;
            switch (slope_type) {
            case 's': // top-left (REC_LU)
                inside = (fx + fy <= 1.0);
                break;
            case 'a': // top-right (REC_RU)
                inside = (fx >= fy);
                break;
            case 'w': // bottom-left (REC_LD)
                inside = (fx <= fy);
                break;
            case 'q': // bottom-right (REC_RD)
                inside = (fx + fy >= 1.0);
                break;
            default:
                inside = true;
                break;
            }
            if (inside) {
                PutPixel(surf, px, py, color);
            }
        }
    }
}

static inline double WrapCoord(double v, double max_v) {
    if (max_v <= 0) return 0;
    double r = std::fmod(v, max_v);
    if (r < 0) r += max_v;
    return r;
}

} // anonymous namespace

bool MapScanner::LoadXP2Map(MapInfo& info, SDL_Renderer* renderer) {
    std::ifstream file(info.full_path);
    if (!file.is_open()) return false;

    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) return false;

    XP2ParseContext ctx;
    ctx.info = &info;

    XML_SetUserData(parser, &ctx);
    XML_SetElementHandler(parser, xp2_start_element, xp2_end_element);

    char buf[8192];
    bool done = false;
    while (!done) {
        file.read(buf, sizeof(buf));
        size_t len = file.gcount();
        done = file.eof();
        if (XML_Parse(parser, buf, static_cast<int>(len), done) == XML_STATUS_ERROR) {
            XML_ParserFree(parser);
            return false;
        }
    }
    XML_ParserFree(parser);

    info.num_bases = static_cast<int>(ctx.bases.size());
    info.num_cannons = static_cast<int>(ctx.cannons.size());

    if (info.title.empty()) {
        info.title = info.filename;
    }
    if (info.author.empty()) {
        info.author = "Unknown";
    }

    if (info.width <= 0) info.width = 3500;
    if (info.height <= 0) info.height = 3500;

    double world_w = info.width * 64.0;
    double world_h = info.height * 64.0;

    // Create 256x256 Surface
    const int THUMB_SIZE = 256;
    SDL_Surface* surf = SDL_CreateSurface(THUMB_SIZE, THUMB_SIZE, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return false;

    SDL_ClearSurface(surf, 0.05f, 0.07f, 0.10f, 1.0f);

    const int margin = 10;
    const double draw_w = THUMB_SIZE - 2 * margin;
    const double draw_h = THUMB_SIZE - 2 * margin;

    // Uniform aspect ratio scaling
    double scale = std::min(draw_w / world_w, draw_h / world_h);
    double ox = margin + (draw_w - world_w * scale) * 0.5;
    double oy = margin + (draw_h - world_h * scale) * 0.5;

    auto MapToThumb = [&](double x, double y) -> std::pair<int, int> {
        int tx = static_cast<int>(std::round(ox + x * scale));
        int ty = static_cast<int>(std::round(THUMB_SIZE - (oy + y * scale)));
        return {tx, ty};
    };

    // Draw world border outline
    auto [bx0, by1] = MapToThumb(0, 0);
    auto [bx1, by0] = MapToThumb(world_w, world_h);
    for (int x = bx0; x <= bx1; x += 2) {
        PutPixel(surf, x, by0, 0xFF35445A);
        PutPixel(surf, x, by1, 0xFF35445A);
    }
    for (int y = by0; y <= by1; y += 2) {
        PutPixel(surf, bx0, y, 0xFF35445A);
        PutPixel(surf, bx1, y, 0xFF35445A);
    }

    // Draw polygons (filled cyan/blue with crisp edges)
    uint32_t poly_color = 0xFF4A90E2;
    for (const auto& poly : ctx.polygons) {
        if (poly.pts.size() < 2) continue;

        // Skip invisible polygon styles
        if (poly.style == "xx" || poly.style == "invisible") continue;
        auto it = ctx.styles.find(poly.style);
        if (it != ctx.styles.end()) {
            if (it->second.flags & (4 | 8)) { // STYLE_INVISIBLE or STYLE_INVISIBLE_RADAR
                continue;
            }
        }

        // Bounding box in raw click space
        double min_x = poly.pts[0].x, max_x = poly.pts[0].x;
        double min_y = poly.pts[0].y, max_y = poly.pts[0].y;
        for (const auto& pt : poly.pts) {
            if (pt.x < min_x) min_x = pt.x;
            if (pt.x > max_x) max_x = pt.x;
            if (pt.y < min_y) min_y = pt.y;
            if (pt.y > max_y) max_y = pt.y;
        }

        // Toroidal replicas: which offsets bring any part into [0, world_w] x [0, world_h]?
        int min_xoff = static_cast<int>(std::floor((0.0 - max_x) / world_w));
        int max_xoff = static_cast<int>(std::floor((world_w - min_x) / world_w));
        int min_yoff = static_cast<int>(std::floor((0.0 - max_y) / world_h));
        int max_yoff = static_cast<int>(std::floor((world_h - min_y) / world_h));

        for (int xoff = min_xoff; xoff <= max_xoff; ++xoff) {
            for (int yoff = min_yoff; yoff <= max_yoff; ++yoff) {
                double r_min_x = min_x + xoff * world_w;
                double r_max_x = max_x + xoff * world_w;
                double r_min_y = min_y + yoff * world_h;
                double r_max_y = max_y + yoff * world_h;

                if (r_max_x < 0.0 || r_min_x > world_w || r_max_y < 0.0 || r_min_y > world_h) {
                    continue;
                }

                std::vector<Point2D> screen_pts;
                screen_pts.reserve(poly.pts.size());
                for (const auto& p : poly.pts) {
                    auto [sx, sy] = MapToThumb(p.x + xoff * world_w, p.y + yoff * world_h);
                    screen_pts.push_back({static_cast<double>(sx), static_cast<double>(sy)});
                }

                FillPolygon(surf, screen_pts, bx0, by0, bx1, by1, poly_color);

                size_t n = screen_pts.size();
                for (size_t i = 0; i < n; ++i) {
                    size_t next_i = (i + 1) % n;
                    int x0 = static_cast<int>(screen_pts[i].x);
                    int y0 = static_cast<int>(screen_pts[i].y);
                    int x1 = static_cast<int>(screen_pts[next_i].x);
                    int y1 = static_cast<int>(screen_pts[next_i].y);
                    DrawLineBresenham(surf, x0, y0, x1, y1, poly_color, bx0, by0, bx1, by1);
                }
            }
        }
    }

    // Draw fuel stations (yellow)
    uint32_t fuel_color = 0xFFF5A623;
    for (const auto& f : ctx.fuels) {
        auto [tx, ty] = MapToThumb(WrapCoord(f.x, world_w), WrapCoord(f.y, world_h));
        DrawFilledCircle(surf, tx, ty, 1, fuel_color);
    }

    // Draw cannons (red)
    uint32_t cannon_color = 0xFFE04040;
    for (const auto& c : ctx.cannons) {
        auto [tx, ty] = MapToThumb(WrapCoord(c.x, world_w), WrapCoord(c.y, world_h));
        DrawFilledCircle(surf, tx, ty, 2, cannon_color);
    }

    // Draw bases (green)
    uint32_t base_color = 0xFF2ECC71;
    for (const auto& b : ctx.bases) {
        auto [tx, ty] = MapToThumb(WrapCoord(b.x, world_w), WrapCoord(b.y, world_h));
        DrawFilledCircle(surf, tx, ty, 3, base_color);
    }

    info.thumbnail = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    return info.thumbnail != nullptr;
}

bool MapScanner::LoadXPMap(MapInfo& info, SDL_Renderer* renderer) {
    std::ifstream file(info.full_path);
    if (!file.is_open()) return false;

    std::string line;
    bool in_map_data = false;
    std::vector<std::string> grid;

    while (std::getline(file, line)) {
        if (!in_map_data) {
            if (line.find("mapData:") != std::string::npos) {
                in_map_data = true;
                continue;
            }
            auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#') continue;
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                val.erase(0, val.find_first_not_of(" \t"));
                val.erase(val.find_last_not_of(" \t\r\n") + 1);

                if (!key.empty()) {
                    info.map_options[key] = val;
                }

                if (strcasecmp(key.c_str(), "mapname") == 0) {
                    info.title = val;
                } else if (strcasecmp(key.c_str(), "mapauthor") == 0) {
                    info.author = val;
                } else if (strcasecmp(key.c_str(), "mapwidth") == 0) {
                    info.width = std::atoi(val.c_str());
                } else if (strcasecmp(key.c_str(), "mapheight") == 0) {
                    info.height = std::atoi(val.c_str());
                }
            }
        } else {
            if (line.find("EndOfMapdata") != std::string::npos) {
                break;
            }
            grid.push_back(line);
        }
    }

    if (info.title.empty()) info.title = info.filename;
    if (info.author.empty()) info.author = "Classic XPilot";

    const int THUMB_SIZE = 256;
    SDL_Surface* surf = SDL_CreateSurface(THUMB_SIZE, THUMB_SIZE, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return false;

    SDL_ClearSurface(surf, 0.05f, 0.07f, 0.10f, 1.0f);

    if (!grid.empty()) {
        size_t rows = grid.size();
        size_t cols = 0;
        for (const auto& r : grid) cols = std::max(cols, r.size());

        if (info.width <= 0) info.width = static_cast<int>(cols);
        if (info.height <= 0) info.height = static_cast<int>(rows);

        const int margin = 8;
        double avail_w = THUMB_SIZE - 2 * margin;
        double avail_h = THUMB_SIZE - 2 * margin;
        double cell_size = (cols > 0 && rows > 0)
            ? std::min(avail_w / cols, avail_h / rows)
            : 1.0;
        double ox = margin + (avail_w - cols * cell_size) * 0.5;
        double oy = margin + (avail_h - rows * cell_size) * 0.5;

        // Border outline
        int bx0 = static_cast<int>(ox);
        int by0 = static_cast<int>(oy);
        int bx1 = static_cast<int>(ox + cols * cell_size);
        int by1 = static_cast<int>(oy + rows * cell_size);

        for (int x = bx0; x <= bx1; x += 2) {
            PutPixel(surf, x, by0, 0xFF35445A);
            PutPixel(surf, x, by1, 0xFF35445A);
        }
        for (int y = by0; y <= by1; y += 2) {
            PutPixel(surf, bx0, y, 0xFF35445A);
            PutPixel(surf, bx1, y, 0xFF35445A);
        }

        info.num_bases = 0;
        info.num_cannons = 0;

        uint32_t wall_color = 0xFF4A90E2;    // Classic blue
        uint32_t decor_color = 0xFF3568A0;   // Darker blue
        uint32_t fuel_color = 0xFFF5A623;    // Yellow
        uint32_t base_color = 0xFF2ECC71;    // Green
        uint32_t cannon_color = 0xFFE04040;  // Red
        uint32_t wormhole_color = 0xFF9B59B6;// Purple
        uint32_t misc_color = 0xFF788CA0;    // Checkpoint / item / gray-blue

        for (size_t y = 0; y < rows; ++y) {
            for (size_t x = 0; x < grid[y].size(); ++x) {
                char ch = grid[y][x];
                if (ch == ' ' || ch == '.') continue;

                int x0 = static_cast<int>(ox + x * cell_size);
                int y0 = static_cast<int>(oy + y * cell_size);
                int x1 = std::max(x0 + 1, static_cast<int>(ox + (x + 1) * cell_size));
                int y1 = std::max(y0 + 1, static_cast<int>(oy + (y + 1) * cell_size));

                if (ch == 'x') {
                    FillRect(surf, x0, y0, x1, y1, wall_color);
                } else if (ch == 's' || ch == 'a' || ch == 'w' || ch == 'q') {
                    FillTriangle(surf, x0, y0, x1, y1, ch, wall_color);
                } else if (ch == 'b') {
                    FillRect(surf, x0, y0, x1, y1, decor_color);
                } else if (ch == 'h') {
                    FillTriangle(surf, x0, y0, x1, y1, 's', decor_color);
                } else if (ch == 'g') {
                    FillTriangle(surf, x0, y0, x1, y1, 'a', decor_color);
                } else if (ch == 'y') {
                    FillTriangle(surf, x0, y0, x1, y1, 'w', decor_color);
                } else if (ch == 't') {
                    FillTriangle(surf, x0, y0, x1, y1, 'q', decor_color);
                } else if (ch == '#') {
                    FillRect(surf, x0, y0, x1, y1, fuel_color);
                } else if ((ch >= '0' && ch <= '9') || ch == '_' || ch == 'B') {
                    FillRect(surf, x0, y0, x1, y1, base_color);
                    info.num_bases++;
                } else if (ch == 'r' || ch == 'd' || ch == 'f' || ch == 'c' || ch == '+' || ch == 'C') {
                    FillRect(surf, x0, y0, x1, y1, cannon_color);
                    info.num_cannons++;
                } else if (ch == '@' || ch == '(' || ch == ')') {
                    FillRect(surf, x0, y0, x1, y1, wormhole_color);
                } else {
                    FillRect(surf, x0, y0, x1, y1, misc_color);
                }
            }
        }
    }

    info.thumbnail = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    return info.thumbnail != nullptr;
}

std::vector<MapInfo> MapScanner::ScanDirectory(const std::string& directory_path, SDL_Renderer* renderer) {
    std::vector<MapInfo> maps;
    if (!fs::exists(directory_path)) {
        return maps;
    }

    for (const auto& entry : fs::directory_iterator(directory_path)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".xp2" || ext == ".xp") {
            MapInfo info;
            info.filename = entry.path().filename().string();
            info.full_path = entry.path().string();
            info.is_xp2 = (ext == ".xp2");

            bool ok = false;
            if (info.is_xp2) {
                ok = LoadXP2Map(info, renderer);
            } else {
                ok = LoadXPMap(info, renderer);
            }

            if (ok) {
                maps.push_back(info);
            }
        }
    }

    std::sort(maps.begin(), maps.end(), [](const MapInfo& a, const MapInfo& b) {
        if (a.has_prio != b.has_prio) return a.has_prio;
        if (a.has_prio && a.launcher_prio != b.launcher_prio) return a.launcher_prio > b.launcher_prio;
        return a.filename < b.filename;
    });

    return maps;
}

void MapScanner::FreeThumbnails(std::vector<MapInfo>& maps) {
    for (auto& m : maps) {
        if (m.thumbnail) {
            SDL_DestroyTexture(m.thumbnail);
            m.thumbnail = nullptr;
        }
    }
}
