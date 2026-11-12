#include "ship_shape.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace ss {

namespace {

const PartInfo kParts[kPartCount] = {
    {"shape",        "SH", "Hull",            kMaxPts, -1},
    {"engine",       "EN", "Engine",          1,       -1},
    {"mainGun",      "MG", "Main gun",        1,       -1},
    {"leftGun",      "LG", "Left gun",        3,       kRightGun},
    {"rightGun",     "RG", "Right gun",       3,       kLeftGun},
    {"leftRearGun",  "LR", "Left rear gun",   3,       kRightRearGun},
    {"rightRearGun", "RR", "Right rear gun",  3,       kLeftRearGun},
    {"leftLight",    "LL", "Left light",      3,       kRightLight},
    {"rightLight",   "RL", "Right light",     3,       kLeftLight},
    {"missileRack",  "MR", "Missile rack",    4,       -1},
};

// Keyword index as in Get_shape_keyword(): 0..kPartCount-1 parts, 100 name, 101 author, -1 unknown
int Keyword(const std::string& k) {
    if (k.empty()) return -1;
    const bool lower = std::islower((unsigned char)k[0]), upper = std::isupper((unsigned char)k[0]);
    for (int i = 0; i < kPartCount; ++i) {
        if (lower && k == std::string(kParts[i].key) + ":") return i;
        if (upper && k == std::string(kParts[i].abbrev) + ":") return i;
    }
    if ((lower && k == "name:") || (upper && k == "NM:")) return 100;
    if ((lower && k == "author:") || (upper && k == "AU:")) return 101;
    return -1;
}

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

void Note(std::vector<std::string>* notes, const std::string& t) { if (notes) notes->push_back(t); }

std::string P(IPt p) { return "(" + std::to_string(p.x) + "," + std::to_string(p.y) + ")"; }

// segment intersection (proper, excluding shared endpoints)
bool SegCross(IPt a, IPt b, IPt c, IPt d) {
    auto orient = [](IPt p, IPt q, IPt r) {
        long v = long(q.x - p.x) * (r.y - p.y) - long(q.y - p.y) * (r.x - p.x);
        return (v > 0) - (v < 0);
    };
    int o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (o1 != o2 && o3 != o4 && o1 && o2 && o3 && o4) return true;
    return false;
}

} // namespace

const PartInfo& Info(int part) { return kParts[part]; }

bool Parse(const std::string& text, ShipShape* out, std::vector<std::string>* notes) {
    ShipShape s;
    bool any = false, version_checked = false;
    bool main_gun_set = false, engine_set = false, removed_edge = false;
    size_t pos = 0;
    while ((pos = text.find('(', pos)) != std::string::npos) {
        ++pos;
        if (!version_checked) {
            version_checked = true;
            if (pos < text.size() && std::isdigit((unsigned char)text[pos])) {
                Note(notes, "Shape is in the obsolete 3.1 format.");
                return false;
            }
        }
        // keyword up to and including ':' (max 19 chars, as in shape2wire)
        size_t colon = text.find(':', pos);
        if (colon == std::string::npos || colon - pos > 18) {
            Note(notes, "Missing colon in \"" + text.substr(pos, 12) + "\"");
            continue;
        }
        std::string key = text.substr(pos, colon - pos + 1);
        size_t end = text.find(')', colon);
        std::string body = text.substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1);
        pos = end == std::string::npos ? text.size() : end + 1;

        int kw = Keyword(key);
        if (kw == 100) { s.name = Trim(body); continue; }
        if (kw == 101) { s.author = Trim(body); continue; }
        if (kw < 0) { Note(notes, "Unknown keyword \"" + key + "\" ignored."); continue; }
        any = true;

        // points: "x,y" tokens separated by spaces; stop at the first unparsable token
        std::vector<IPt> found;
        const char* t = body.c_str();
        while (t) {
            while (*t == ' ') ++t;
            IPt in;
            if (std::sscanf(t, "%d,%d", &in.x, &in.y) != 2) break;
            found.push_back(in);
            if (kw == kEngine || kw == kMainGun) break;
            t = std::strchr(t, ' ');
        }
        auto& dst = s.pts[kw];
        if (kw == kEngine || kw == kMainGun) {
            bool& set = kw == kEngine ? engine_set : main_gun_set;
            if (set) { Note(notes, std::string(kParts[kw].label) + " defined more than once, extra ignored."); continue; }
            if (found.empty()) { Note(notes, std::string("Missing ") + kParts[kw].label + " coordinate."); continue; }
            dst.push_back(found[0]);
            set = true;
            continue;
        }
        for (IPt p : found) {
            if ((int)dst.size() >= kParts[kw].max) {
                Note(notes, std::string("Too many ") + kParts[kw].label + " points, extra ignored.");
                break;
            }
            if (kw == kHull && !dst.empty() && dst.back() == p) {
                removed_edge = true;
                Note(notes, "Duplicate hull point " + P(p) + " ignored.");
                continue;
            }
            dst.push_back(p);
        }
    }
    auto& h = s.pts[kHull];
    if (h.size() > 1 && h.back() == h.front()) {
        Note(notes, "Last hull point equals the first one, ignored.");
        h.pop_back();
        removed_edge = true;
    }
    (void)removed_edge;
    if (!any && s.name.empty()) return false;
    *out = s;
    return true;
}

