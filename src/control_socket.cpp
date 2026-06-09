#include "control_socket.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

ControlSocket::~ControlSocket() {
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
    if (!path_.empty())
        unlink(path_.c_str());
}

bool ControlSocket::init() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    path_ = xdg ? std::string(xdg) + "/myshell.sock" : "/tmp/myshell.sock";

    unlink(path_.c_str()); // clean up stale socket from previous crash

    fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        fprintf(stderr, "control socket: socket() failed: %s\n", strerror(errno));
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "control socket: bind(%s) failed: %s\n", path_.c_str(), strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }

    if (listen(fd_, 8) < 0) {
        fprintf(stderr, "control socket: listen() failed: %s\n", strerror(errno));
        close(fd_);
        fd_ = -1;
        unlink(path_.c_str());
        return false;
    }

    fprintf(stderr, "control socket: listening at %s\n", path_.c_str());
    return true;
}

void ControlSocket::dispatch(const std::function<void(std::string_view)>& handler) {
    int client = accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client < 0)
        return;

    char buf[256];
    ssize_t n = read(client, buf, sizeof(buf) - 1);
    close(client);

    if (n <= 0)
        return;

    buf[n] = '\0';
    // Trim trailing whitespace and newlines
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
        buf[--n] = '\0';

    if (n > 0)
        handler(std::string_view(buf, n));
}
