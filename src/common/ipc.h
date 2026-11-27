/* Mistbar - MIT License */

#pragma once

#include <functional>
#include <string>

namespace mistbar::ipc {

class Server {
public:
    using Handler = std::function<std::string(const std::string& request)>;

    Server() = default;
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(const std::string& socket_path, Handler handler);
    void stop();

    int fd() const noexcept { return listen_fd_; }
    void poll_once();

private:
    int listen_fd_ = -1;
    std::string socket_path_;
    Handler handler_;
};

std::string default_socket_path();
bool send_command(const std::string& socket_path, const std::string& command, std::string* reply_out = nullptr);

} // namespace mistbar::ipc
