#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <sys/types.h>

struct ProcessLogEntry {
    std::string timestamp;
    std::string tag; // "SERVER", "CLIENT" or "LAUNCHER"
    std::string text;
};

struct PlayerInfo {
    int id = -1;
    std::string name;
    std::string host;
    std::string kind; // "player", "spectator" or "bot"
    int team = 0;
};

// Runs bin with args to completion and returns its stdout (stderr is discarded).
// No shell is involved on Windows, so no console window pops up.
std::string RunAndCapture(const std::string& bin, const std::vector<std::string>& args);

// True if a UDP socket can be bound to the port (no local server uses it).
bool IsUdpPortFree(int port);

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    // --- Server -----------------------------------------------------------
    bool StartServer(const std::string& binary_path, const std::vector<std::string>& args);
    void StopServer();
    bool IsServerRunning() const { return server_running_; }
    // True once the server printed "Server runs at ..." (it accepts clients).
    bool IsServerReady() const { return server_ready_; }
    const std::vector<PlayerInfo>& GetPlayers() const { return players_; }
    int HumanCount() const;

    // --- Client -----------------------------------------------------------
    bool StartClient(const std::string& binary_path, const std::vector<std::string>& args);
    void StopClient();
    bool IsClientRunning() const { return client_running_; }

    // Starts the server, waits until it is ready, then starts the client.
    void LaunchLocalGame(const std::string& server_bin, const std::vector<std::string>& server_args,
                         const std::string& client_bin, const std::vector<std::string>& client_args);
    void CancelLaunch();

    // Call once per frame.
    void Update();

    // One-shot events, true exactly once after the process ended.
    bool ConsumeClientExited(int* exit_code = nullptr);
    bool ConsumeServerExited(int* exit_code = nullptr);
    // Set when a pending local launch failed (server died / timeout).
    bool ConsumeLaunchFailed();

    const std::vector<ProcessLogEntry>& GetLogs() const { return logs_; }
    void ClearLogs() { logs_.clear(); }
    void AddLog(const std::string& tag, const std::string& text);

private:
    void ReadFromFd(int fd, std::string& line_buf, const std::string& tag);
    void ParseServerLine(const std::string& line);
    void ReapAll();
    static std::string CurrentTimeStr();

    pid_t server_pid_ = -1;
    int server_read_fd_ = -1;
    bool server_running_ = false;
    bool server_ready_ = false;
    std::string server_line_buf_;
    std::vector<PlayerInfo> players_;

    pid_t client_pid_ = -1;
    int client_read_fd_ = -1;
    bool client_running_ = false;
    std::string client_line_buf_;

    std::vector<ProcessLogEntry> logs_;

    bool pending_client_launch_ = false;
    std::chrono::steady_clock::time_point launch_deadline_;
    std::string pending_client_bin_;
    std::vector<std::string> pending_client_args_;

    bool client_exited_ = false;
    int client_exit_code_ = 0;
    bool server_exited_ = false;
    int server_exit_code_ = 0;
    bool launch_failed_ = false;
};
