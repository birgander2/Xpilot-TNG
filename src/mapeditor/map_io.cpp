#include "map_io.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <strings.h>

#include <expat.h>
#include <zlib.h>

namespace xpe {

namespace {

bool Is(const char* a, const char* b) { return strcasecmp(a, b) == 0; }

// ============================================================================
// reading
// ============================================================================

struct Parser {
    Map* map = nullptr;
    LoadResult* res = nullptr;
    double scale = 1.0;
    bool seen_root = false;
    bool in_options = false;
    int container = -1;         // item that receives nested polygons / options
    int option_owner = -1;      // Base or Cannon receiving <Option>
    Polygon* poly = nullptr;    // polygon being read
    Pt cur;                     // current vertex while reading offsets
    int cur_edge = kEdgeDefault;
    std::set<std::string> unknown_tags;
    std::set<std::string> unknown_styles;

    const char* Get(const char** attr, const char* name) {
        for (; *attr; attr += 2)
            if (Is(attr[0], name)) return attr[1];
        return nullptr;
    }
    int Int(const char** attr, const char* name, int def) {
        const char* v = Get(attr, name);
        return v ? std::atoi(v) : def;
    }
    double Real(const char** attr, const char* name, double def) {
        const char* v = Get(attr, name);
        return v ? std::atof(v) : def;
    }
    int Coord(const char** attr, const char* name) {
        const char* v = Get(attr, name);
        return v ? int(std::atoi(v) * scale) : 0;
    }
    Pt Pos(const char** attr) { return {Coord(attr, "x"), Coord(attr, "y")}; }
    uint32_t Color(const char** attr) {
        const char* v = Get(attr, "color");
        return v ? uint32_t(std::strtoul(v, nullptr, 16)) & 0xFFFFFF : 0;
    }
    void MissingStyle(const std::string& id) {
        if (unknown_styles.insert(id).second) res->warnings.push_back("Unknown style '" + id + "'.");
    }
    int PolyStyleRef(const char** attr, const char* name) {
        const char* v = Get(attr, name);
        if (!v) return -1;
        int idx = map->FindPolyStyle(v);
        if (idx < 0) MissingStyle(v);
        return idx;
    }

    Item& NewItem(Kind k, const char** attr) {
        Item it;
        it.kind = k;
        if (KindHasPos(k)) it.pos = Pos(attr);
        if (KindHasTeam(k)) {
            it.team = Int(attr, "team", kNoTeam);
            // TEAM_NOT_SET (65535) and other out of range values mean "no team"; bases keep it for validation
            if (k != Kind::Base && (it.team < 0 || it.team >= kMaxTeams)) it.team = kNoTeam;
        }
        map->items.push_back(std::move(it));
        return map->items.back();
    }

