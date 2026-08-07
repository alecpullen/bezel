#include "mpris_art_loader.hpp"
#include <cstdio>
#include <cstring>

MprisArtLoader::MprisArtLoader(NVGcontext* vg) {
#ifdef HAVE_LIBCURL
    multi_ = curl_multi_init();
#endif
    if (vg) images_[vg] = -1;
}

MprisArtLoader::~MprisArtLoader() {
#ifdef HAVE_LIBCURL
    cleanupEasy();
    if (multi_) curl_multi_cleanup(multi_);
#endif
    for (auto& [vg, handle] : images_) {
        if (handle >= 0 && vg) nvgDeleteImage(vg, handle);
    }
}

void MprisArtLoader::setNvgContext(NVGcontext* vg) {
    if (!vg || images_.count(vg)) return;
    images_[vg] = -1;
    if (artData_.empty()) return;
    // A context registered after the art finished downloading still gets it.
    int handle = nvgCreateImageMem(vg, 0, (unsigned char*)artData_.data(), (int)artData_.size());
    if (handle > 0) {
        images_[vg] = handle;
        ++generation_;
    }
}

int MprisArtLoader::imageHandle(NVGcontext* vg) const {
    if (!vg) return -1;
    auto it = images_.find(vg);
    return it == images_.end() ? -1 : it->second;
}

#ifdef HAVE_LIBCURL

static size_t art_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    buf->append(ptr, total);
    return total;
}

void MprisArtLoader::cleanupEasy() {
    if (easy_ && multi_) curl_multi_remove_handle(multi_, easy_);
    if (easy_) curl_easy_cleanup(easy_);
    easy_ = nullptr;
    buffer_.clear();
}

void MprisArtLoader::request(const std::string& url) {
    if (url == currentUrl_ && !images_.empty()) return;  // cached or in flight

    // Drop the previous art (any context) before acting on the new URL.
    for (auto& [vg, handle] : images_) {
        if (handle >= 0 && vg) nvgDeleteImage(vg, handle);
        handle = -1;
    }
    artData_.clear();
    currentUrl_ = url;
    ++generation_;
    // Cancel an in-flight transfer even when the new URL is empty.
    cleanupEasy();
    if (url.empty()) return;

    easy_ = curl_easy_init();
    if (!easy_) return;
    curl_easy_setopt(easy_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, art_write_cb);
    curl_easy_setopt(easy_, CURLOPT_WRITEDATA, &buffer_);
    curl_easy_setopt(easy_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy_, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(easy_, CURLOPT_NOSIGNAL, 1L);
    curl_multi_add_handle(multi_, easy_);
}

int MprisArtLoader::curlFd() const {
    if (!multi_) return -1;
    int maxfd = -1;
    fd_set fdread, fdwrite, fdexcep;
    FD_ZERO(&fdread); FD_ZERO(&fdwrite); FD_ZERO(&fdexcep);
    CURLMcode mc = curl_multi_fdset(multi_, &fdread, &fdwrite, &fdexcep, &maxfd);
    (void)mc;
    return maxfd;
}

int MprisArtLoader::curlTimeout() const {
    if (!multi_) return -1;
    long ms = -1;
    curl_multi_timeout(multi_, &ms);
    return (int)ms;
}

void MprisArtLoader::tick() {
    if (!multi_ || !easy_) return;
    int still_running = 0;
    curl_multi_perform(multi_, &still_running);
    if (still_running == 0) {
        finalizeTransfer();
    }
}

void MprisArtLoader::finalizeTransfer() {
    if (!easy_) return;
    CURLMsg* msg;
    int msgs_left = 0;
    while ((msg = curl_multi_info_read(multi_, &msgs_left)) != nullptr) {
        if (msg->msg == CURLMSG_DONE && msg->easy_handle == easy_) {
            if (msg->data.result == CURLE_OK) {
                decodeAndCreateImages();
            } else {
                std::fprintf(stderr, "MprisArtLoader: curl failed: %s\n",
                             curl_easy_strerror(msg->data.result));
            }
        }
    }
    cleanupEasy();
}

void MprisArtLoader::decodeAndCreateImages() {
    if (buffer_.empty()) return;
    for (auto& [vg, handle] : images_) {
        if (!vg) continue;
        // NanoVG's nvgCreateImageMem expects RGBA or JPG/PNG bytes; it decodes via stb_image.
        int h = nvgCreateImageMem(vg, 0,
                                  (unsigned char*)buffer_.data(),
                                  (int)buffer_.size());
        if (h <= 0) {
            std::fprintf(stderr, "MprisArtLoader: nvgCreateImageMem failed for %s\n",
                         currentUrl_.c_str());
            continue;
        }
        handle = h;
    }
    artData_ = buffer_;
    ++generation_;
}

#else  // !HAVE_LIBCURL

void MprisArtLoader::request(const std::string&) {}
int MprisArtLoader::curlFd() const { return -1; }
int MprisArtLoader::curlTimeout() const { return -1; }
void MprisArtLoader::tick() {}

#endif
