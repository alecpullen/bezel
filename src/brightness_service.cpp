#include "brightness_service.hpp"
#include <cstdio>
#include <dirent.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

BrightnessService::~BrightnessService() {
    if (watchFd_ >= 0) inotify_rm_watch(inotifyFd_, watchFd_);
    if (inotifyFd_ >= 0) close(inotifyFd_);
}

bool BrightnessService::init() {
    DIR* dir = opendir("/sys/class/backlight");
    if (!dir) {
        std::fprintf(stderr, "BrightnessService: /sys/class/backlight not available\n");
        return false;
    }

    std::string devicePath;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string base = "/sys/class/backlight/";
        base += ent->d_name;
        std::string maxPath = base + "/max_brightness";
        std::string actualPath = base + "/actual_brightness";
        struct stat st;
        if (stat(maxPath.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
            stat(actualPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            devicePath = base;
            current_.device = ent->d_name;
            break;
        }
    }
    closedir(dir);

    if (devicePath.empty()) {
        std::fprintf(stderr, "BrightnessService: no backlight device with max+actual files\n");
        return false;
    }

    FILE* f = fopen((devicePath + "/max_brightness").c_str(), "r");
    if (!f) {
        std::fprintf(stderr, "BrightnessService: cannot read max_brightness\n");
        return false;
    }
    if (fscanf(f, "%d", &maxBrightness_) != 1 || maxBrightness_ <= 0) {
        fclose(f);
        std::fprintf(stderr, "BrightnessService: invalid max_brightness\n");
        return false;
    }
    fclose(f);

    if (!readBrightness()) return false;

    inotifyFd_ = inotify_init1(IN_NONBLOCK);
    if (inotifyFd_ < 0) {
        std::perror("BrightnessService: inotify_init1");
        return false;
    }
    watchFd_ = inotify_add_watch(inotifyFd_, (devicePath + "/actual_brightness").c_str(), IN_MODIFY);
    if (watchFd_ < 0) {
        std::perror("BrightnessService: inotify_add_watch");
        close(inotifyFd_);
        inotifyFd_ = -1;
        return false;
    }

    // The initial read is baseline state, not a change to report; the OSD
    // must not pop up at startup.
    dirty_ = false;

    return true;
}

bool BrightnessService::readBrightness() {
    std::string path = "/sys/class/backlight/" + current_.device + "/actual_brightness";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    int actual = 0;
    if (fscanf(f, "%d", &actual) != 1) {
        fclose(f);
        return false;
    }
    fclose(f);
    float newLevel = maxBrightness_ > 0 ? (float)actual / (float)maxBrightness_ : 0.0f;
    if (newLevel < 0.0f) newLevel = 0.0f;
    if (newLevel > 1.0f) newLevel = 1.0f;
    if (newLevel != current_.level) {
        current_.level = newLevel;
        dirty_ = true;
    }
    return true;
}

bool BrightnessService::tick() {
    if (inotifyFd_ < 0) return false;
    char buf[4096] __attribute__((aligned(4)));
    bool any = false;
    while (true) {
        ssize_t n = read(inotifyFd_, buf, sizeof(buf));
        if (n <= 0) break;
        any = true;
    }
    if (any) readBrightness();
    if (dirty_) {
        dirty_ = false;
        notify();
        return true;
    }
    return false;
}