std::string Serialize(const ShipShape& s, bool with_name) {
    std::string r;
    if (with_name) {
        if (!s.name.empty()) r += "(NM:" + s.name + ")";
        if (!s.author.empty()) r += "(AU:" + s.author + ")";
    }
    for (int p = 0; p < kPartCount; ++p) {
        if (s.pts[p].empty()) continue;
        r += std::string("(") + kParts[p].abbrev + ":";
        for (IPt q : s.pts[p]) r += " " + std::to_string(q.x) + "," + std::to_string(q.y);
        r += ")";
    }
    return r;
}

ShipShape Resolve(const ShipShape& in) {
    ShipShape s = in;
    const auto& pt = s.pts[kHull];
    const int n = (int)pt.size();
    if (n < 1) return s;
    if (s.pts[kMainGun].empty()) {   // foremost point
        int max = 0;
        for (int i = 1; i < n; i++)
            if (pt[i].x > pt[max].x || (pt[i].x == pt[max].x && std::abs(pt[i].y) < std::abs(pt[max].y))) max = i;
        s.pts[kMainGun].push_back(pt[max]);
    }
    if (s.pts[kLeftLight].empty()) {   // leftmost point
        int max = 0;
        for (int i = 1; i < n; i++)
            if (pt[i].y > pt[max].y || (pt[i].y == pt[max].y && pt[i].x <= pt[max].x)) max = i;
        s.pts[kLeftLight].push_back(pt[max]);
    }
    if (s.pts[kRightLight].empty()) {   // rightmost point
        int max = 0;
        for (int i = 1; i < n; i++)
            if (pt[i].y < pt[max].y || (pt[i].y == pt[max].y && pt[i].x <= pt[max].x)) max = i;
        s.pts[kRightLight].push_back(pt[max]);
    }
    if (s.pts[kEngine].empty()) {   // rear of the ship, on the axis
        int max = 0;
        for (int i = 1; i < n; i++)
            if (pt[i].x < pt[max].x) max = i;
        s.pts[kEngine].push_back({pt[max].x, 0});
    }
    if (s.pts[kMissileRack].empty()) s.pts[kMissileRack].push_back(s.pts[kMainGun][0]);
    return s;
}

