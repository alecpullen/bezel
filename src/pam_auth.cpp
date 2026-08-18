#include "pam_auth.hpp"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <security/pam_appl.h>

// Security model: the password travels parent→child over a pipe, never through
// argv, env, or shared memory. The child is short-lived and runs only
// pam_authenticate. The parent never blocks on PAM — it polls resultFd_ in the
// main event loop, so the single-threaded loop is never stalled by an
// unresponsive PAM module.

PamAuth::~PamAuth() {
    cancel();
}

namespace {

struct ConvData {
    const char* password;
};

// PAM conversation: the only prompt we ever expect is for the password. Fill
// pam_response with the in-memory password from ConvData.
int convFn(int num_msg, const struct pam_message** msg,
           struct pam_response** resp, void* appdata_ptr) {
    auto* data = static_cast<ConvData*>(appdata_ptr);
    if (num_msg <= 0 || !msg || !resp) return PAM_CONV_ERR;

    auto* replies = static_cast<struct pam_response*>(
        calloc(num_msg, sizeof(struct pam_response)));
    if (!replies) return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; ++i) {
        int style = msg[i]->msg_style;
        if (style == PAM_PROMPT_ECHO_OFF || style == PAM_PROMPT_ECHO_ON) {
            replies[i].resp = strdup(data->password);
            if (!replies[i].resp) {
                for (int j = 0; j < i; ++j) free(replies[j].resp);
                free(replies);
                return PAM_BUF_ERR;
            }
        } else if (style == PAM_ERROR_MSG || style == PAM_TEXT_INFO) {
            replies[i].resp = nullptr;
        } else {
            for (int j = 0; j < i; ++j) free(replies[j].resp);
            free(replies);
            return PAM_CONV_ERR;
        }
    }
    *resp = replies;
    return PAM_SUCCESS;
}

} // namespace

bool PamAuth::authenticateAsync(const std::string& user, const std::string& password) {
    cancel(); // never leave a stale child/pipe around

    int toChild[2] = {-1, -1};   // parent writes -> child reads
    int toParent[2] = {-1, -1};  // child writes -> parent reads

    if (pipe(toChild) != 0 || pipe(toParent) != 0) {
        fprintf(stderr, "PamAuth: pipe() failed: %s\n", strerror(errno));
        if (toChild[0] >= 0)  close(toChild[0]);
        if (toChild[1] >= 0)  close(toChild[1]);
        if (toParent[0] >= 0) close(toParent[0]);
        if (toParent[1] >= 0) close(toParent[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "PamAuth: fork() failed: %s\n", strerror(errno));
        close(toChild[0]); close(toChild[1]);
        close(toParent[0]); close(toParent[1]);
        return false;
    }

    if (pid == 0) {
        // ---- child ----
        close(toChild[1]);   // unused write end of parent->child
        close(toParent[0]);  // unused read end of child->parent

        char buf[512];
        ssize_t n = read(toChild[0], buf, sizeof(buf) - 1);
        close(toChild[0]);
        if (n <= 0) _exit(1);
        buf[n] = '\0';

        struct pam_conv conv;
        ConvData data{buf};
        conv.conv = convFn;
        conv.appdata_ptr = &data;

        pam_handle_t* pamh = nullptr;
        int rc = pam_start("bezel", user.c_str(), &conv, &pamh);
        char result = 'E';
        if (rc == PAM_SUCCESS) {
            rc = pam_authenticate(pamh, 0);
            result = (rc == PAM_SUCCESS) ? 'S' : 'F';
            pam_end(pamh, rc);
        }

        (void)!write(toParent[1], &result, 1);
        close(toParent[1]);
        _exit(0);
    }

    // ---- parent ----
    close(toChild[0]);   // unused read end
    close(toParent[1]);  // unused write end

    ssize_t w = write(toChild[1], password.c_str(), password.size());
    close(toChild[1]);
    if (w != (ssize_t)password.size()) {
        // Child may already have exited without reading; fall through and wait.
        fprintf(stderr, "PamAuth: short write to child\n");
    }

    child_    = pid;
    resultFd_ = toParent[0];
    return true;
}

PamResult PamAuth::pollResult() {
    if (child_ < 0 || resultFd_ < 0) return PamResult::Pending;

    char result = 'E';
    ssize_t n = read(resultFd_, &result, 1);
    close(resultFd_);
    resultFd_ = -1;

    if (child_ > 0) waitpid(child_, nullptr, 0);
    child_ = -1;

    if (n != 1) return PamResult::Error;
    switch (result) {
        case 'S': return PamResult::Success;
        case 'F': return PamResult::AuthFail;
        default:  return PamResult::Error;
    }
}

void PamAuth::cancel() {
    if (child_ > 0) {
        kill(child_, SIGTERM);
        waitpid(child_, nullptr, 0);
        child_ = -1;
    }
    if (resultFd_ >= 0) {
        close(resultFd_);
        resultFd_ = -1;
    }
}
