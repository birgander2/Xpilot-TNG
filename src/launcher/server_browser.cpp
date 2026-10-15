#include "server_browser.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "metaserver.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kServerPort = 15345;          // SERVER_PORT in pack.h
constexpr int kMetaProgPort = 4401;         // META_PROG_PORT in client/meta.h
constexpr uint32_t kMagic = (0x4F15u << 16) | 0xF4EDu;   // POLYGON_VERSION, MAGIC_WORD
constexpr uint8_t kReportStatus = 0x21;     // REPORT_STATUS_pack
constexpr uint8_t kContact = 0x31;          // CONTACT_pack
constexpr int kMetaFields = 18;             // NUM_META_DATA_FIELDS

long ElapsedMs(Clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
}

// Contact packet as built by Packet_printf("%u%s%hu%c", magic, name, port, type)
std::string BuildPacket(uint8_t type) {
    std::string p;
    for (int shift = 24; shift >= 0; shift -= 8) p.push_back(char((kMagic >> shift) & 0xFF));
    p += "launcher";
    p.push_back('\0');
    p.push_back('\0');   // reply port (the server answers to the source address anyway)
    p.push_back('\0');
    p.push_back(char(type));
    return p;
}

// "KEY....: value" line of the status text -> value
std::string StatusValue(const std::string& text, const char* key) {
    size_t pos = text.find(key);
    if (pos == std::string::npos) return "";
    size_t colon = text.find(':', pos);
    size_t eol = text.find('\n', pos);
    if (colon == std::string::npos || colon > eol) return "";
    size_t b = text.find_first_not_of(' ', colon + 1);
    if (b == std::string::npos || b > eol) return "";
    return text.substr(b, eol - b);
}

bool ParseStatusReply(const char* buf, int len, uint8_t expect, ServerEntry* out) {
    if (len < 6) return false;
    uint32_t magic = (uint32_t(uint8_t(buf[0])) << 24) | (uint32_t(uint8_t(buf[1])) << 16) |
                     (uint32_t(uint8_t(buf[2])) << 8) | uint8_t(buf[3]);
    if ((magic & 0xFFFF) != (kMagic & 0xFFFF)) return false;
    if (uint8_t(buf[4]) != expect || buf[5] != 0) return false;   // status 0 = SUCCESS
    if (expect == kContact) return true;

    std::string text(buf + 6, size_t(len - 6));
    out->version = StatusValue(text, "SERVER VERSION");
    out->status = StatusValue(text, "STATUS");
    out->map = StatusValue(text, "WORLD");
    out->author = StatusValue(text, "AUTHOR");
    std::string fps = StatusValue(text, "CURRENT FPS");
    out->fps = std::atoi(fps.c_str());
    std::string pl = StatusValue(text, "PLAYERS");
    std::sscanf(pl.c_str(), "%d/%d", &out->players, &out->bases);
    return true;
}

std::set<std::string> LocalAddresses() {
    std::set<std::string> addrs;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return addrs;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        char buf[INET_ADDRSTRLEN];
        auto* sin = reinterpret_cast<sockaddr_in*>(a->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf)) addrs.insert(buf);
    }
    freeifaddrs(list);
    return addrs;
}

std::vector<sockaddr_in> BroadcastTargets(int port) {
    std::vector<sockaddr_in> targets;
    auto add = [&](in_addr a) {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(uint16_t(port));
        sa.sin_addr = a;
        targets.push_back(sa);
    };
    in_addr all{};
    all.s_addr = htonl(INADDR_BROADCAST);
    add(all);
    in_addr lo{};
    lo.s_addr = htonl(INADDR_LOOPBACK);
    add(lo);
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            if (!(a->ifa_flags & IFF_UP) || !(a->ifa_flags & IFF_BROADCAST) || !a->ifa_broadaddr) continue;
            add(reinterpret_cast<sockaddr_in*>(a->ifa_broadaddr)->sin_addr);
        }
        freeifaddrs(list);
    }
    return targets;
}

