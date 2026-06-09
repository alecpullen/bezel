#pragma once
#include <functional>
#include <string>
#include <string_view>

class ControlSocket {
public:
    ~ControlSocket();
    bool init();
    int  fd() const { return fd_; }
    void dispatch(const std::function<void(std::string_view)>& handler);
private:
    int         fd_   = -1;
    std::string path_;
};
