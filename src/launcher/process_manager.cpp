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
#ifdef _WIN32
#include <map>
#include <io.h>
#include <winsock2.h>
#include <windows.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

ProcessManager::ProcessManager() {
#ifdef SIGPIPE
    // Writing to a pipe whose reader died must not kill the launcher.
    signal(SIGPIPE, SIG_IGN);
#endif
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
#ifdef _WIN32
    localtime_s(&tm_now, &t);
#else
    localtime_r(&t, &tm_now);
#endif
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

#ifdef _WIN32

// Children are tracked by process id; the handles live here. Each child runs in its own job
// object, so terminating it also ends anything it spawned, and closing the launcher (which
// closes the job handles) kills leftover children.
struct WinChild {
    HANDLE process = nullptr;
    HANDLE job = nullptr;
};
std::map<pid_t, WinChild> g_children;

// Quotes one argument following the MSVCRT/CommandLineToArgvW rules.
void AppendQuotedArg(std::string& cmd, const std::string& arg) {
    if (!cmd.empty()) cmd += ' ';
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        cmd += arg;
        return;
    }
    cmd += '"';
    size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++backslashes;
        } else if (c == '"') {
            cmd.append(backslashes * 2 + 1, '\\');
            cmd += '"';
            backslashes = 0;
        } else {
            cmd.append(backslashes, '\\');
            cmd += c;
            backslashes = 0;
        }
    }
    cmd.append(backslashes * 2, '\\');
    cmd += '"';
}

// Starts bin; returns its process id (or -1) and a CRT fd for the read end of a stdout pipe
// (stderr too unless discard_stderr). CreateProcess appends ".exe" and searches the launcher's
// directory first.
pid_t Spawn(const std::string& bin, const std::vector<std::string>& args, int* read_fd,
            bool discard_stderr = false) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0); // only the write end goes to the child
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, nullptr);

    std::string cmd;
    AppendQuotedArg(cmd, bin);
    for (const auto& a : args) AppendQuotedArg(cmd, a);
    std::vector<char> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back('\0');

    // Pass exactly these handles: the launcher's other pipes must not leak into the child.
    HANDLE inherit[2] = {wr, nul};
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<char> attr_buf(attr_size);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
    bool have_attrs = InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) &&
                      UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                                (nul != INVALID_HANDLE_VALUE ? 2 : 1) * sizeof(HANDLE),
                                                nullptr, nullptr);

    STARTUPINFOEXA si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : nullptr;
    si.StartupInfo.hStdOutput = wr;
    si.StartupInfo.hStdError = (discard_stderr && nul != INVALID_HANDLE_VALUE) ? nul : wr;
    si.lpAttributeList = have_attrs ? attrs : nullptr;

    // CREATE_NO_WINDOW: the console server gets a hidden console instead of a popup window.
    // CREATE_NEW_PROCESS_GROUP: Terminate() can send it Ctrl+Break without hitting us.
    // CREATE_SUSPENDED: it joins its job before it can spawn anything.
    DWORD flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED |
                  (have_attrs ? EXTENDED_STARTUPINFO_PRESENT : 0);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr,
                             &si.StartupInfo, &pi);
    if (have_attrs) DeleteProcThreadAttributeList(attrs);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        return -1;
    }

    WinChild child;
    child.process = pi.hProcess;
    child.job = CreateJobObjectA(nullptr, nullptr);
    if (child.job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(child.job, JobObjectExtendedLimitInformation, &li, sizeof(li));
        AssignProcessToJobObject(child.job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    pid_t pid = static_cast<pid_t>(pi.dwProcessId);
    g_children[pid] = child;
    *read_fd = _open_osfhandle(reinterpret_cast<intptr_t>(rd), _O_RDONLY | _O_BINARY);
    if (*read_fd < 0) CloseHandle(rd);
    return pid;
}

void ReleaseChild(pid_t pid) {
    auto it = g_children.find(pid);
    if (it == g_children.end()) return;
    CloseHandle(it->second.process);
    if (it->second.job) CloseHandle(it->second.job);
    g_children.erase(it);
}

// Non-blocking: true once the child ended (exit code in *code); releases its handles.
bool Reap(pid_t pid, int* code) {
    auto it = g_children.find(pid);
    if (it == g_children.end()) {
        *code = -1;
        return true;
    }
    if (WaitForSingleObject(it->second.process, 0) != WAIT_OBJECT_0) return false;
    DWORD exit_code = 0;
    GetExitCodeProcess(it->second.process, &exit_code);
    *code = static_cast<int>(exit_code);
    ReleaseChild(pid);
    return true;
}

BOOL CALLBACK CloseWindowsOf(HWND hwnd, LPARAM lparam) {
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner == static_cast<DWORD>(lparam)) PostMessageA(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

// Asks a child to quit (WM_CLOSE for the SDL client, Ctrl+Break for the console server),
// short grace period, then kills its job.
void Terminate(pid_t pid) {
    auto it = g_children.find(pid);
    if (it == g_children.end()) return;
    EnumWindows(CloseWindowsOf, static_cast<LPARAM>(pid));
    if (AttachConsole(static_cast<DWORD>(pid))) {
        SetConsoleCtrlHandler(nullptr, TRUE); // the launcher itself ignores the event
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(pid));
        FreeConsole();
        SetConsoleCtrlHandler(nullptr, FALSE);
    }
    if (WaitForSingleObject(it->second.process, 1000) != WAIT_OBJECT_0) {
        if (it->second.job) TerminateJobObject(it->second.job, 1);
        else TerminateProcess(it->second.process, 1);
        WaitForSingleObject(it->second.process, 2000);
    }
    ReleaseChild(pid);
}

// Reads what is available without blocking; <= 0 when the pipe is empty or closed.
ssize_t ReadAvailable(int fd, char* buf, size_t size) {
    DWORD avail = 0;
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE || !PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) ||
        avail == 0)
        return 0;
    return _read(fd, buf, static_cast<unsigned>(std::min<size_t>(size, avail)));
}

