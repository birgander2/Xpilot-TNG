#include "process_manager.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

ProcessManager::ProcessManager() {
    // Writing to a pipe whose reader died must not kill the launcher.
    signal(SIGPIPE, SIG_IGN);
    AddLog("LAUNCHER", "Process manager ready.");
}

ProcessManager::~ProcessManager() {
    StopClient();
    StopServer();
}

std::string ProcessManager::CurrentTimeStr() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&t, &tm_now);
    std::ostringstream ss;
    ss << std::put_time(&tm_now, "%H:%M:%S");
    return ss.str();
}

void ProcessManager::AddLog(const std::string& tag, const std::string& text) {
    if (text.empty()) return;
    logs_.push_back({CurrentTimeStr(), tag, text});
    if (logs_.size() > 2000) logs_.erase(logs_.begin(), logs_.begin() + 200);
}

int ProcessManager::HumanCount() const {
    int n = 0;
    for (const auto& p : players_)
        if (p.kind == "player") ++n;
    return n;
}

// --- spawning --------------------------------------------------------------

namespace {

// Forks and execs; returns pid (or -1) and the read end of a combined stdout/stderr pipe.
pid_t Spawn(const std::string& bin, const std::vector<std::string>& args, int* read_fd) {
    // O_CLOEXEC: the child must not inherit the launcher's other pipes. dup2() below clears it
    // on stdout/stderr only.
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) return -1;

    // Everything that allocates happens before fork(): the launcher has search threads, so the
    // child may only call async-signal-safe functions.
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(bin.c_str()));
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0); // own process group so Stop can signal the whole tree
        signal(SIGPIPE, SIG_DFL); // the launcher ignores it, the children should not inherit that
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        execvp(bin.c_str(), argv.data());
        static const char msg[] = "execvp failed\n";
        ssize_t ignored = write(STDERR_FILENO, msg, sizeof msg - 1);
        (void)ignored;
        _exit(127);
    }
    setpgid(pid, pid); // also from the parent side, so Terminate() cannot race the child
    close(pipefd[1]);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    *read_fd = pipefd[0];
    return pid;
}

std::string Describe(const std::string& bin, const std::vector<std::string>& args) {
    std::ostringstream ss;
    ss << bin;
    for (const auto& a : args) ss << " " << a;
    return ss.str();
}

// Terminates a child: SIGTERM, short grace period, then SIGKILL.
void Terminate(pid_t pid) {
    kill(-pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        int status;
        if (waitpid(pid, &status, WNOHANG) != 0) return;
        usleep(50000);
    }
    kill(-pid, SIGKILL);
    int status;
    waitpid(pid, &status, 0);
}

} // namespace

bool ProcessManager::StartServer(const std::string& binary_path, const std::vector<std::string>& args) {
    if (server_running_) {
        AddLog("LAUNCHER", "Server is already running.");
        return false;
    }
    int fd = -1;
    pid_t pid = Spawn(binary_path, args, &fd);
    if (pid < 0) {
        AddLog("LAUNCHER", "Failed to start server process.");
        return false;
    }
    server_read_fd_ = fd;
    server_pid_ = pid;
    server_running_ = true;
    server_ready_ = false;
    server_exited_ = false;
    server_line_buf_.clear();
    players_.clear();
    AddLog("LAUNCHER", "Server started (PID " + std::to_string(pid) + "): " + Describe(binary_path, args));
    return true;
}

void ProcessManager::StopServer() {
    pending_client_launch_ = false;
    if (!server_running_ || server_pid_ <= 0) return;
    AddLog("LAUNCHER", "Stopping server (PID " + std::to_string(server_pid_) + ")...");
    Terminate(server_pid_);
    if (server_read_fd_ >= 0) {
        close(server_read_fd_);
        server_read_fd_ = -1;
    }
    server_running_ = false;
    server_ready_ = false;
    server_pid_ = -1;
    players_.clear();
    AddLog("LAUNCHER", "Server stopped.");
}

bool ProcessManager::StartClient(const std::string& binary_path, const std::vector<std::string>& args) {
    if (client_running_) {
        AddLog("LAUNCHER", "Client is already running.");
        return false;
    }
    int fd = -1;
    pid_t pid = Spawn(binary_path, args, &fd);
    if (pid < 0) {
        AddLog("LAUNCHER", "Failed to start client process.");
        return false;
    }
    client_read_fd_ = fd;
    client_pid_ = pid;
    client_running_ = true;
    client_exited_ = false;
    client_line_buf_.clear();
    AddLog("LAUNCHER", "Client started (PID " + std::to_string(pid) + "): " + Describe(binary_path, args));
    return true;
}

void ProcessManager::StopClient() {
    if (!client_running_ || client_pid_ <= 0) return;
    AddLog("LAUNCHER", "Stopping client (PID " + std::to_string(client_pid_) + ")...");
    Terminate(client_pid_);
    if (client_read_fd_ >= 0) {
        close(client_read_fd_);
        client_read_fd_ = -1;
    }
    client_running_ = false;
    client_pid_ = -1;
    AddLog("LAUNCHER", "Client stopped.");
}