    void Start(const char* el, const char** attr) {
        if (Is(el, "XPilotMap")) {
            seen_root = true;
            for (const char** a = attr; *a; a += 2) {
                if (Is(a[0], "version")) map->version = a[1];
                else map->attrs.emplace_back(a[0], a[1]);
            }
            return;
        }
        if (Is(el, "GeneralOptions")) { in_options = true; return; }
        if (Is(el, "Group")) return;    // grouping hint of the old editor, no meaning to the server
        if (Is(el, "Scale")) { scale = Real(attr, "value", 1.0); return; }

        if (Is(el, "Option")) {
            const char* name = Get(attr, "name");
            const char* value = Get(attr, "value");
            if (!name) return;
            std::string v = value ? value : "";
            if (in_options) map->options.emplace_back(name, v);
            else if (option_owner >= 0) map->items[option_owner].options.emplace_back(name, v);
            else res->warnings.push_back(std::string("Option '") + name + "' outside of any context dropped.");
            return;
        }
        if (Is(el, "Edgestyle")) {
            EdgeStyle es;
            const char* id = Get(attr, "id");
            es.id = id ? id : "";
            es.width = Int(attr, "width", 0);
            es.color = Color(attr);
            es.style = Int(attr, "style", 0);
            map->edge_styles.push_back(es);
            return;
        }
        if (Is(el, "Bmpstyle")) {
            BmpStyle bs;
            const char* id = Get(attr, "id");
            const char* fn = Get(attr, "filename");
            const char* sc = Get(attr, "scalable");
            bs.id = id ? id : "";
            bs.filename = fn ? fn : "";
            bs.flags = (sc && Is(sc, "yes")) ? 1 : 0;   // the server ignores a "flags" attribute
            map->bmp_styles.push_back(bs);
            return;
        }
        if (Is(el, "Polystyle")) {
            PolyStyle ps;
            const char* id = Get(attr, "id");
            ps.id = id ? id : "";
            ps.color = Color(attr);
            if (const char* t = Get(attr, "texture")) {
                ps.texture = map->FindBmpStyle(t);
                if (ps.texture < 0) MissingStyle(t);
            }
            if (const char* d = Get(attr, "defedge")) {
                ps.defedge = map->FindEdgeStyle(d);
                if (ps.defedge < 0) MissingStyle(d);
            }
            ps.flags = Int(attr, "flags", 0);
            map->poly_styles.push_back(ps);
            return;
        }

        if (Is(el, "Polygon")) {
            Polygon p;
            cur = Pos(attr);
            p.pts.push_back(cur);
            p.style = PolyStyleRef(attr, "style");
            cur_edge = kEdgeDefault;
            if (container < 0) {
                Item& it = NewItem(Kind::Wall, attr);
                it.polys.push_back(std::move(p));
                poly = &it.polys.back();
            } else {
                Item& it = map->items[container];
                it.polys.push_back(std::move(p));
                poly = &it.polys.back();
            }
            return;
        }
        if (Is(el, "Offset")) {
            if (!poly) return;
            const Pt off = Pos(attr);
            if (const char* s = Get(attr, "style")) {
                int e = map->FindEdgeStyle(s);
                if (e == kEdgeDefault) MissingStyle(s);
                cur_edge = e;
            }
            if (off.x == 0 && off.y == 0) return;   // the server skips zero-length edges, too
            poly->edges.push_back(cur_edge);
            cur.x += off.x;
            cur.y += off.y;
            poly->pts.push_back(cur);
            return;
        }
        if (Is(el, "Style")) {
            const char* state = Get(attr, "state");
            if (poly && state && Is(state, "destroyed")) poly->destroyed_style = PolyStyleRef(attr, "id");
            return;
        }

        struct { const char* tag; Kind kind; } containers[] = {
            {"Decor", Kind::Decor},       {"BallArea", Kind::BallArea}, {"BallTarget", Kind::BallTarget},
            {"FrictionArea", Kind::FrictionArea}, {"Cannon", Kind::Cannon}, {"Target", Kind::Target},
            {"Wormhole", Kind::Wormhole},
        };
        for (const auto& c : containers) {
            if (!Is(el, c.tag)) continue;
            Item& it = NewItem(c.kind, attr);
            if (c.kind == Kind::FrictionArea) it.value = Real(attr, "friction", 0.0);
            if (c.kind == Kind::Cannon) it.dir = Int(attr, "dir", kRes / 4);
            if (c.kind == Kind::Wormhole) {
                const char* t = Get(attr, "type");
                for (int i = 0; t && i < int(WormType::Count); i++)
                    if (Is(t, WormTypeName(WormType(i)))) it.type = i;
            }
            container = int(map->items.size()) - 1;
            if (c.kind == Kind::Cannon) option_owner = container;
            return;
        }

        if (Is(el, "Base")) {
            Item& it = NewItem(Kind::Base, attr);
            it.dir = Int(attr, "dir", kRes / 4);
            it.order = Int(attr, "order", 0);
            option_owner = int(map->items.size()) - 1;
            return;
        }
        if (Is(el, "Fuel")) { NewItem(Kind::Fuel, attr); return; }
        if (Is(el, "Check")) { NewItem(Kind::Check, attr); return; }
        if (Is(el, "ItemConcentrator")) { NewItem(Kind::ItemConc, attr); return; }
        if (Is(el, "AsteroidConcentrator")) { NewItem(Kind::AsteroidConc, attr); return; }
        if (Is(el, "Ball")) {
            Item& it = NewItem(Kind::Ball, attr);
            it.style = PolyStyleRef(attr, "style");
            return;
        }
        if (Is(el, "Grav")) {
            Item& it = NewItem(Kind::Grav, attr);
            it.value = Real(attr, "force", 0.0);
            const char* t = Get(attr, "type");
            for (int i = 0; t && i < int(GravType::Count); i++)
                if (Is(t, GravTypeName(GravType(i)))) it.type = i;
            return;
        }
        if (unknown_tags.insert(el).second)
            res->warnings.push_back(std::string("Unknown tag <") + el + "> ignored.");
    }