Validation Validate(const ShipShape& shape) {
    Validation v;
    auto err = [&](const std::string& t) { v.issues.push_back({Severity::Error, t}); };
    auto warn = [&](const std::string& t) { v.issues.push_back({Severity::Warning, t}); };
    auto info = [&](const std::string& t) { v.issues.push_back({Severity::Info, t}); };

    // the game drops consecutive duplicates and a closing duplicate
    ShipShape s = shape;
    {
        auto& h = s.pts[kHull];
        std::vector<IPt> clean;
        for (IPt p : h)
            if (clean.empty() || clean.back() != p) clean.push_back(p);
        if (clean.size() > 1 && clean.back() == clean.front()) clean.pop_back();
        if (clean.size() != h.size()) warn("Duplicate neighbouring hull points are removed by the game.");
        h = clean;
    }
    const auto& pt = s.pts[kHull];
    const int n = (int)pt.size();

    std::string why;
    if (!NameUsable(shape.name, &why)) err(why);
    if (shape.name.empty()) warn("No name: needed to store the ship in a collection.");
    if (shape.author.find_first_of(");") != std::string::npos) err("Author must not contain ) or ;");

    if (n < kMinPts) {
        err("A ship needs at least 3 hull points (has " + std::to_string(n) + ").");
        v.resolved = s;
        return v;
    }
    if ((int)shape.hull().size() > kMaxPts) err("At most 24 hull points.");
    v.resolved = Resolve(s);

    // ---- size limits (shape2wire, shapeLimits) ----
    const int isLow = -8, isHi = 8, isLeft = 8, isRight = -8;
    int lowest = 0, highest = 0, leftmost = 0, rightmost = 0, outside_range = 0;
    for (IPt p : pt) {
        int change = 0;
        if (p.y >= isLeft) { change++; v.left++; leftmost = std::max(leftmost, p.y); }
        if (p.y <= isRight) { change++; v.right++; rightmost = std::min(rightmost, p.y); }
        if (p.x <= isLow) { change++; v.low++; lowest = std::min(lowest, p.x); }
        if (p.x >= isHi) { change++; v.hi++; highest = std::max(highest, p.x); }
        if (change) v.count++;
        if (p.y > kLimit || p.y < -kLimit) outside_range++;
        if (p.x > kLimit || p.x < -kLimit) outside_range++;
    }
    if (v.hi < 1) err("Needs a hull point at the front (x >= 8).");
    if (v.low < 1) err("Needs a hull point at the rear (x <= -8).");
    if (v.left < 1) err("Needs a hull point on the left (y >= 8).");
    if (v.right < 1) err("Needs a hull point on the right (y <= -8).");
    if (v.count < 3) err("At least 3 hull points must lie in the outer zones (|x| >= 8 or |y| >= 8).");
    if (outside_range) err("Coordinates must be within -15..15.");
    v.size_sum = leftmost - rightmost + highest - lowest;
    if (v.size_sum < kMinSizeSum)
        err("Too small: width + length must be at least 38 (is " + std::to_string(v.size_sum) + ").");

    // width perpendicular to the longest axis
    {
        int pair0 = 0, pair1 = 0, dist = 0;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                int d = (pt[i].x - pt[j].x) * (pt[i].x - pt[j].x) + (pt[i].y - pt[j].y) * (pt[i].y - pt[j].y);
                if (d > dist) { dist = d; pair0 = i; pair1 = j; }
            }
        double vx = pt[pair1].y - pt[pair0].y, vy = pt[pair0].x - pt[pair1].x;
        double len = std::sqrt(vx * vx + vy * vy);
        double w = 0;
        if (len > 0) {
            vx /= len; vy /= len;
            for (int i = 0; i < n; i++)
                for (int j = i + 1; j < n; j++)
                    w = std::max(w, std::fabs(vx * (pt[i].x - pt[j].x) + vy * (pt[i].y - pt[j].y)));
        }
        v.width = w;
        if (int(w) < kMinWidth)
            err("Too thin: width across the longest axis must be at least 12 (is " + std::to_string(int(w)) + ").");
    }

    // ---- inside/outside grid ----
    if (!outside_range) {
        auto get = [&](int x, int y) -> int {
            if (x < -15 || x > 15 || y < -15 || y > 15) return 2;
            return v.grid[x + 15][y + 15];
        };
        auto set = [&](int x, int y, int val) { v.grid[x + 15][y + 15] = (unsigned char)val; };
        for (int i = 0; i < n; i++) {
            int j = (i + 1 == n) ? 0 : i + 1;
            set(pt[i].x, pt[i].y, 1);
            int dx = pt[j].x - pt[i].x, dy = pt[j].y - pt[i].y;
            if (std::abs(dx) >= std::abs(dy)) {
                if (dx > 0) for (int x = pt[i].x + 1; x < pt[j].x; x++) set(x, pt[i].y + (dy * (x - pt[i].x)) / dx, 1);
                else for (int x = pt[j].x + 1; x < pt[i].x; x++) set(x, pt[j].y + (dy * (x - pt[j].x)) / dx, 1);
            } else {
                if (dy > 0) for (int y = pt[i].y + 1; y < pt[j].y; y++) set(pt[i].x + (dx * (y - pt[i].y)) / dy, y, 1);
                else for (int y = pt[j].y + 1; y < pt[i].y; y++) set(pt[j].x + (dx * (y - pt[j].y)) / dy, y, 1);
            }
        }
        std::vector<IPt> todo;
        auto add = [&](int x, int y) { set(x, y, 2); todo.push_back({x, y}); };
        for (int y = -15; y <= 15; y++)
            for (int x = -15; x <= 15; x += (y == -15 || y == 15) ? 1 : 30)
                if (get(x, y) == 0) add(x, y);
        for (size_t k = 0; k < todo.size(); k++) {
            int x = todo[k].x, y = todo[k].y;
            if (x < 15 && get(x + 1, y) == 0) add(x + 1, y);
            if (x > -15 && get(x - 1, y) == 0) add(x - 1, y);
            if (y < 15 && get(x, y + 1) == 0) add(x, y + 1);
            if (y > -15 && get(x, y - 1) == 0) add(x, y - 1);
        }
        v.grid_ok = true;
        int cells = 0;
        for (auto& col : v.grid) for (unsigned char c : col) if (c != 2) cells++;
        v.area = cells;

        for (int p = kEngine; p < kPartCount; ++p) {
            const auto& list = v.resolved.pts[p];
            for (size_t i = 0; i < list.size(); ++i)
                if (Outside(v, list[i])) {
                    std::string what = kParts[p].label;
                    if (s.pts[p].empty()) what += " (default)";
                    err(what + " at " + P(list[i]) + " is outside the ship.");
                }
        }
    }

    // ---- information ----
    int max_r2 = 0;
    for (IPt p : pt) max_r2 = std::max(max_r2, p.x * p.x + p.y * p.y);
    v.max_radius = std::sqrt((double)max_r2);
    int maxr = int(2.0 * std::sqrt((double)max_r2));
    v.shield_radius = (maxr + 2 <= 34) ? 34 : (maxr + 2 - (maxr & 1));

    for (int i = 0; i < n && !v.self_intersecting; i++)
        for (int j = i + 2; j < n; j++) {
            if (i == 0 && j == n - 1) continue;
            if (SegCross(pt[i], pt[(i + 1) % n], pt[j], pt[(j + 1) % n])) { v.self_intersecting = true; break; }
        }
    if (v.self_intersecting) info("Hull edges cross each other (allowed, every edge still collides with walls).");
    if (s.pts[kEngine].empty()) info("Engine not set: placed at the rearmost x on the axis.");

    bool has_error = false;
    for (auto& i : v.issues) if (i.sev == Severity::Error) has_error = true;
    v.valid = !has_error;
    return v;
}

