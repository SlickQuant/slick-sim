#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slick::sim::integration {

/// A child process with stdin on a pipe and stdout/stderr redirected to files. Tests tail the
/// output files, which retain diagnostics after a crash. This also avoids polling two extra pipes
/// to prevent a full output buffer from blocking the child.
///
/// The destructor terminates a running child so failed assertions cannot leave a simulator holding
/// ports between runs.
class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;

    bool start(const std::string& executable,
               const std::vector<std::string>& arguments,
               const std::string& working_directory,
               const std::string& stdout_path,
               const std::string& stderr_path,
               std::string& error);

    bool running();
    /// False once the child has closed its stdin (or exited).
    bool write_line(std::string_view line);
    /// Waits up to `timeout`; nullopt means it is still running.
    std::optional<int> wait_for_exit(std::chrono::milliseconds timeout);
    int exit_code() const { return exit_code_; }
    bool exited() const { return exited_; }
    uint32_t pid() const { return pid_; }
    /// Kills the child if it is still running, then reaps it.
    void terminate();

private:
    void release();

#if defined(_WIN32)
    void* process_ = nullptr;       ///< HANDLE
    void* stdin_write_ = nullptr;   ///< HANDLE
#else
    int stdin_fd_ = -1;
#endif
    uint32_t pid_ = 0;
    int exit_code_ = -1;
    bool exited_ = false;
};

/// Binds port 0 on the loopback interface, reads the assigned port, then closes the socket. The
/// port is not reserved; the caller starts its listener immediately.
uint16_t free_tcp_port();

/// Whether anything is accepting connections on 127.0.0.1:`port`.
bool port_is_open(uint16_t port, std::chrono::milliseconds timeout);

/// Polls `port_is_open` until it answers or `timeout` elapses.
bool wait_for_port(uint16_t port, std::chrono::milliseconds timeout);

std::string read_file(const std::string& path);

}  // namespace slick::sim::integration