    void End(const char* el) {
        if (Is(el, "GeneralOptions")) in_options = false;
        else if (Is(el, "Polygon")) {
            if (poly) {
                if (poly->pts.size() > 1 && poly->pts.back() == poly->pts.front()) poly->pts.pop_back();
                else poly->edges.push_back(cur_edge);   // unclosed outline: implicit closing edge
                poly->edges.resize(poly->pts.size(), kEdgeDefault);
            }
            poly = nullptr;
        } else if (Is(el, "Base")) option_owner = -1;
        else if (Is(el, "Cannon")) { option_owner = -1; container = -1; }
        else if (Is(el, "Decor") || Is(el, "BallArea") || Is(el, "BallTarget") || Is(el, "FrictionArea") ||
                 Is(el, "Target") || Is(el, "Wormhole"))
            container = -1;
    }
};

void XMLCALL OnStart(void* ud, const char* el, const char** attr) { static_cast<Parser*>(ud)->Start(el, attr); }
void XMLCALL OnEnd(void* ud, const char* el) { static_cast<Parser*>(ud)->End(el); }

bool ReadAll(const std::string& path, std::string& data) {
    gzFile in = gzopen(path.c_str(), "rb");     // transparently reads uncompressed files
    if (!in) return false;
    char buf[65536];
    int n;
    while ((n = gzread(in, buf, sizeof(buf))) > 0) data.append(buf, size_t(n));
    const bool ok = n == 0;
    gzclose(in);
    return ok;
}

// ============================================================================
// writing
// ============================================================================

std::string Esc(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        case '\n': o += "&#10;"; break;   // keep line breaks (mapData) through attribute normalization
        case '\r': o += "&#13;"; break;
        case '\t': o += "&#9;"; break;
        default: o += c;
        }
    }
    return o;
}

std::string Hex(uint32_t c) {
    char b[8];
    std::snprintf(b, sizeof(b), "%06X", c & 0xFFFFFF);
    return b;
}

std::string Real(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.6g", v);
    return b;
}

struct Writer {
    explicit Writer(const Map& map) : m(map) {}
    const Map& m;
    std::ostringstream o;

    std::string PolyId(int i) const {
        return i >= 0 && i < int(m.poly_styles.size()) ? m.poly_styles[i].id : std::string();
    }
    std::string EdgeId(int i) const {
        if (i == kEdgeInternal) return "internal";
        return i >= 0 && i < int(m.edge_styles.size()) ? m.edge_styles[i].id : std::string();
    }
    std::string Team(int team) const {
        return team >= 0 ? " team=\"" + std::to_string(team) + "\"" : std::string();
    }
    std::string XY(Pt p) const {
        return " x=\"" + std::to_string(p.x) + "\" y=\"" + std::to_string(p.y) + "\"";
    }

