#pragma once
#include <string>
#include <sys/types.h>

enum class PamResult { Pending, Success, AuthFail, Error };

class PamAuth {
public:
    ~PamAuth();
    // Forks a child that runs pam_authenticate; parent returns immediately.
    // resultFd() becomes readable when the child writes a result byte.
    bool authenticateAsync(const std::string& user, const std::string& password);
    int  resultFd() const { return resultFd_; }   // -1 when idle
    PamResult pollResult();                        // reads result, reaps child, closes fds
    void cancel();
private:
    pid_t child_    = -1;
    int   resultFd_ = -1;
};