int OpenUdp(bool broadcast) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    if (broadcast) {
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    }
    return fd;
}

// Sends a CONTACT to every server and fills in ping_ms from the replies.
void PingServers(std::vector<ServerEntry>& servers, const std::atomic<bool>& cancel) {
    int fd = OpenUdp(false);
    if (fd < 0) return;
    const std::string pkt = BuildPacket(kContact);
    auto start = Clock::now();
    for (const auto& s : servers) {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(uint16_t(s.port));
        if (inet_pton(AF_INET, s.ip.c_str(), &sa.sin_addr) != 1) continue;
        sendto(fd, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    }
    int pending = int(servers.size());
    while (pending > 0 && !cancel && ElapsedMs(start) < 2000) {
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, 100) <= 0) continue;
        char buf[1024];
        sockaddr_in from{};
        socklen_t flen = sizeof from;
        int n = int(recvfrom(fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &flen));
        if (n <= 0 || !ParseStatusReply(buf, n, kContact, nullptr)) continue;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        for (auto& s : servers)
            if (s.ping_ms < 0 && s.ip == ip && s.port == ntohs(from.sin_port)) {
                s.ping_ms = int(ElapsedMs(start));
                pending--;
            }
    }
    close(fd);
}

// Whole body of a TCP connection to host:port (up to a time limit). Empty on failure.
bool FetchAll(const char* host, int port, std::string* body, std::string* err, const std::atomic<bool>& cancel) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        *err = std::string("cannot resolve ") + host;
        return false;
    }
    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0) { freeaddrinfo(res); *err = "no socket"; return false; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0 && errno == EINPROGRESS) {
        pollfd pfd{fd, POLLOUT, 0};
        int waited = 0;
        while (waited < 5000 && !cancel && (rc = poll(&pfd, 1, 100)) == 0) waited += 100;
        int soerr = 0;
        socklen_t sl = sizeof soerr;
        if (rc <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
            close(fd);
            *err = std::string("cannot connect to ") + host;
            return false;
        }
    } else if (rc != 0) {
        close(fd);
        *err = std::string("cannot connect to ") + host;
        return false;
    }
    auto start = Clock::now();
    char buf[4096];
    while (!cancel && ElapsedMs(start) < 8000) {
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, 100) <= 0) continue;
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) break;
        body->append(buf, size_t(n));
        if (body->size() > (4u << 20)) break;
    }
    close(fd);
    return true;
}

bool ParseMetaLine(const std::string& line, ServerEntry* e) {
    std::vector<std::string> f;
    size_t b = 0;
    for (;;) {
        size_t c = line.find(':', b);
        f.push_back(line.substr(b, c == std::string::npos ? std::string::npos : c - b));
        if (c == std::string::npos) break;
        b = c + 1;
    }
    if (int(f.size()) < kMetaFields) return false;
    int port = std::atoi(f[2].c_str());
    if (port <= 0 || port > 65535 || f[15].empty()) return false;
    e->version = f[0];
    e->host = f[1];
    e->port = port;
    e->players = std::atoi(f[3].c_str());
    e->map = f[4];
    e->author = f[6];
    e->status = f[7];
    e->bases = std::atoi(f[8].c_str());
    e->fps = std::atoi(f[9].c_str());
    e->teambases = std::atoi(f[13].c_str());
    e->ip = f[15];
    return true;
}

}  // namespace

ServerSearch::~ServerSearch() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

void ServerSearch::Start() {
    if (busy_) return;
    if (thread_.joinable()) thread_.join();
    cancel_ = false;
    busy_ = true;
    started_ = true;
    SetMessage(kind_ == Kind::Lan ? "Searching the local network ..." : "Contacting meta servers ...");
    thread_ = std::thread([this] {
        if (kind_ == Kind::Lan) RunLan();
        else RunInternet();
        busy_ = false;
    });
}

std::vector<ServerEntry> ServerSearch::Snapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return servers_;
}