void ProcessManager::LaunchLocalGame(const std::string& server_bin, const std::vector<std::string>& server_args,
                                     const std::string& client_bin, const std::vector<std::string>& client_args) {
    if (client_running_) StopClient();
    if (server_running_) {
        AddLog("LAUNCHER", "Stopping previous server before starting a new local game...");
        StopServer();
    }
    launch_failed_ = false;
    if (!StartServer(server_bin, server_args)) {
        launch_failed_ = true;
        return;
    }
    pending_client_launch_ = true;
    launch_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    pending_client_bin_ = client_bin;
    pending_client_args_ = client_args;
    AddLog("LAUNCHER", "Waiting for the server to become ready...");
}

void ProcessManager::CancelLaunch() {
    pending_client_launch_ = false;
}

// --- output handling -------------------------------------------------------

void ProcessManager::ParseServerLine(const std::string& line) {
    if (line.find("Server runs at") != std::string::npos) {
        server_ready_ = true;
        return;
    }
    if (line.rfind("[join] ", 0) == 0) {
        // [join] name|host|kind|team|id
        std::vector<std::string> f;
        std::stringstream ss(line.substr(7));
        std::string part;
        while (std::getline(ss, part, '|')) f.push_back(part);
        if (f.size() >= 5) {
            PlayerInfo p;
            p.name = f[0];
            p.host = f[1];
            p.kind = f[2];
            try { p.team = std::stoi(f[3]); p.id = std::stoi(f[4]); } catch (...) { return; }
            players_.erase(std::remove_if(players_.begin(), players_.end(),
                           [&](const PlayerInfo& o) { return o.id == p.id; }), players_.end());
            players_.push_back(p);
        }
        return;
    }
    if (line.rfind("[leave] ", 0) == 0) {
        // [leave] name|id
        size_t bar = line.rfind('|');
        if (bar != std::string::npos) {
            try {
                int id = std::stoi(line.substr(bar + 1));
                players_.erase(std::remove_if(players_.begin(), players_.end(),
                               [&](const PlayerInfo& o) { return o.id == id; }), players_.end());
            } catch (...) {}
        }
    }
}

void ProcessManager::ReadFromFd(int fd, std::string& line_buf, const std::string& tag) {
    if (fd < 0) return;
    char buffer[2048];
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i) {
            char c = buffer[i];
            if (c == '\n' || c == '\r') {
                if (!line_buf.empty()) {
                    AddLog(tag, line_buf);
                    if (tag == "SERVER") ParseServerLine(line_buf);
                    line_buf.clear();
                }
            } else {
                line_buf.push_back(c);
            }
        }
    }
}

bool ProcessManager::ConsumeClientExited(int* exit_code) {
    if (!client_exited_) return false;
    client_exited_ = false;
    if (exit_code) *exit_code = client_exit_code_;
    return true;
}

bool ProcessManager::ConsumeServerExited(int* exit_code) {
    if (!server_exited_) return false;
    server_exited_ = false;
    if (exit_code) *exit_code = server_exit_code_;
    return true;
}

bool ProcessManager::ConsumeLaunchFailed() {
    bool v = launch_failed_;
    launch_failed_ = false;
    return v;
}

void ProcessManager::Update() {
    // Server
    if (server_running_ && server_pid_ > 0) {
        ReadFromFd(server_read_fd_, server_line_buf_, "SERVER");
        int status = 0;
        if (waitpid(server_pid_, &status, WNOHANG) > 0) {
            ReadFromFd(server_read_fd_, server_line_buf_, "SERVER"); // drain
            close(server_read_fd_);
            server_read_fd_ = -1;
            server_running_ = false;
            server_ready_ = false;
            server_pid_ = -1;
            players_.clear();
            server_exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            server_exited_ = true;
            AddLog("LAUNCHER", "Server exited with code " + std::to_string(server_exit_code_));
            if (pending_client_launch_) {
                pending_client_launch_ = false;
                launch_failed_ = true;
                AddLog("LAUNCHER", "Local game aborted: the server did not start (see log).");
            }
        }
    }

    // Pending local launch: start the client once the server is ready.
    if (pending_client_launch_) {
        if (server_ready_) {
            pending_client_launch_ = false;
            if (!StartClient(pending_client_bin_, pending_client_args_)) launch_failed_ = true;
        } else if (std::chrono::steady_clock::now() > launch_deadline_) {
            pending_client_launch_ = false;
            launch_failed_ = true;
            AddLog("LAUNCHER", "Timeout: the server did not become ready within 15 s.");
        }
    }

    // Client
    if (client_running_ && client_pid_ > 0) {
        ReadFromFd(client_read_fd_, client_line_buf_, "CLIENT");
        int status = 0;
        if (waitpid(client_pid_, &status, WNOHANG) > 0) {
            ReadFromFd(client_read_fd_, client_line_buf_, "CLIENT"); // drain
            close(client_read_fd_);
            client_read_fd_ = -1;
            client_running_ = false;
            client_pid_ = -1;
            client_exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            client_exited_ = true;
            AddLog("LAUNCHER", "Client exited with code " + std::to_string(client_exit_code_));
        }
    }
}