FPt Rotate(IPt p, int dir) {
    dir = ((dir % kRes) + kRes) % kRes;
    const double a = dir * (2.0 * M_PI / kRes);
    const double c = std::cos(a), s = std::sin(a);
    const double px = p.x * kClick, py = p.y * kClick;   // PIXEL_TO_CLICK
    double cx = c * px - s * py, cy = s * px + c * py;
    int ix = (int)(cx >= 0.0 ? cx + 0.5 : cx - 0.5);
    int iy = (int)(cy >= 0.0 ? cy + 0.5 : cy - 0.5);
    return {ix / double(kClick), iy / double(kClick)};
}

bool NameUsable(const std::string& name, std::string* why) {
    // ')' ends the field, ';' starts a comment in the xpilotrc
    if (name.find_first_of(");") != std::string::npos) {
        if (why) *why = "Name must not contain ) or ;";
        return false;
    }
    if (!name.empty() && (name.front() == ' ' || name.back() == ' ')) {
        if (why) *why = "Name must not start or end with a space.";
        return false;
    }
    return true;
}

void FlipFrontBack(ShipShape& s) {
    for (auto& part : s.pts) for (auto& p : part) p.x = -p.x;
}

void FlipLeftRight(ShipShape& s) {
    for (auto& part : s.pts) for (auto& p : part) p.y = -p.y;
    std::swap(s.pts[kLeftGun], s.pts[kRightGun]);
    std::swap(s.pts[kLeftRearGun], s.pts[kRightRearGun]);
    std::swap(s.pts[kLeftLight], s.pts[kRightLight]);
}

void Rotate90(ShipShape& s, bool ccw) {
    for (auto& part : s.pts)
        for (auto& p : part) p = ccw ? IPt{-p.y, p.x} : IPt{p.y, -p.x};
}

bool MakeSymmetric(ShipShape& s, bool keep_left) {
    auto& h = s.pts[kHull];
    const int n = (int)h.size();
    if (n < 2) return false;
    auto on_side = [&](IPt p) { return keep_left ? p.y >= 0 : p.y <= 0; };
    // start the walk at a point on the kept side right after a point on the other side
    int start = -1;
    for (int i = 0; i < n; i++)
        if (on_side(h[i]) && !on_side(h[(i + n - 1) % n])) { start = i; break; }
    if (start < 0) start = 0;
    std::vector<IPt> half;
    for (int k = 0; k < n; k++) {
        IPt p = h[(start + k) % n];
        if (on_side(p)) half.push_back(p);
        else if (!half.empty()) break;
    }
    if (half.size() < 2) return false;
    // order the chain front to back, so the mirror closes the outline
    if (half.front().x < half.back().x) std::reverse(half.begin(), half.end());
    std::vector<IPt> out = half;
    for (int k = (int)half.size() - 1; k >= 0; --k) {
        IPt m{half[k].x, -half[k].y};
        if (half[k].y == 0) continue;           // points on the axis are not doubled
        out.push_back(m);
    }
    if ((int)out.size() > kMaxPts) out.resize(kMaxPts);
    h = out;
    // special points: copy the kept side to the other one
    auto mirror = [](const std::vector<IPt>& src) {
        std::vector<IPt> r;
        for (IPt p : src) r.push_back({p.x, -p.y});
        return r;
    };
    int a[3] = {kLeftGun, kLeftRearGun, kLeftLight};
    for (int l : a) {
        int r = kParts[l].mirror;
        if (keep_left) s.pts[r] = mirror(s.pts[l]);
        else s.pts[l] = mirror(s.pts[r]);
    }
    return true;
}

} // namespace ss