    void WritePolygon(const Polygon& p) {
        if (p.pts.empty()) return;
        o << "<Polygon" << XY(p.pts[0]) << " style=\"" << Esc(PolyId(p.style)) << "\">\n";
        if (p.destroyed_style >= 0)
            o << "<Style state=\"destroyed\" id=\"" << Esc(PolyId(p.destroyed_style)) << "\"/>\n";
        const int defedge = p.style >= 0 && p.style < int(m.poly_styles.size()) ? m.poly_styles[p.style].defedge
                                                                                   : kEdgeDefault;
        int current = defedge;   // the server starts each polygon with the default edge style
        const size_t n = p.pts.size();
        for (size_t i = 0; i < n; i++) {
            const Pt a = p.pts[i], b = p.pts[(i + 1) % n];
            if (a == b) continue;
            o << "<Offset x=\"" << (b.x - a.x) << "\" y=\"" << (b.y - a.y) << "\"";
            int e = i < p.edges.size() ? p.edges[i] : kEdgeDefault;
            if (e == kEdgeDefault) e = defedge;
            if (e != current && !EdgeId(e).empty()) {
                o << " style=\"" << Esc(EdgeId(e)) << "\"";
                current = e;
            }
            o << "/>\n";
        }
        o << "</Polygon>\n";
    }

    void WriteOptions(const OptionList& opts) {
        for (const auto& [k, v] : opts)
            o << "<Option name=\"" << Esc(k) << "\" value=\"" << Esc(v) << "\"/>\n";
    }

    void WriteItem(const Item& it) {
        const char* tag = KindTag(it.kind);
        switch (it.kind) {
        case Kind::Wall:
            for (const auto& p : it.polys) WritePolygon(p);
            return;
        case Kind::Decor:
        case Kind::BallArea:
            o << "<" << tag << ">\n";
            break;
        case Kind::BallTarget:
            o << "<" << tag << Team(it.team) << ">\n";
            break;
        case Kind::FrictionArea:
            o << "<" << tag << " friction=\"" << Real(it.value) << "\">\n";
            break;
        case Kind::Cannon:
            o << "<" << tag << Team(it.team) << XY(it.pos) << " dir=\"" << it.dir << "\">\n";
            WriteOptions(it.options);
            break;
        case Kind::Target:
            o << "<" << tag << Team(it.team) << XY(it.pos) << ">\n";
            break;
        case Kind::Wormhole:
            o << "<" << tag << XY(it.pos) << " type=\"" << WormTypeName(WormType(it.type)) << "\">\n";
            break;
        case Kind::Base:
            o << "<Base" << Team(it.team) << XY(it.pos) << " dir=\"" << it.dir << "\"";
            if (it.order) o << " order=\"" << it.order << "\"";
            if (it.options.empty()) o << "/>\n";
            else {
                o << ">\n";
                WriteOptions(it.options);
                o << "</Base>\n";
            }
            return;
        case Kind::Ball:
            o << "<Ball" << Team(it.team) << XY(it.pos);
            if (it.style >= 0) o << " style=\"" << Esc(PolyId(it.style)) << "\"";
            o << "/>\n";
            return;
        case Kind::Grav:
            o << "<Grav" << XY(it.pos) << " type=\"" << GravTypeName(GravType(it.type)) << "\" force=\""
              << Real(it.value) << "\"/>\n";
            return;
        case Kind::Fuel:
            o << "<Fuel" << Team(it.team) << XY(it.pos) << "/>\n";
            return;
        default:
            o << "<" << tag << XY(it.pos) << "/>\n";
            return;
        }
        for (const auto& p : it.polys) WritePolygon(p);
        o << "</" << tag << ">\n";
    }