#else // POSIX

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

// Non-blocking: true once the child ended (exit code in *code, -1 if killed by a signal).
bool Reap(pid_t pid, int* code) {
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) <= 0) return false;
    *code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return true;
}

ssize_t ReadAvailable(int fd, char* buf, size_t size) {
    return read(fd, buf, size);
}

#endif

std::string Describe(const std::string& bin, const std::vector<std::string>& args) {
    std::ostringstream ss;
    ss << bin;
    for (const auto& a : args) ss << " " << a;
    return ss.str();
}

} // namespace

std::string RunAndCapture(const std::string& bin, const std::vector<std::string>& args) {
    std::string out;
    char buf[4096];
#ifdef _WIN32
    int fd = -1;
    pid_t pid = Spawn(bin, args, &fd, true);
    if (pid < 0) return out;
    if (fd >= 0) {
        int n;
        while ((n = _read(fd, buf, sizeof(buf))) > 0) out.append(buf, n); // EOF when it exits
        _close(fd);
    }
    auto it = g_children.find(pid);
    if (it != g_children.end()) WaitForSingleObject(it->second.process, 5000);
    int code;
    if (!Reap(pid, &code)) Terminate(pid);
#else
    auto quote = [](const std::string& s) {
        std::string q = "'";
        for (char c : s) q += (c == '\'') ? std::string("'\\''") : std::string(1, c);
        return q + "'";
    };
    std::string cmd = quote(bin);
    for (const auto& a : args) cmd += " " + quote(a);
    cmd += " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
#endif
    return out;
}

bool IsUdpPortFree(int port) {
#ifdef _WIN32
    static const bool wsa_ok = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
    if (!wsa_ok) return false;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) return false;
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    int res = bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return res == 0;
}

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
        ReadFromFd(server_read_fd_, server_line_buf_, "SERVER"); // drain shutdown output
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
        ssize_t n = ReadAvailable(fd, buffer, sizeof(buffer));
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
        int code = 0;
        if (Reap(server_pid_, &code)) {
            ReadFromFd(server_read_fd_, server_line_buf_, "SERVER"); // drain
            close(server_read_fd_);
            server_read_fd_ = -1;
            server_running_ = false;
            server_ready_ = false;
            server_pid_ = -1;
            players_.clear();
            server_exit_code_ = code;
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
        int code = 0;
        if (Reap(client_pid_, &code)) {
            ReadFromFd(client_read_fd_, client_line_buf_, "CLIENT"); // drain
            close(client_read_fd_);
            client_read_fd_ = -1;
            client_running_ = false;
            client_pid_ = -1;
            client_exit_code_ = code;
            client_exited_ = true;
            AddLog("LAUNCHER", "Client exited with code " + std::to_string(client_exit_code_));
        }
    }
}
