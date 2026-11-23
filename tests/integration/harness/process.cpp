#include "process.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace slick::sim::integration {
namespace {

#if defined(_WIN32)
std::string quoteArgument(const std::string& argument) {
    if (!argument.empty() && argument.find_first_of(" \t\"") == std::string::npos) {
        return argument;
    }
    std::string quoted = "\"";
    for (const char c : argument) {
        if (c == '"') {
            quoted += "\\\"";
        } else {
            quoted += c;
        }
    }
    quoted += '"';
    return quoted;
}

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
    wide.pop_back();
    return wide;
}

void ensureWinsock() {
    static const bool ready = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)ready;
}
#else
std::string quoteArgument(const std::string& argument) {
    return argument;
}
#endif
}  // namespace

ChildProcess::~ChildProcess() { terminate(); }

ChildProcess::ChildProcess(ChildProcess&& other) noexcept {
    *this = std::move(other);
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        terminate();
    }
#if defined(_WIN32)
    process_ = other.process_;
    stdin_write_ = other.stdin_write_;
    other.process_ = nullptr;
    other.stdin_write_ = nullptr;
#else
    stdin_fd_ = other.stdin_fd_;
    other.stdin_fd_ = -1;
#endif
    pid_ = other.pid_;
    exit_code_ = other.exit_code_;
    exited_ = other.exited_;
    other.pid_ = 0;
    other.exit_code_ = -1;
    other.exited_ = true;
    return *this;
}

#if defined(_WIN32)
bool ChildProcess::start(const std::string& executable,
                         const std::vector<std::string>& arguments,
                         const std::string& working_directory,
                         const std::string& stdout_path,
                         const std::string& stderr_path,
                         std::string& error) {
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;
    if (!CreatePipe(&stdin_read, &stdin_write, &inheritable, 0)) {
        error = "CreatePipe failed";
        return false;
    }
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    HANDLE out = CreateFileW(widen(stdout_path).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE err = CreateFileW(widen(stderr_path).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE || err == INVALID_HANDLE_VALUE) {
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        error = "Cannot create the child's output files";
        return false;
    }

    std::string command_line = quoteArgument(executable);
    for (const auto& argument : arguments) {
        command_line += ' ';
        command_line += quoteArgument(argument);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = out;
    startup.hStdError = err;
    PROCESS_INFORMATION info{};

    std::wstring wide_command_line = widen(command_line);
    std::wstring wide_working_directory = widen(working_directory);
    const BOOL created = CreateProcessW(nullptr, wide_command_line.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr,
                                        wide_working_directory.empty() ? nullptr : wide_working_directory.c_str(),
                                        &startup, &info);
    CloseHandle(stdin_read);
    CloseHandle(out);
    CloseHandle(err);
    if (!created) {
        CloseHandle(stdin_write);
        error = "CreateProcess failed for " + executable + " (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(info.hThread);
    process_ = info.hProcess;
    stdin_write_ = stdin_write;
    pid_ = info.dwProcessId;
    return true;
}
bool ChildProcess::running() {
    if (exited_) {
        return false;
    }
    return WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_TIMEOUT;
}

bool ChildProcess::write_line(std::string_view line) {
    if (stdin_write_ == nullptr) {
        return false;
    }
    std::string data(line);
    data += '\n';
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(stdin_write_), data.data(), static_cast<DWORD>(data.size()), &written,
                   nullptr)) {
        return false;
    }
    return written == data.size();
}

std::optional<int> ChildProcess::wait_for_exit(std::chrono::milliseconds timeout) {
    if (exited_) {
        return exit_code_;
    }
    if (WaitForSingleObject(static_cast<HANDLE>(process_), static_cast<DWORD>(timeout.count())) != WAIT_OBJECT_0) {
        return std::nullopt;
    }
    DWORD code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(process_), &code);
    exit_code_ = static_cast<int>(code);
    exited_ = true;
    release();
    return exit_code_;
}

void ChildProcess::release() {
    if (stdin_write_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(stdin_write_));
        stdin_write_ = nullptr;
    }
    if (process_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
}

