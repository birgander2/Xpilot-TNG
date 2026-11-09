#pragma once
// Ship shape model for the XPilot TNG ship editor.
//
// Parsing, defaults and validation follow src/common/shipshape.c (shape2wire)
// exactly, so a shape that is valid here is accepted by client and server.
// Coordinates are pixels, x points forward (nose), y points to the ship's left.

#include <array>
#include <string>
#include <vector>

namespace ss {

// Limits from shipshape.h / shape2wire()
constexpr int kMinPts = 3;
constexpr int kMaxPts = 24;
constexpr int kLimit = 15;         // every coordinate within -15..15
constexpr int kMinSizeSum = 38;    // width + height >= 22 + 16
constexpr int kMinWidth = 12;      // width across the longest axis

// Hit boxes on the server (src/common/const.h, src/server/collision.c)
constexpr int kShipSz = 16;                 // SHIP_SZ: object vs. ship hit radius
constexpr int kShipShipRange = 2 * kShipSz - 6;   // centre distance for ship-ship crashes
constexpr int kRes = 128;                   // RES: number of ship directions
constexpr int kClick = 64;                  // CLICK: sub-pixel resolution of the server

struct IPt {
    int x = 0, y = 0;
    bool operator==(const IPt& o) const { return x == o.x && y == o.y; }
    bool operator!=(const IPt& o) const { return !(*this == o); }
};

enum Part {
    kHull = 0, kEngine, kMainGun, kLeftGun, kRightGun, kLeftRearGun, kRightRearGun,
    kLeftLight, kRightLight, kMissileRack, kPartCount
};

struct PartInfo {
    const char* key;       // long keyword, e.g. "shape"
    const char* abbrev;    // abbreviation, e.g. "SH"
    const char* label;     // UI label
    int max;               // maximum number of points
    int mirror;            // part on the other side (-1 if none)
};
const PartInfo& Info(int part);

struct ShipShape {
    std::string name, author;
    std::array<std::vector<IPt>, kPartCount> pts;   // empty part = game default

    std::vector<IPt>& hull() { return pts[kHull]; }
    const std::vector<IPt>& hull() const { return pts[kHull]; }
    bool operator==(const ShipShape& o) const { return name == o.name && author == o.author && pts == o.pts; }
};

// Parse a shape definition. Anything before the first '(' is ignored, so whole
// xpilotrc / shipshapes.cfg lines work. Returns false if the text holds no
// shape at all (or is in the obsolete 3.1 format). Notes about ignored data
// (duplicates, too many points, unknown keywords) are appended to *notes.
bool Parse(const std::string& text, ShipShape* out, std::vector<std::string>* notes = nullptr);

// "(NM:..)(AU:..)(SH: ..)(EN: ..)..." - empty parts are left out (game defaults)
std::string Serialize(const ShipShape& s, bool with_name = true);

// The ship as the game resolves it: unset main gun, lights, engine and
// missile rack get their default positions. Hull duplicates are removed.
ShipShape Resolve(const ShipShape& s);

enum class Severity { Error, Warning, Info };
struct Issue {
    Severity sev;
    std::string text;
};

struct Validation {
    bool valid = false;                 // accepted by the game
    std::vector<Issue> issues;
    ShipShape resolved;                 // with defaults filled in
    // size requirements
    int low = 0, hi = 0, left = 0, right = 0, count = 0;
    int size_sum = 0;                   // leftmost - rightmost + highest - lowest
    double width = 0;                   // width perpendicular to the longest axis
    bool grid_ok = false;               // grid below is valid
    // 0 = inside, 1 = outline, 2 = outside; index [x + 15][y + 15]
    unsigned char grid[31][31] = {};
    int shield_radius = 0;              // as computed by the client (a diameter)
    double max_radius = 0;              // farthest hull point from the centre
    double area = 0;                    // grid cells inside or on the outline
    bool self_intersecting = false;
};
Validation Validate(const ShipShape& s);

// True if grid point p is outside the ship (special points must not be)
inline bool Outside(const Validation& v, IPt p) {
    if (p.x < -kLimit || p.x > kLimit || p.y < -kLimit || p.y > kLimit) return true;
    return v.grid[p.x + 15][p.y + 15] == 2;
}

// Point rotated to direction dir (0..127) with the server's click rounding,
// result in pixels.
struct FPt { double x, y; };
FPt Rotate(IPt p, int dir);

// Name may not contain characters that break the format or xpilotrc parsing
bool NameUsable(const std::string& name, std::string* why);

// Geometry helpers for editing
void FlipFrontBack(ShipShape& s);   // x -> -x
void FlipLeftRight(ShipShape& s);   // y -> -y, left and right parts swapped
void Rotate90(ShipShape& s, bool ccw);
void Shift(ShipShape& s, int dx, int dy);
bool MakeSymmetric(ShipShape& s, bool keep_left);   // mirror one half of the hull

} // namespace ss