    std::string Run() {
        o << "<XPilotMap version=\"" << Esc(m.version.empty() ? "1.1" : m.version) << "\"";
        for (const auto& [k, v] : m.attrs) o << "\n           " << k << "=\"" << Esc(v) << "\"";
        o << ">\n<GeneralOptions>\n";
        WriteOptions(m.options);
        o << "</GeneralOptions>\n";
        for (const auto& e : m.edge_styles)
            o << "<Edgestyle id=\"" << Esc(e.id) << "\" width=\"" << e.width << "\" color=\"" << Hex(e.color)
              << "\" style=\"" << e.style << "\"/>\n";
        for (const auto& b : m.bmp_styles) {
            o << "<BmpStyle id=\"" << Esc(b.id) << "\" filename=\"" << Esc(b.filename) << "\"";
            if (b.flags & 1) o << " scalable=\"yes\"";
            o << "/>\n";
        }
        for (const auto& p : m.poly_styles) {
            o << "<Polystyle id=\"" << Esc(p.id) << "\" color=\"" << Hex(p.color) << "\"";
            if (p.texture >= 0 && p.texture < int(m.bmp_styles.size()))
                o << " texture=\"" << Esc(m.bmp_styles[p.texture].id) << "\"";
            o << " defedge=\"" << Esc(EdgeId(p.defedge)) << "\" flags=\"" << p.flags << "\"/>\n";
        }
        for (const auto& it : m.items) WriteItem(it);
        o << "</XPilotMap>\n";
        return o.str();
    }
};

} // namespace

LoadResult LoadMap(const std::string& path, Map& out) {
    LoadResult res;
    std::string data;
    if (!ReadAll(path, data)) {
        res.error = "Cannot read " + path + ": " + std::strerror(errno);
        return res;
    }
    // .xpd: "XPD <version>\n" followed by entries "<name> <length>\n<data>" (map and bitmaps)
    size_t begin = 0, length = data.size();
    if (data.compare(0, 4, "XPD ") == 0) {
        size_t pos = data.find('\n');
        bool found = false;
        while (pos != std::string::npos && pos + 1 < data.size()) {
            const size_t eol = data.find('\n', pos + 1);
            if (eol == std::string::npos) break;
            char name[512];
            unsigned len = 0;
            if (std::sscanf(data.substr(pos + 1, eol - pos - 1).c_str(), "%511s %u", name, &len) != 2) break;
            const size_t start = eol + 1;
            len = unsigned(std::min<size_t>(len, data.size() - start));
            if (data.compare(start, 10, "<XPilotMap") == 0) {
                begin = start;
                length = len;
                found = true;
                break;
            }
            pos = start + len - 1;     // the next header follows the entry data
        }
        if (!found) {
            res.error = "No map found in the .xpd bundle.";
            return res;
        }
        res.was_xpd = true;
    }

    Map map;
    Parser p;
    p.map = &map;
    p.res = &res;
    XML_Parser xp = XML_ParserCreate(nullptr);
    XML_SetUserData(xp, &p);
    XML_SetElementHandler(xp, OnStart, OnEnd);
    if (XML_Parse(xp, data.data() + begin, int(length), 1) == XML_STATUS_ERROR) {
        res.error = "XML error at line " + std::to_string(XML_GetCurrentLineNumber(xp)) + ": " +
                    XML_ErrorString(XML_GetErrorCode(xp));
        XML_ParserFree(xp);
        return res;
    }
    XML_ParserFree(xp);
    if (!p.seen_root) {
        res.error = "Not an XPilot map (no <XPilotMap> element).";
        return res;
    }
    if (p.scale != 1.0) res.warnings.push_back("<Scale> was applied to all coordinates.");
    out = std::move(map);
    res.ok = true;
    return res;
}

std::string SerializeMap(const Map& map) {
    Writer w(map);
    return w.Run();
}

bool SaveMap(const std::string& path, const Map& map, std::string* error) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (error) *error = "Cannot write " + tmp + ": " + std::strerror(errno);
            return false;
        }
        f << SerializeMap(map);
        f.close();
        if (!f) {
            if (error) *error = "Write error on " + tmp;
            std::remove(tmp.c_str());
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "Cannot replace " + path + ": " + ec.message();
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace xpe
