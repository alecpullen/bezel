#include "audio_service.hpp"
#include <cstdio>
#include <cstring>

#ifdef HAVE_PIPEWIRE
#include <spa/param/props.h>
#include <spa/pod/iter.h>
#include <spa/utils/dict.h>

void registry_global_cb(void* data, uint32_t id, uint32_t,
                        const char* type, uint32_t,
                        const struct spa_dict* props) {
    auto* self = static_cast<AudioService*>(data);
    if (self->sinkProxy_) return;
    if (strcmp(type, "PipeWire:Interface:Node") != 0) return;

    const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (!mediaClass || strcmp(mediaClass, "Audio/Sink") != 0) return;

    self->sinkId_ = id;
    self->sinkProxy_ = static_cast<pw_proxy*>(
        pw_registry_bind(self->registry_, id, type, PW_VERSION_NODE, 0));
    if (!self->sinkProxy_) return;

    static struct pw_node_events node_events = {};
    node_events.version = PW_VERSION_NODE_EVENTS;
    node_events.param = [](void* d, int, uint32_t pid, uint32_t, uint32_t,
                           const struct spa_pod* param) {
        auto* self = static_cast<AudioService*>(d);
        if (pid == SPA_PARAM_Props && param) {
            self->handleSinkParam(param);
        }
    };
    pw_node_add_listener(reinterpret_cast<pw_node*>(self->sinkProxy_),
                         &self->sinkListener_, &node_events, self);
    uint32_t params[1] = { SPA_PARAM_Props };
    pw_node_subscribe_params(reinterpret_cast<pw_node*>(self->sinkProxy_), params, 1);

    const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    self->current_.sinkName = name ? name : "";

    self->sinkBound_ = true;
}

void core_done_cb(void* data, uint32_t id, int seq) {
    auto* self = static_cast<AudioService*>(data);
    if (id == PW_ID_CORE && seq == self->coreSyncSeq_) {
        pw_main_loop_quit(self->loop_);
    }
}

void AudioService::handleSinkParam(const struct spa_pod* param) {
    bool changed = false;
    bool newMuted = false;
    float newVol = current_.volume;

    struct spa_pod_prop* prop;
    SPA_POD_OBJECT_FOREACH((const struct spa_pod_object*)param, prop) {
        if (prop->key == SPA_PROP_mute) {
            bool v = false;
            if (spa_pod_get_bool(&prop->value, &v) == 0) {
                newMuted = v;
            }
        } else if (prop->key == SPA_PROP_channelVolumes) {
            float values[32];
            uint32_t n = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, values, 32);
            if (n > 0) {
                float sum = 0.0f;
                for (uint32_t i = 0; i < n; ++i) sum += values[i];
                newVol = sum / (float)n;
                if (newVol > 1.0f) newVol = 1.0f;
            }
        }
    }

    if (newMuted != current_.muted) {
        current_.muted = newMuted;
        changed = true;
    }
    if (newVol != current_.volume) {
        current_.volume = newVol;
        changed = true;
    }
    if (changed) dirty_ = true;
}

bool AudioService::init() {
    pw_init(nullptr, nullptr);
    loop_ = pw_main_loop_new(nullptr);
    if (!loop_) {
        std::fprintf(stderr, "AudioService: pw_main_loop_new failed\n");
        return false;
    }
    context_ = pw_context_new(pw_main_loop_get_loop(loop_), nullptr, 0);
    if (!context_) {
        std::fprintf(stderr, "AudioService: pw_context_new failed\n");
        return false;
    }
    core_ = pw_context_connect(context_, nullptr, 0);
    if (!core_) {
        std::fprintf(stderr, "AudioService: pw_context_connect failed\n");
        return false;
    }

    static struct pw_core_events core_events = {};
    core_events.version = PW_VERSION_CORE_EVENTS;
    core_events.done = core_done_cb;
    pw_core_add_listener(core_, &coreListener_, &core_events, this);

    registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
    static struct pw_registry_events reg_events = {};
    reg_events.version = PW_VERSION_REGISTRY_EVENTS;
    reg_events.global = registry_global_cb;
    pw_registry_add_listener(registry_, &registryListener_, &reg_events, this);

    coreSyncSeq_ = pw_core_sync(core_, PW_ID_CORE, 0);
    pw_main_loop_iterate(loop_, -1);

    if (!sinkBound_) {
        std::fprintf(stderr, "AudioService: no Audio/Sink node found\n");
        return false;
    }
    return true;
}

bool AudioService::tick() {
    if (!loop_) return false;
    pw_main_loop_iterate(loop_, 0);
    if (dirty_) {
        dirty_ = false;
        notify();
        return true;
    }
    return false;
}

int AudioService::pollFd() const {
    if (!loop_) return -1;
    return pw_loop_get_fd(pw_main_loop_get_loop(loop_));
}

AudioService::~AudioService() {
    if (sinkProxy_) pw_proxy_destroy(sinkProxy_);
    if (registry_) pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry_));
    if (core_) pw_core_disconnect(core_);
    if (context_) pw_context_destroy(context_);
    if (loop_) pw_main_loop_destroy(loop_);
}

#else  // !HAVE_PIPEWIRE

AudioService::~AudioService() = default;
bool AudioService::init() {
    std::fprintf(stderr, "AudioService: built without PipeWire support\n");
    return false;
}
bool AudioService::tick() { return false; }
int AudioService::pollFd() const { return -1; }

#endif
