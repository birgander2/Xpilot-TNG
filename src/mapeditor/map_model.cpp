#include "map_model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <strings.h>

namespace xpe {

namespace {

bool IEquals(const std::string& a, const std::string& b) {
    return strcasecmp(a.c_str(), b.c_str()) == 0;
}

struct BuiltinEdge { const char* id; int width; uint32_t color; };
struct BuiltinPoly { const char* id; uint32_t color; const char* edge; int flags; };

// Same palette as tools/xp2convert.py, so converted and edited maps look alike.
const BuiltinEdge kBuiltinEdges[] = {
    {"wall_es", 2, 0x2244EE},     {"decor_es", 1, 0x1A2E77},  {"treasure_es", 2, 0xFF0000},
    {"target_es", 3, 0xFF7700},   {"cannon_es", 3, 0xFFFFFF}, {"destroyed_es", 3, 0xFF0000},
    {"wormhole_es", -1, 0x00FFFF}, {"fa_es", 2, 0xFF1F00},
};
const BuiltinPoly kBuiltinPolys[] = {
    {"wall_ps", 0x0033AA, "wall_es", kStyleFilled},
    {"decor_ps", 0x001A55, "decor_es", kStyleFilled},
    {"treasure_ps", 0xFF0000, "treasure_es", 0},
    {"target_ps", 0xFF7700, "target_es", 0},
    {"cannon_ps", 0xFFFFFF, "cannon_es", 0},
    {"destroyed_ps", 0xFF0000, "destroyed_es", kStyleInvisible | kStyleInvisibleRadar},
    {"wormhole_ps", 0x00FFFF, "wormhole_es", 0},
    {"fa_ps", 0xCF1F00, "fa_es", 0},
};

int ParseIntOr(const std::string& s, int def) {
    if (s.empty()) return def;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    return end && end != s.c_str() ? int(v) : def;
}

} // namespace

// ============================================================================
// Map
// ============================================================================

int Map::FindOption(const std::string& name) const {
    for (size_t i = 0; i < options.size(); i++)
        if (IEquals(options[i].first, name)) return int(i);
    return -1;
}

std::string Map::Option(const std::string& name, const std::string& def) const {
    int i = FindOption(name);
    return i >= 0 ? options[i].second : def;
}

void Map::SetOption(const std::string& name, const std::string& value) {
    int i = FindOption(name);
    if (i >= 0) options[i].second = value;
    else options.emplace_back(name, value);
}

void Map::RemoveOption(const std::string& name) {
    int i = FindOption(name);
    if (i >= 0) options.erase(options.begin() + i);
}

bool Map::OptionBool(const std::string& name, bool def) const {
    int i = FindOption(name);
    if (i < 0) return def;
    const std::string& v = options[i].second;
    return IEquals(v, "yes") || IEquals(v, "true") || IEquals(v, "on") || v == "1";
}

std::string Map::Attr(const std::string& name) const {
    for (const auto& [k, v] : attrs)
        if (IEquals(k, name)) return v;
    return "";
}

void Map::SetAttr(const std::string& name, const std::string& value) {
    for (size_t i = 0; i < attrs.size(); i++) {
        if (IEquals(attrs[i].first, name)) {
            if (value.empty()) attrs.erase(attrs.begin() + i);
            else attrs[i].second = value;
            return;
        }
    }
    if (!value.empty()) attrs.emplace_back(name, value);
}

int Map::WidthPx() const { return std::max(1, ParseIntOr(Option("mapWidth"), 3500)); }
int Map::HeightPx() const { return std::max(1, ParseIntOr(Option("mapHeight"), 3500)); }

int Map::FindEdgeStyle(const std::string& id) const {
    if (id == "internal") return kEdgeInternal;
    for (size_t i = 0; i < edge_styles.size(); i++)
        if (edge_styles[i].id == id) return int(i);
    return kEdgeDefault;
}

int Map::FindPolyStyle(const std::string& id) const {
    for (size_t i = 0; i < poly_styles.size(); i++)
        if (poly_styles[i].id == id) return int(i);
    return -1;
}

int Map::FindBmpStyle(const std::string& id) const {
    for (size_t i = 0; i < bmp_styles.size(); i++)
        if (bmp_styles[i].id == id) return int(i);
    return -1;
}

int Map::EnsureBuiltinPolyStyle(const std::string& id) {
    int idx = FindPolyStyle(id);
    if (idx >= 0) return idx;
    for (const auto& bp : kBuiltinPolys) {
        if (id != bp.id) continue;
        int e = FindEdgeStyle(bp.edge);
        if (e < 0) {
            for (const auto& be : kBuiltinEdges) {
                if (std::string(be.id) == bp.edge) {
                    edge_styles.push_back({be.id, be.width, be.color, 0});
                    e = int(edge_styles.size()) - 1;
                }
            }
        }
        poly_styles.push_back({bp.id, bp.color, -1, e, bp.flags});
        return int(poly_styles.size()) - 1;
    }
    // unknown builtin: plain style on the first edge style
    if (edge_styles.empty()) edge_styles.push_back({"wall_es", 2, 0x2244EE, 0});
    poly_styles.push_back({id, 0x0033AA, -1, 0, kStyleFilled});
    return int(poly_styles.size()) - 1;
}

namespace {
// Index fixup after removing element `idx` from a style table.
int Remap(int ref, int idx, int replacement) {
    if (ref == idx) return replacement > idx ? replacement - 1 : replacement;
    if (ref > idx) return ref - 1;
    return ref;
}
} // namespace

void Map::RemoveEdgeStyle(int idx, int replacement) {
    if (idx < 0 || idx >= int(edge_styles.size())) return;
    for (auto& ps : poly_styles) ps.defedge = Remap(ps.defedge, idx, replacement);
    for (auto& it : items)
        for (auto& p : it.polys)
            for (int& e : p.edges)
                if (e >= 0) e = Remap(e, idx, replacement);
    edge_styles.erase(edge_styles.begin() + idx);
}

void Map::RemovePolyStyle(int idx, int replacement) {
    if (idx < 0 || idx >= int(poly_styles.size())) return;
    for (auto& it : items) {
        if (it.style >= 0) it.style = Remap(it.style, idx, replacement);
        for (auto& p : it.polys) {
            p.style = Remap(p.style, idx, replacement);
            if (p.destroyed_style >= 0) p.destroyed_style = Remap(p.destroyed_style, idx, -1);
        }
    }
    poly_styles.erase(poly_styles.begin() + idx);
}

void Map::RemoveBmpStyle(int idx) {
    if (idx < 0 || idx >= int(bmp_styles.size())) return;
    for (auto& ps : poly_styles) {
        ps.texture = Remap(ps.texture, idx, -1);
        if (ps.texture < 0) ps.flags &= ~kStyleTextured;
    }
    bmp_styles.erase(bmp_styles.begin() + idx);
}

int Map::CountEdgeStyleUse(int idx) const {
    int n = 0;
    for (const auto& ps : poly_styles) n += ps.defedge == idx;
    for (const auto& it : items)
        for (const auto& p : it.polys)
            for (int e : p.edges) n += e == idx;
    return n;
}

int Map::CountPolyStyleUse(int idx) const {
    int n = 0;
    for (const auto& it : items) {
        n += it.kind == Kind::Ball && it.style == idx;
        for (const auto& p : it.polys) n += (p.style == idx) + (p.destroyed_style == idx);
    }
    return n;
}

// ============================================================================
// kind metadata
// ============================================================================

const char* KindName(Kind k) {
    switch (k) {
    case Kind::Wall: return "Wall";
    case Kind::Decor: return "Decoration";
    case Kind::BallArea: return "Ball area";
    case Kind::BallTarget: return "Ball target";
    case Kind::FrictionArea: return "Friction area";
    case Kind::Cannon: return "Cannon";
    case Kind::Target: return "Target";
    case Kind::Wormhole: return "Wormhole";
    case Kind::Base: return "Base";
    case Kind::Fuel: return "Fuel station";
    case Kind::Check: return "Checkpoint";
    case Kind::Ball: return "Ball";
    case Kind::Grav: return "Gravity";
    case Kind::ItemConc: return "Item concentrator";
    case Kind::AsteroidConc: return "Asteroid concentrator";
    default: return "?";
    }
}

const char* KindTag(Kind k) {
    switch (k) {
    case Kind::Wall: return "Polygon";
    case Kind::Decor: return "Decor";
    case Kind::BallArea: return "BallArea";
    case Kind::BallTarget: return "BallTarget";
    case Kind::FrictionArea: return "FrictionArea";
    case Kind::Cannon: return "Cannon";
    case Kind::Target: return "Target";
    case Kind::Wormhole: return "Wormhole";
    case Kind::Base: return "Base";
    case Kind::Fuel: return "Fuel";
    case Kind::Check: return "Check";
    case Kind::Ball: return "Ball";
    case Kind::Grav: return "Grav";
    case Kind::ItemConc: return "ItemConcentrator";
    case Kind::AsteroidConc: return "AsteroidConcentrator";
    default: return "?";
    }
}

bool KindIsContainer(Kind k) { return k <= Kind::FrictionArea; }
bool KindHasPolys(Kind k) { return k <= Kind::Wormhole; }
bool KindHasPos(Kind k) { return k >= Kind::Cannon; }
bool KindHasTeam(Kind k) {
    return k == Kind::BallTarget || k == Kind::Cannon || k == Kind::Target || k == Kind::Base ||
           k == Kind::Fuel || k == Kind::Ball;
}
bool KindHasDir(Kind k) { return k == Kind::Base || k == Kind::Cannon; }

const char* GravTypeName(GravType t) {
    static const char* names[] = {"pos", "neg", "cwise", "acwise", "up", "down", "right", "left"};
    int i = int(t);
    return i >= 0 && i < int(GravType::Count) ? names[i] : "pos";
}

const char* WormTypeName(WormType t) {
    static const char* names[] = {"normal", "in", "out", "fixed"};
    int i = int(t);
    return i >= 0 && i < int(WormType::Count) ? names[i] : "normal";
}

const char* DefaultStyleFor(Kind k) {
    switch (k) {
    case Kind::Decor: return "decor_ps";
    case Kind::BallArea:
    case Kind::BallTarget: return "treasure_ps";
    case Kind::FrictionArea: return "fa_ps";
    case Kind::Cannon: return "cannon_ps";
    case Kind::Target: return "target_ps";
    case Kind::Wormhole: return "wormhole_ps";
    default: return "wall_ps";
    }
}

// ============================================================================
// construction
// ============================================================================

Map NewMap(int width_px, int height_px, const std::string& name, const std::string& author) {
    Map m;
    m.SetOption("mapName", name);
    m.SetOption("mapAuthor", author);
    m.SetOption("mapWidth", std::to_string(width_px));
    m.SetOption("mapHeight", std::to_string(height_px));
    m.SetOption("edgeWrap", "yes");
    m.EnsureBuiltinPolyStyle("wall_ps");
    m.EnsureBuiltinPolyStyle("decor_ps");
    return m;
}

Polygon MakePolygon(std::vector<Pt> pts, int style) {
    Polygon p;
    p.edges.assign(pts.size(), kEdgeDefault);
    p.pts = std::move(pts);
    p.style = style;
    return p;
}

namespace {

std::vector<Pt> CirclePts(Pt c, int r, int n) {
    std::vector<Pt> pts;
    for (int i = 0; i < n; i++) {
        double a = 2.0 * M_PI * i / n;
        pts.push_back({c.x + int(std::lround(r * std::cos(a))), c.y + int(std::lround(r * std::sin(a)))});
    }
    return pts;
}

} // namespace

void RebuildCannonPoly(Map& map, Item& c) {
    // Triangle with the tip slightly behind the cannon position and the base half
    // a block behind it, like the converter does for block-map cannons.
    const double a = 2.0 * M_PI * c.dir / kRes;
    const double dx = std::cos(a), dy = std::sin(a);
    const double h = kBlockClicks / 2.0;
    auto at = [&](double fwd, double side) {
        return Pt{c.pos.x + int(std::lround(fwd * dx - side * dy)),
                  c.pos.y + int(std::lround(fwd * dy + side * dx))};
    };
    std::vector<Pt> pts = {at(0, 0), at(-h / 1.5, h), at(-h / 1.5, -h)};
    Polygon p = MakePolygon(pts, map.EnsureBuiltinPolyStyle("cannon_ps"));
    p.destroyed_style = map.EnsureBuiltinPolyStyle("destroyed_ps");
    c.polys.clear();
    c.polys.push_back(std::move(p));
}

Item MakeItem(Map& map, Kind kind, Pt pos) {
    Item it;
    it.kind = kind;
    it.pos = pos;
    const int h = kBlockClicks / 2;
    switch (kind) {
    case Kind::Base:
        it.team = 0;
        it.dir = kRes / 4;
        break;
    case Kind::Cannon:
        it.dir = kRes / 4;
        RebuildCannonPoly(map, it);
        break;
    case Kind::Target: {
        std::vector<Pt> pts = {{pos.x - h, pos.y - h}, {pos.x + h - 1, pos.y - h},
                               {pos.x + h - 1, pos.y + h - 1}, {pos.x - h, pos.y + h - 1}};
        Polygon p = MakePolygon(pts, map.EnsureBuiltinPolyStyle("target_ps"));
        p.destroyed_style = map.EnsureBuiltinPolyStyle("destroyed_ps");
        it.polys.push_back(std::move(p));
        break;
    }
    case Kind::Wormhole:
        it.type = int(WormType::Normal);
        it.polys.push_back(MakePolygon(CirclePts(pos, h - 1, 12), map.EnsureBuiltinPolyStyle("wormhole_ps")));
        break;
    case Kind::Grav:
        it.type = int(GravType::Pos);
        it.value = -2.7;
        break;
    case Kind::Ball:
        it.team = 0;
        break;
    default:
        break;
    }
    return it;
}

// ============================================================================
// geometry
// ============================================================================

void Box::Add(Pt p) {
    lo.x = std::min(lo.x, p.x);
    lo.y = std::min(lo.y, p.y);
    hi.x = std::max(hi.x, p.x);
    hi.y = std::max(hi.y, p.y);
}

Box ItemBounds(const Item& it) {
    Box b;
    if (KindHasPos(it.kind)) b.Add(it.pos);
    for (const auto& p : it.polys)
        for (const auto& v : p.pts) b.Add(v);
    return b;
}

bool PointInPolygon(const std::vector<Pt>& pts, double x, double y) {
    bool in = false;
    const size_t n = pts.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = pts[i].x, yi = pts[i].y, xj = pts[j].x, yj = pts[j].y;
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

double DistToSegment(double px, double py, Pt a, Pt b, double* t_out) {
    const double vx = b.x - a.x, vy = b.y - a.y;
    const double len2 = vx * vx + vy * vy;
    double t = len2 > 0 ? ((px - a.x) * vx + (py - a.y) * vy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    if (t_out) *t_out = t;
    const double dx = a.x + t * vx - px, dy = a.y + t * vy - py;
    return std::sqrt(dx * dx + dy * dy);
}

double SignedArea(const std::vector<Pt>& pts) {
    double a = 0;
    for (size_t i = 0, n = pts.size(); i < n; i++) {
        const Pt& p = pts[i];
        const Pt& q = pts[(i + 1) % n];
        a += double(p.x) * q.y - double(q.x) * p.y;
    }
    return a / 2;
}

namespace {
int Orient(Pt a, Pt b, Pt c) {
    const long long v = (long long)(b.x - a.x) * (c.y - a.y) - (long long)(b.y - a.y) * (c.x - a.x);
    return (v > 0) - (v < 0);
}
bool SegmentsCross(Pt a, Pt b, Pt c, Pt d) {
    return Orient(a, b, c) * Orient(a, b, d) < 0 && Orient(c, d, a) * Orient(c, d, b) < 0;
}
} // namespace

bool SelfIntersects(const std::vector<Pt>& pts) {
    const size_t n = pts.size();
    if (n < 4 || n > 2000) return false;    // big polygons: skip the O(n^2) test
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 2; j < n; j++) {
            if (i == 0 && j == n - 1) continue;     // adjacent through the closing edge
            if (SegmentsCross(pts[i], pts[(i + 1) % n], pts[j], pts[(j + 1) % n])) return true;
        }
    }
    return false;
}

std::vector<int> Triangulate(const std::vector<Pt>& pts) {
    std::vector<int> tris;
    // work on a counter-clockwise ring without repeated points
    std::vector<int> v;
    for (int i = 0; i < int(pts.size()); i++)
        if (v.empty() || !(pts[v.back()] == pts[i])) v.push_back(i);
    while (v.size() > 1 && pts[v.front()] == pts[v.back()]) v.pop_back();
    if (v.size() < 3) return tris;
    {
        std::vector<Pt> ring;
        for (int i : v) ring.push_back(pts[i]);
        if (SignedArea(ring) < 0) std::reverse(v.begin(), v.end());
    }
    auto inside = [&](Pt p, Pt a, Pt b, Pt c) {     // inside or on the border of CCW triangle abc
        return Orient(a, b, p) >= 0 && Orient(b, c, p) >= 0 && Orient(c, a, p) >= 0;
    };
    size_t guard = v.size() * v.size() + 16;
    size_t i = 0;
    while (v.size() > 3 && guard-- > 0) {
        const size_t n = v.size();
        const size_t ip = (i + n - 1) % n, in = (i + 1) % n;
        const Pt a = pts[v[ip]], b = pts[v[i]], c = pts[v[in]];
        const int o = Orient(a, b, c);
        if (o == 0) {                       // collinear point: drop it, no area lost
            v.erase(v.begin() + i);
            i %= v.size();
            continue;
        }
        bool ear = o > 0;
        for (size_t k = 0; ear && k < n; k++) {
            if (k == ip || k == i || k == in) continue;
            const Pt p = pts[v[k]];
            if (p == a || p == b || p == c) continue;
            if (inside(p, a, b, c)) ear = false;
        }
        if (ear) {
            tris.insert(tris.end(), {v[ip], v[i], v[in]});
            v.erase(v.begin() + i);
            i = i == 0 ? 0 : i - 1;
            i %= v.size();
        } else {
            i = (i + 1) % n;
        }
    }
    if (v.size() == 3) {
        if (Orient(pts[v[0]], pts[v[1]], pts[v[2]]) != 0) tris.insert(tris.end(), {v[0], v[1], v[2]});
    } else {
        // self-intersecting outline: fan the rest so at least something is shown
        for (size_t k = 1; k + 1 < v.size(); k++) tris.insert(tris.end(), {v[0], v[k], v[k + 1]});
    }
    return tris;
}

void TranslateItem(Item& it, int dx, int dy) {
    it.pos.x += dx;
    it.pos.y += dy;
    for (auto& p : it.polys)
        for (auto& v : p.pts) {
            v.x += dx;
            v.y += dy;
        }
}

void EnsureCCW(Polygon& p) {
    const size_t n = p.pts.size();
    if (n < 3 || SignedArea(p.pts) >= 0) return;
    std::vector<Pt> pts(n);
    std::vector<int> edges(n, kEdgeDefault);
    for (size_t j = 0; j < n; j++) {
        pts[j] = p.pts[n - 1 - j];
        const size_t orig = j < n - 1 ? n - 2 - j : n - 1;
        if (orig < p.edges.size()) edges[j] = p.edges[orig];
    }
    p.pts = std::move(pts);
    p.edges = std::move(edges);
}

namespace {

// Working ring for smoothing: vertex, style of its outgoing edge, fixed flag.
struct SVtx {
    double x, y;
    int edge;
    bool fixed;
};

double SegDist(const SVtx& p, const SVtx& a, const SVtx& b) {
    const double vx = b.x - a.x, vy = b.y - a.y, len2 = vx * vx + vy * vy;
    const double t = len2 > 0 ? std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(a.x + t * vx - p.x, a.y + t * vy - p.y);
}

// Douglas-Peucker on every chain between fixed vertices of the ring.
void SimplifyRing(std::vector<SVtx>& v, double tol) {
    const size_t n = v.size();
    if (n < 4 || tol <= 0) return;
    std::vector<char> keep(n, 0);
    std::vector<size_t> splits;
    for (size_t i = 0; i < n; i++)
        if (v[i].fixed) splits.push_back(i);
    if (splits.empty()) {                       // free ring: split at vertex 0 and the vertex farthest from it
        size_t far = 0;
        double best = -1;
        for (size_t i = 1; i < n; i++) {
            const double d = std::hypot(v[i].x - v[0].x, v[i].y - v[0].y);
            if (d > best) best = d, far = i;
        }
        splits = {0, far};
    }
    for (size_t s : splits) keep[s] = 1;
    std::vector<std::pair<size_t, size_t>> stack;   // chains as (start, length) along the ring
    for (size_t k = 0; k < splits.size(); k++) {
        const size_t a = splits[k], b = splits[(k + 1) % splits.size()];
        stack.push_back({a, (b + n - a) % n == 0 ? n : (b + n - a) % n});
    }
    while (!stack.empty()) {
        const auto [a, len] = stack.back();
        stack.pop_back();
        if (len < 2) continue;
        const size_t b = (a + len) % n;
        double best = -1;
        size_t bi = 0;
        for (size_t j = 1; j < len; j++) {
            const double d = SegDist(v[(a + j) % n], v[a], v[b]);
            if (d > best) best = d, bi = j;
        }
        if (best <= tol) continue;
        keep[(a + bi) % n] = 1;
        stack.push_back({a, bi});
        stack.push_back({(a + bi) % n, len - bi});
    }
    std::vector<SVtx> out;
    for (size_t i = 0; i < n; i++)
        if (keep[i]) out.push_back(v[i]);
    if (out.size() >= 3) v = std::move(out);
}

// One Chaikin pass; fixed vertices stay, all others are replaced by points at 1/4 and 3/4 of their edges.
void ChaikinRing(std::vector<SVtx>& v) {
    const size_t n = v.size();
    std::vector<SVtx> out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; i++) {
        const SVtx& a = v[i];
        const SVtx& b = v[(i + 1) % n];
        if (a.fixed) out.push_back(a);
        if (a.fixed && b.fixed) continue;       // edge between fixed vertices is kept as is
        if (!a.fixed) out.push_back({a.x * 0.75 + b.x * 0.25, a.y * 0.75 + b.y * 0.25, a.edge, false});
        if (!b.fixed) out.push_back({a.x * 0.25 + b.x * 0.75, a.y * 0.25 + b.y * 0.75, a.edge, false});
    }
    v = std::move(out);
}

} // namespace