void ChildProcess::terminate() {
    if (process_ == nullptr) {
        return;
    }
    if (!exited_ && WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_TIMEOUT) {
        TerminateProcess(static_cast<HANDLE>(process_), 1);
        WaitForSingleObject(static_cast<HANDLE>(process_), 5000);
    }
    DWORD code = 0;
    if (!exited_ && GetExitCodeProcess(static_cast<HANDLE>(process_), &code)) {
        exit_code_ = static_cast<int>(code);
    }
    exited_ = true;
    release();
}

#else   // POSIX
bool ChildProcess::start(const std::string& executable,
                         const std::vector<std::string>& arguments,
                         const std::string& working_directory,
                         const std::string& stdout_path,
                         const std::string& stderr_path,
                         std::string& error) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        error = "pipe failed";
        return false;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        error = "fork failed";
        return false;
    }
    if (pid == 0) {
        // Child: only async-signal-safe calls before exec.
        dup2(pipe_fds[0], 0);
        const int out = open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        const int err = open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out >= 0) dup2(out, 1);
        if (err >= 0) dup2(err, 2);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        if (out > 2) close(out);
        if (err > 2) close(err);
        if (!working_directory.empty() && chdir(working_directory.c_str()) != 0) {
            _exit(126);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const auto& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    close(pipe_fds[0]);
    stdin_fd_ = pipe_fds[1];
    pid_ = static_cast<uint32_t>(pid);
    return true;
}

bool ChildProcess::running() {
    if (exited_) {
        return false;
    }
    int status = 0;
    if (waitpid(static_cast<pid_t>(pid_), &status, WNOHANG) == 0) {
        return true;
    }
    exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    exited_ = true;
    release();
    return false;
}

bool ChildProcess::write_line(std::string_view line) {
    if (stdin_fd_ < 0) {
        return false;
    }
    std::string data(line);
    data += '\n';
    size_t written = 0;
    while (written < data.size()) {
        const ssize_t count = write(stdin_fd_, data.data() + written, data.size() - written);
        if (count <= 0) {
            return false;
        }
        written += static_cast<size_t>(count);
    }
    return true;
}

std::optional<int> ChildProcess::wait_for_exit(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (exited_) {
            return exit_code_;
        }
        int status = 0;
        const pid_t result = waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
        if (result == static_cast<pid_t>(pid_)) {
            exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            exited_ = true;
            release();
            return exit_code_;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void ChildProcess::release() {
    if (stdin_fd_ >= 0) {
        close(stdin_fd_);
        stdin_fd_ = -1;
    }
}

void ChildProcess::terminate() {
    if (pid_ == 0 || exited_) {
        release();
        return;
    }
    kill(static_cast<pid_t>(pid_), SIGTERM);
    if (!wait_for_exit(std::chrono::milliseconds(1000))) {
        kill(static_cast<pid_t>(pid_), SIGKILL);
        wait_for_exit(std::chrono::milliseconds(1000));
    }
    release();
}

#endif
#if defined(_WIN32)
using socket_handle = SOCKET;
constexpr socket_handle kInvalidSocket = INVALID_SOCKET;
#else
using socket_handle = int;
constexpr socket_handle kInvalidSocket = -1;
#endif

uint16_t free_tcp_port() {
#if defined(_WIN32)
    ensureWinsock();
#endif
    const socket_handle sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
        return 0;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    uint16_t port = 0;
    if (bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
#if defined(_WIN32)
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&address), &length) == 0) {
            port = ntohs(address.sin_port);
        }
    }
#if defined(_WIN32)
    closesocket(sock);
#else
    close(sock);
#endif
    return port;
}

bool port_is_open(uint16_t port, std::chrono::milliseconds /*timeout*/) {
#if defined(_WIN32)
    ensureWinsock();
#endif
    const socket_handle sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    const bool connected = ::connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
#if defined(_WIN32)
    closesocket(sock);
#else
    close(sock);
#endif
    return connected;
}

bool wait_for_port(uint16_t port, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (port_is_open(port, std::chrono::milliseconds(200))) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

}  // namespace slick::sim::integration
