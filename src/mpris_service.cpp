#include "mpris_service.hpp"
#include <cstdio>
#include <vector>

static const char* MPRIS_PREFIX = "org.mpris.MediaPlayer2.";

MprisService::MprisService(sdbus::IConnection* conn, NVGcontext* vg)
    : conn_(conn), vg_(vg), artLoader_(vg) {}

bool MprisService::init() {
    if (!conn_) {
        std::fprintf(stderr, "MprisService: no session bus\n");
        return false;
    }
    try {
        dbusProxy_ = sdbus::createProxy(*conn_,
            sdbus::ServiceName{"org.freedesktop.DBus"},
            sdbus::ObjectPath{"/org/freedesktop/DBus"});

        dbusProxy_->uponSignal("NameOwnerChanged")
            .onInterface("org.freedesktop.DBus")
            .call([this](const std::string& name,
                         const std::string& oldOwner,
                         const std::string& newOwner) {
                this->onNameOwnerChanged(name, oldOwner, newOwner);
            });

        scanExistingPlayers();
        if (currentPlayer_.empty()) {
            std::printf("MprisService: no MPRIS players found at startup\n");
            // Not a failure — players may appear later.
        }
        return true;
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "MprisService: init failed: %s\n", e.what());
        return false;
    }
}

void MprisService::scanExistingPlayers() {
    try {
        std::vector<std::string> names;
        dbusProxy_->callMethod("ListNames")
            .onInterface("org.freedesktop.DBus")
            .storeResultsTo(names);
        for (const auto& name : names) {
            if (name.rfind(MPRIS_PREFIX, 0) != 0) continue;
            std::string owner;
            try {
                dbusProxy_->callMethod("GetNameOwner")
                    .onInterface("org.freedesktop.DBus")
                    .withArguments(name)
                    .storeResultsTo(owner);
                seenPlayers_[name] = std::chrono::steady_clock::now();
                if (currentPlayer_.empty()) {
                    selectPlayer(name, owner);
                }
            } catch (const sdbus::Error&) {}
        }
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "MprisService: ListNames failed: %s\n", e.what());
    }
}

void MprisService::onNameOwnerChanged(const std::string& name,
                                      const std::string& oldOwner,
                                      const std::string& newOwner) {
    (void)oldOwner;
    if (name.rfind(MPRIS_PREFIX, 0) != 0) return;
    if (newOwner.empty()) {
        // Player gone
        seenPlayers_.erase(name);
        if (name == currentPlayer_) {
            playerProxy_.reset();
            currentPlayer_.clear();
            current_ = MprisInfo{};
            dirty_ = true;
            // Pick the most-recently-active remaining player
            std::string best;
            std::chrono::steady_clock::time_point bestT;
            for (const auto& [n, t] : seenPlayers_) {
                if (best.empty() || t > bestT) { best = n; bestT = t; }
            }
            if (!best.empty()) {
                selectPlayer(best, "");  // owner unknown; proxy uses service name
            }
        }
    } else {
        seenPlayers_[name] = std::chrono::steady_clock::now();
        // Switch to the new player (most-recently-active wins)
        selectPlayer(name, newOwner);
    }
}

void MprisService::selectPlayer(const std::string& name, const std::string& /*owner*/) {
    if (name == currentPlayer_) return;
    try {
        playerProxy_ = sdbus::createProxy(*conn_,
            sdbus::ServiceName{name},
            sdbus::ObjectPath{"/org/mpris/MediaPlayer2"});

        playerProxy_->uponSignal("PropertiesChanged")
            .onInterface("org.freedesktop.DBus.Properties")
            .call([this](const std::string& iface,
                         const std::map<sdbus::PropertyName, sdbus::Variant>& changed,
                         const std::vector<std::string>& invalidated) {
                this->onPlayerPropertiesChanged(iface, changed, invalidated);
            });

        currentPlayer_ = name;
        current_.player = name;
        refreshPlayer();
        dirty_ = true;
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "MprisService: selectPlayer(%s) failed: %s\n",
                     name.c_str(), e.what());
    }
}

static std::string variantToString(const sdbus::Variant& v) {
    try { return static_cast<std::string>(v); }
    catch (const sdbus::Error&) { return std::string{}; }
}

static std::string variantToFirstString(const sdbus::Variant& v) {
    try {
        // xesam:artist is typically an array of strings
        auto arr = v.get<std::vector<std::string>>();
        if (!arr.empty()) return arr.front();
    } catch (const sdbus::Error&) {}
    try { return static_cast<std::string>(v); }
    catch (const sdbus::Error&) { return std::string{}; }
}

void MprisService::refreshPlayer() {
    if (!playerProxy_) return;
    try {
        std::string statusStr =
            static_cast<std::string>(
                playerProxy_->getProperty("PlaybackStatus")
                    .onInterface("org.mpris.MediaPlayer2.Player"));
        MprisInfo::Status s = MprisInfo::Status::Stopped;
        if (statusStr == "Playing") s = MprisInfo::Status::Playing;
        else if (statusStr == "Paused") s = MprisInfo::Status::Paused;

        sdbus::Variant metadata =
            playerProxy_->getProperty("Metadata")
                .onInterface("org.mpris.MediaPlayer2.Player");

        // Metadata is a dict {key: variant}
        auto md = metadata.get<std::map<std::string, sdbus::Variant>>();
        std::string title, artist, artUrl;
        if (md.count("xesam:title")) title = variantToString(md.at("xesam:title"));
        if (md.count("xesam:artist")) artist = variantToFirstString(md.at("xesam:artist"));
        if (md.count("mpris:artUrl")) artUrl = variantToString(md.at("mpris:artUrl"));

        if (title != current_.title || artist != current_.artist ||
            artUrl != current_.artUrl || s != current_.status) {
            current_.title = title;
            current_.artist = artist;
            current_.artUrl = artUrl;
            current_.status = s;
            if (!artUrl.empty()) artLoader_.request(artUrl);
            dirty_ = true;
        }
    } catch (const sdbus::Error& e) {
        std::fprintf(stderr, "MprisService: refreshPlayer failed: %s\n", e.what());
    }
}

void MprisService::onPlayerPropertiesChanged(const std::string& /*iface*/,
                                             const std::map<sdbus::PropertyName, sdbus::Variant>& /*changed*/,
                                             const std::vector<std::string>& /*invalidated*/) {
    refreshPlayer();
}

void MprisService::setNvgContext(NVGcontext* vg) {
    if (vg_ == nullptr && vg != nullptr) {
        vg_ = vg;
        artLoader_.setNvgContext(vg);
        // Re-request art if we already have a URL queued
        if (!current_.artUrl.empty()) artLoader_.request(current_.artUrl);
    }
}

bool MprisService::tick() {
    // Drive art loader
    int oldHandle = artLoader_.imageHandle();
    artLoader_.tick();
    if (artLoader_.imageHandle() != oldHandle) {
        dirty_ = true;
    }
    if (dirty_) {
        dirty_ = false;
        notify();
        return true;
    }
    return false;
}