std::string ServerSearch::Message() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return message_;
}

void ServerSearch::SetMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    message_ = msg;
}

void ServerSearch::Publish(std::vector<ServerEntry> list, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    servers_ = std::move(list);
    message_ = msg;
}

void ServerSearch::RunLan() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        servers_.clear();
    }
    int fd = OpenUdp(true);
    if (fd < 0) { SetMessage("Could not open a network socket."); return; }

    const std::set<std::string> local = LocalAddresses();
    const auto targets = BroadcastTargets(kServerPort);
    const std::string pkt = BuildPacket(kReportStatus);
    std::vector<ServerEntry> found;

    auto start = Clock::now();
    long next_send = 0, last_send = 0;
    while (!cancel_ && ElapsedMs(start) < 1800) {
        if (ElapsedMs(start) >= next_send && next_send <= 600) {
            for (const auto& t : targets)
                sendto(fd, pkt.data(), pkt.size(), 0, reinterpret_cast<const sockaddr*>(&t), sizeof t);
            last_send = ElapsedMs(start);
            next_send += 600;
        }
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, 50) <= 0) continue;
        char buf[8192];
        sockaddr_in from{};
        socklen_t flen = sizeof from;
        int n = int(recvfrom(fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &flen));
        ServerEntry e;
        if (n <= 0 || !ParseStatusReply(buf, n, kReportStatus, &e)) continue;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        e.ip = local.count(ip) ? "127.0.0.1" : ip;   // same machine answers on several addresses
        e.port = ntohs(from.sin_port);
        e.host = (e.ip == "127.0.0.1") ? "localhost" : e.ip;
        e.ping_ms = int(ElapsedMs(start) - last_send);
        bool dup = std::any_of(found.begin(), found.end(),
                               [&](const ServerEntry& o) { return o.ip == e.ip && o.port == e.port; });
        if (dup) continue;
        found.push_back(e);
        Publish(found, "Searching the local network ...");
    }
    close(fd);
    std::sort(found.begin(), found.end(), [](const ServerEntry& a, const ServerEntry& b) { return a.ping_ms < b.ping_ms; });
    Publish(found, found.empty() ? "No servers found on the local network."
                                 : std::to_string(found.size()) + " server(s) found.");
}

void ServerSearch::RunInternet() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        servers_.clear();
    }
    const char* metas[] = {META_HOST, META_HOST_TWO};
    std::vector<ServerEntry> found;
    std::string errors;
    int reachable = 0;
    for (const char* meta : metas) {
        if (cancel_) return;
        std::string body, err;
        if (!FetchAll(meta, kMetaProgPort, &body, &err, cancel_)) {
            errors += (errors.empty() ? "" : "; ") + err;
            continue;
        }
        reachable++;
        size_t b = 0;
        while (b < body.size()) {
            size_t e = body.find('\n', b);
            std::string line = body.substr(b, e == std::string::npos ? std::string::npos : e - b);
            b = e == std::string::npos ? body.size() : e + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            ServerEntry s;
            if (!ParseMetaLine(line, &s)) continue;
            bool dup = std::any_of(found.begin(), found.end(),
                                   [&](const ServerEntry& o) { return o.ip == s.ip && o.port == s.port; });
            if (!dup) found.push_back(s);
        }
    }
    if (cancel_) return;
    if (!reachable) { Publish({}, "Could not contact any meta server (" + errors + ")."); return; }

    Publish(found, "Measuring ping ...");
    PingServers(found, cancel_);
    std::sort(found.begin(), found.end(), [](const ServerEntry& a, const ServerEntry& b) {
        int pa = a.ping_ms < 0 ? 1 << 30 : a.ping_ms, pb = b.ping_ms < 0 ? 1 << 30 : b.ping_ms;
        return pa < pb;   // no answer last
    });
    Publish(found, found.empty() ? "The meta server lists no servers."
                                 : std::to_string(found.size()) + " server(s) listed.");
}