Polygon SmoothPolygon(const Polygon& p, const SmoothParams& sp, const std::vector<Pt>& anchors) {
    const size_t n = p.pts.size();
    if (n < 3) return p;
    auto edge = [&](size_t i) { return i < p.edges.size() ? p.edges[i] : kEdgeDefault; };
    std::vector<SVtx> v(n);
    for (size_t i = 0; i < n; i++) {
        const int e = edge(i), ep = edge((i + n - 1) % n);
        bool fixed = e != ep || e == kEdgeInternal;
        for (size_t k = 0; !fixed && k < anchors.size(); k++)
            fixed = std::hypot(double(p.pts[i].x - anchors[k].x), double(p.pts[i].y - anchors[k].y)) <= sp.anchor_radius;
        v[i] = {double(p.pts[i].x), double(p.pts[i].y), e, fixed};
    }
    SimplifyRing(v, sp.tolerance);

    // keep sharp corners of the simplified outline
    const double keep = sp.keep_angle * M_PI / 180.0;
    for (size_t i = 0, m = v.size(); i < m; i++) {
        const SVtx& a = v[(i + m - 1) % m];
        const SVtx& b = v[(i + 1) % m];
        const double ax = a.x - v[i].x, ay = a.y - v[i].y, bx = b.x - v[i].x, by = b.y - v[i].y;
        const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
        if (la > 0 && lb > 0 && std::acos(std::clamp((ax * bx + ay * by) / (la * lb), -1.0, 1.0)) < keep)
            v[i].fixed = true;
    }
    for (int k = 0; k < sp.iterations; k++) ChaikinRing(v);

    // back to clicks; drop duplicates from rounding and points left on straight lines
    std::vector<SVtx> r;
    for (const SVtx& q : v) {
        SVtx t{std::round(q.x), std::round(q.y), q.edge, q.fixed};
        if (!r.empty() && r.back().x == t.x && r.back().y == t.y) {
            if (t.fixed) r.back() = t;
            continue;
        }
        r.push_back(t);
    }
    while (r.size() > 1 && r.front().x == r.back().x && r.front().y == r.back().y) r.pop_back();
    if (sp.iterations > 0) SimplifyRing(r, kClick * 0.5);
    if (r.size() < 3) return p;

    Polygon out = p;
    out.pts.clear();
    out.edges.clear();
    for (const SVtx& q : r) {
        out.pts.push_back({int(q.x), int(q.y)});
        out.edges.push_back(q.edge);
    }
    while (!out.edges.empty() && out.edges.back() == kEdgeDefault) out.edges.pop_back();
    return out;
}

