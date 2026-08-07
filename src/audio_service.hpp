#pragma once
#include "service.hpp"
#include <cstdint>
#include <string>

#ifdef HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#endif

struct AudioInfo {
    float volume = 0.0f;        // 0.0-1.0, averaged across channels
    bool  muted = false;
    std::string sinkName;
};

class AudioService : public Service {
public:
    AudioService() = default;
    ~AudioService() override;

    bool init() override;
    bool tick() override;

    const AudioInfo& info() const { return current_; }
    int pollFd() const;

private:
#ifdef HAVE_PIPEWIRE
    friend void registry_global_cb(void* data, uint32_t id, uint32_t permissions,
                                   const char* type, uint32_t version,
                                   const struct spa_dict* props);
    friend void core_done_cb(void* data, uint32_t id, int seq);

    void handleSinkParam(const struct spa_pod* param);

    struct pw_main_loop* loop_ = nullptr;
    struct pw_context* context_ = nullptr;
    struct pw_core* core_ = nullptr;
    struct pw_registry* registry_ = nullptr;
    struct pw_proxy* sinkProxy_ = nullptr;
    struct spa_hook registryListener_;
    struct spa_hook coreListener_;
    struct spa_hook sinkListener_;
    uint32_t sinkId_ = 0;
    int coreSyncSeq_ = 0;
    bool sinkBound_ = false;
#endif
    AudioInfo current_;
    bool dirty_ = false;
};
