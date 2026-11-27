/* Mistbar - MIT License */

#include "common/ipc.h"
#include "common/logger.h"
#include "common/xdg.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace mistbar::ipc {

Server::~Server() {
    stop();
}

bool Server::start(const std::string& socket_path, Handler handler) {
    stop();
    socket_path_ = socket_path;
    handler_ = std::move(handler);

    if (socket_path_.size() >= sizeof(sockaddr_un::sun_path)) {
        log::error("ipc", "Socket path exceeds sun_path limit: " + socket_path_);
        return false;
    }

    const auto slash = socket_path_.rfind('/');
    if (slash != std::string::npos) {
        xdg::ensure_dir(socket_path_.substr(0, slash));
    }

    ::unlink(socket_path_.c_str());

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        log::error("ipc", std::string("socket() failed: ") + std::strerror(errno));
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log::error("ipc", std::string("bind() failed: ") + std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, 16) < 0) {
        log::error("ipc", std::string("listen() failed: ") + std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(socket_path_.c_str());
        return false;
    }

    return true;
}

void Server::stop() {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!socket_path_.empty()) {
        ::unlink(socket_path_.c_str());
        socket_path_.clear();
    }
}

void Server::poll_once() {
    if (listen_fd_ < 0) {
        return;
    }
    while (true) {
        int client_fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            log::warn("ipc", std::string("accept4() error: ") + std::strerror(errno));
            break;
        }

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        std::string req;
        char buf[1024];
        while (req.size() < 65536) {
            const ssize_t n = ::read(client_fd, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            req.append(buf, static_cast<std::size_t>(n));
            if (req.find('\n') != std::string::npos) {
                break;
            }
        }
        while (!req.empty() && (req.back() == '\n' || req.back() == '\r')) {
            req.pop_back();
        }

        std::string reply = handler_ ? handler_(req) : "ok";
        reply.push_back('\n');

        std::size_t sent = 0;
        while (sent < reply.size()) {
            const ssize_t n = ::send(client_fd, reply.data() + sent, reply.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) {
                break;
            }
            sent += static_cast<std::size_t>(n);
        }
        ::close(client_fd);
    }
}

std::string default_socket_path() {
    return xdg::runtime_dir() + "/bar.sock";
}

bool send_command(const std::string& socket_path, const std::string& command, std::string* reply_out) {
    if (socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        return false;
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }

    timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return false;
    }

    std::string payload = command;
    if (payload.empty() || payload.back() != '\n') {
        payload.push_back('\n');
    }

    std::size_t sent = 0;
    while (sent < payload.size()) {
        const ssize_t n = ::send(fd, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }

    if (reply_out) {
        reply_out->clear();
        char buf[1024];
        while (true) {
            const ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            reply_out->append(buf, static_cast<std::size_t>(n));
            if (reply_out->find('\n') != std::string::npos) {
                break;
            }
        }
        while (!reply_out->empty() && (reply_out->back() == '\n' || reply_out->back() == '\r')) {
            reply_out->pop_back();
        }
    }

    ::close(fd);
    return true;
}

} // namespace mistbar::ipc