// ============================================================================
// validation
// ============================================================================

std::vector<Problem> Validate(const Map& m) {
    std::vector<Problem> out;
    auto err = [&](int item, std::string t) { out.push_back({true, item, std::move(t)}); };
    auto warn = [&](int item, std::string t) { out.push_back({false, item, std::move(t)}); };

    const int w = m.WidthPx() * kClick, h = m.HeightPx() * kClick;
    const bool teamplay = m.OptionBool("teamPlay");
    if (m.FindOption("mapWidth") < 0 || m.FindOption("mapHeight") < 0)
        warn(-1, "mapWidth / mapHeight are not set (server default 3500 is used).");
    if (m.Option("mapName").empty()) warn(-1, "The map has no name (mapName).");

    for (size_t i = 0; i < m.poly_styles.size(); i++)
        if (m.poly_styles[i].defedge < 0)
            err(-1, "Polygon style '" + m.poly_styles[i].id + "' has no default edge style.");

    int bases = 0, checks = 0;
    bool team_has_base[kMaxTeams] = {};
    bool team_has_target[kMaxTeams] = {};
    for (size_t i = 0; i < m.items.size(); i++) {
        const Item& it = m.items[i];
        const int ii = int(i);
        if (it.kind == Kind::Base) {
            bases++;
            if (it.team < 0 || it.team >= kMaxTeams) err(ii, "Base has an invalid team (must be 0-9).");
            else team_has_base[it.team] = true;
        }
        if (it.kind == Kind::Check) checks++;
        if (it.kind == Kind::BallTarget && it.team >= 0 && it.team < kMaxTeams) team_has_target[it.team] = true;
        if (KindHasPos(it.kind) && (it.pos.x < 0 || it.pos.y < 0 || it.pos.x >= w || it.pos.y >= h))
            warn(ii, std::string(KindName(it.kind)) + " lies outside the map area.");
        if (KindIsContainer(it.kind) && it.polys.empty())
            err(ii, std::string(KindName(it.kind)) + " contains no polygon.");
        for (const auto& p : it.polys) {
            if (p.pts.size() < 3) err(ii, std::string(KindName(it.kind)) + ": polygon with fewer than 3 points.");
            if (p.style < 0) err(ii, std::string(KindName(it.kind)) + ": polygon without style.");
            if (SelfIntersects(p.pts)) warn(ii, std::string(KindName(it.kind)) + ": polygon intersects itself.");
            else if (p.pts.size() >= 3 && SignedArea(p.pts) < 0)
                warn(ii, std::string(KindName(it.kind)) + ": polygon is clockwise (Edit > Fix orientation).");
        }
        if (it.kind == Kind::Grav && it.value == 0.0) warn(ii, "Gravity with force 0 has no effect.");
    }
    if (bases == 0) err(-1, "The map has no base; nobody can play on it.");
    if (teamplay) {
        int teams = 0;
        for (bool b : team_has_base) teams += b;
        if (teams < 2) warn(-1, "teamPlay is on but bases exist for fewer than two teams.");
    }
    for (size_t i = 0; i < m.items.size(); i++) {
        const Item& it = m.items[i];
        if (it.kind == Kind::Ball && it.team >= 0 && it.team < kMaxTeams && !team_has_target[it.team])
            warn(int(i), "Ball of team " + std::to_string(it.team) + " has no ball target of that team.");
    }
    if (m.OptionBool("timing") && checks < 2) warn(-1, "Race mode (timing) needs at least two checkpoints.");
    return out;
}

} // namespace xpe
