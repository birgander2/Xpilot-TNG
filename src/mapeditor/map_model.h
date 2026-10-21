#pragma once

// In-memory model of an .xp2 map.
//
// Coordinates are kept in clicks (1 pixel = 64 clicks) exactly as in the file,
// y grows upwards. Styles are referenced by index into the style tables so
// renaming a style never needs to touch the objects that use it.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xpe {

constexpr int kClick = 64;                      // clicks per pixel (CLICK_SHIFT 6)
constexpr int kBlockPx = 35;                    // BLOCK_SZ
constexpr int kBlockClicks = kBlockPx * kClick; // 2240
constexpr int kRes = 128;                       // directions per full circle
constexpr int kMaxTeams = 10;                   // MAX_TEAMS
constexpr int kNoTeam = -1;                     // team attribute omitted

// Polystyle flags (src/common/const.h)
constexpr int kStyleFilled = 1 << 0;
constexpr int kStyleTextured = 1 << 1;
constexpr int kStyleInvisible = 1 << 2;
constexpr int kStyleInvisibleRadar = 1 << 3;

struct Pt {
    int x = 0, y = 0;
    bool operator==(const Pt&) const = default;
};

struct EdgeStyle {
    std::string id;
    int width = 2;          // -1 = invisible edge
    uint32_t color = 0x2244EE;
    int style = 0;          // 0 solid, 1 on/off dash, 2 double dash
};

struct BmpStyle {
    std::string id;
    std::string filename;
    int flags = 0;          // 1 = scalable
};

struct PolyStyle {
    std::string id;
    uint32_t color = 0x0033AA;
    int texture = -1;       // index into bmp styles
    int defedge = -1;       // index into edge styles (required by the server)
    int flags = 0;
};

// Edge style references besides real indices
constexpr int kEdgeDefault = -1;    // polystyle's default edge style
constexpr int kEdgeInternal = -2;   // builtin "internal" style: edge never drawn

struct Polygon {
    std::vector<Pt> pts;    // absolute vertices, implicitly closed
    std::vector<int> edges; // edge style of edge i (pts[i] -> pts[i+1]), see kEdge*
    int style = -1;         // polystyle index
    int destroyed_style = -1;
};

enum class Kind : int {
    // polygon containers
    Wall, Decor, BallArea, BallTarget, FrictionArea,
    // objects that may own polygons
    Cannon, Target, Wormhole,
    // point objects
    Base, Fuel, Check, Ball, Grav, ItemConc, AsteroidConc,
    Count
};

enum class GravType : int { Pos, Neg, Cwise, Acwise, Up, Down, Right, Left, Count };
enum class WormType : int { Normal, In, Out, Fixed, Count };

using OptionList = std::vector<std::pair<std::string, std::string>>;

struct Item {
    Kind kind = Kind::Wall;
    Pt pos;                     // point objects and polygon owners
    int team = kNoTeam;
    int dir = kRes / 4;         // Base, Cannon (0 = right, 32 = up)
    int order = 0;              // Base
    int style = -1;             // Ball polystyle, -1 = client default
    int type = 0;               // GravType / WormType
    double value = 0.0;         // Grav force, FrictionArea friction
    std::vector<Polygon> polys;
    OptionList options;         // Base / Cannon specific options
};

struct Map {
    // <XPilotMap> attributes other than version, in file order (description, launcherprio, ...)
    OptionList attrs;
    std::string version = "1.1";
    OptionList options;         // <GeneralOptions>, in file order, original spelling
    std::vector<EdgeStyle> edge_styles;
    std::vector<BmpStyle> bmp_styles;
    std::vector<PolyStyle> poly_styles;
    std::vector<Item> items;    // file order

    // --- options (case-insensitive) ---
    int FindOption(const std::string& name) const;
    std::string Option(const std::string& name, const std::string& def = "") const;
    void SetOption(const std::string& name, const std::string& value);
    void RemoveOption(const std::string& name);
    bool OptionBool(const std::string& name, bool def = false) const;

    std::string Attr(const std::string& name) const;
    void SetAttr(const std::string& name, const std::string& value);   // empty removes

    // Map size in pixels (mapWidth / mapHeight)
    int WidthPx() const;
    int HeightPx() const;

    int FindEdgeStyle(const std::string& id) const;
    int FindPolyStyle(const std::string& id) const;
    int FindBmpStyle(const std::string& id) const;

    // Returns the index of the named builtin style, creating it (and its edge style) if missing.
    int EnsureBuiltinPolyStyle(const std::string& id);

    // Style removal: references are redirected to `replacement` (may be -1).
    void RemoveEdgeStyle(int idx, int replacement);
    void RemovePolyStyle(int idx, int replacement);
    void RemoveBmpStyle(int idx);
    int CountEdgeStyleUse(int idx) const;
    int CountPolyStyleUse(int idx) const;
};

// --- kind metadata ----------------------------------------------------------
const char* KindName(Kind k);       // UI name ("Wall", "Ball target", ...)
const char* KindTag(Kind k);        // XML element name
bool KindHasPolys(Kind k);          // container or owner of polygons
bool KindIsContainer(Kind k);       // Wall, Decor, BallArea, BallTarget, FrictionArea
bool KindHasPos(Kind k);            // has x/y attributes
bool KindHasTeam(Kind k);
bool KindHasDir(Kind k);
const char* GravTypeName(GravType t);   // XML value
const char* WormTypeName(WormType t);

// Default polystyle id used for new polygons of that kind ("wall_ps", ...)
const char* DefaultStyleFor(Kind k);

// --- construction -----------------------------------------------------------
Map NewMap(int width_px, int height_px, const std::string& name, const std::string& author);

// Creates an object at pos with default attributes and (for cannons, targets,
// wormholes) the same default polygon the .xp converter generates.
Item MakeItem(Map& map, Kind kind, Pt pos);
// Rebuilds the default polygon of a cannon after its direction changed.
void RebuildCannonPoly(Map& map, Item& cannon);

Polygon MakePolygon(std::vector<Pt> pts, int style);

// --- geometry ---------------------------------------------------------------
struct Box {
    Pt lo{INT32_MAX, INT32_MAX}, hi{INT32_MIN, INT32_MIN};
    void Add(Pt p);
    bool Valid() const { return lo.x <= hi.x; }
};
Box ItemBounds(const Item& it);
bool PointInPolygon(const std::vector<Pt>& pts, double x, double y);
double DistToSegment(double px, double py, Pt a, Pt b, double* t = nullptr);
double SignedArea(const std::vector<Pt>& pts);
bool SelfIntersects(const std::vector<Pt>& pts);
// Ear clipping triangulation of a simple polygon (any winding). Returns vertex index triples.
std::vector<int> Triangulate(const std::vector<Pt>& pts);
void TranslateItem(Item& it, int dx, int dy);
// Maps use counter-clockwise outlines (y up); reverses p if needed, keeping edge styles.
void EnsureCCW(Polygon& p);

// --- validation -------------------------------------------------------------
struct Problem {
    bool error = false;         // false = warning
    int item = -1;              // item index or -1
    std::string text;
};
std::vector<Problem> Validate(const Map& map);

} // namespace xpe
