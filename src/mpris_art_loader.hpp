#pragma once
#include <nanovg.h>
#include <string>

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

class MprisArtLoader {
public:
    explicit MprisArtLoader(NVGcontext* vg);
    ~MprisArtLoader();

    MprisArtLoader(const MprisArtLoader&) = delete;
    MprisArtLoader& operator=(const MprisArtLoader&) = delete;

    void request(const std::string& url);
    void setNvgContext(NVGcontext* vg) { vg_ = vg; }
    int imageHandle() const { return imageHandle_; }
    int curlFd() const;
    int curlTimeout() const;
    void tick();

private:
    NVGcontext* vg_ = nullptr;
    int imageHandle_ = -1;
    std::string currentUrl_;

#ifdef HAVE_LIBCURL
    CURLM* multi_ = nullptr;
    CURL* easy_ = nullptr;
    std::string buffer_;

    void cleanupEasy();
    static size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata);
    void finalizeTransfer();
    bool decodeAndCreateImage();
#endif
};
