#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ServerEntry {
    std::string host;      // hostname (meta) or IP (LAN)
    std::string ip;
    int port = 0;
    std::string version;
    std::string map;
    std::string author;
    std::string status;
    int players = 0;
    int bases = 0;         // max players the map has room for
    int teambases = 0;     // bases that belong to a team (0 = no teams)
    int fps = 0;
    int ping_ms = -1;      // -1 = unknown / no answer
};

// Background lookup of servers. LAN: UDP broadcast, Internet: meta server list.
// Start() returns immediately; poll Snapshot() / Busy() from the UI thread.
class ServerSearch {
public:
    enum class Kind { Lan, Internet };

    explicit ServerSearch(Kind kind) : kind_(kind) {}
    ~ServerSearch();

    void Start();
    bool Busy() const { return busy_; }
    // Internet only: meta server entries that did not answer the ping (before the per-IP limit)
    int UnreachableTotal() const { return unreachable_total_; }
    std::vector<ServerEntry> Snapshot() const;
    std::string Message() const;

private:
    void RunLan();
    void RunInternet();
    void Publish(std::vector<ServerEntry> list, const std::string& msg);
    void SetMessage(const std::string& msg);

    Kind kind_;
    std::thread thread_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<int> unreachable_total_{0};
    mutable std::mutex mtx_;
    std::vector<ServerEntry> servers_;
    std::string message_;
};
