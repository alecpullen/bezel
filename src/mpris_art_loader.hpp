#pragma once
#include <nanovg.h>
#include <string>
#include <unordered_map>

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
    void setNvgContext(NVGcontext* vg);
    int imageHandle(NVGcontext* vg) const;
    int imageGeneration() const { return generation_; }
    int curlFd() const;
    int curlTimeout() const;
    void tick();

private:
    // Each panel has its own NVGcontext (one Renderer per panel), so art must
    // be decoded once per context.
    std::unordered_map<NVGcontext*, int> images_;
    std::string currentUrl_;
    std::string artData_;   // decoded bytes of the current art, kept so
                            // contexts registered later can decode from it
    int generation_ = 0;

#ifdef HAVE_LIBCURL
    CURLM* multi_ = nullptr;
    CURL* easy_ = nullptr;
    std::string buffer_;

    void cleanupEasy();
    void finalizeTransfer();
    void decodeAndCreateImages();
#endif
};
