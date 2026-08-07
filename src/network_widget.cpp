#include "network_widget.hpp"
#include "renderer.hpp"
#include <cstdio>
#include <cstring>

static std::string truncateSsid(const std::string& ssid, size_t maxChars) {
    if (ssid.size() <= maxChars) return ssid;
    return ssid.substr(0, maxChars) + "...";
}

NetworkWidget::NetworkWidget(const Theme& theme, NVGcontext* ctx, int fontHandle, NetworkService& svc)
    : theme_(theme), svc_(svc) {
    label_ = std::make_unique<Label>(ctx, fontHandle);
    label_->setFontSize(14.0f);
    label_->setColor(theme_.textPrimary);
    label_->setAlign(NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    lastInfo_ = svc_.info();
    updateLabelText();
    subId_ = svc_.subscribe([this] {
        lastInfo_ = svc_.info();
        updateLabelText();
        if (requestRedraw_) requestRedraw_();
    });
}

NetworkWidget::~NetworkWidget() {
    if (subId_ >= 0) svc_.unsubscribe(subId_);
}

int NetworkWidget::preferredWidth() const { return label_->preferredWidth(); }
int NetworkWidget::preferredHeight() const { return 24; }

void NetworkWidget::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
    label_->layout(x, y, w, h);
}

void NetworkWidget::render(const Renderer& renderer) const {
    label_->render(renderer);
}

void NetworkWidget::setCallback(std::function<void()> requestRedraw) {
    requestRedraw_ = std::move(requestRedraw);
}

void NetworkWidget::updateLabelText() const {
    const char* icon;
    char buf[128];
    switch (lastInfo_.type) {
        case NetworkInfo::Type::Wifi: {
            // 5 strength levels
            int s = lastInfo_.strength;
            if (s >= 80)      icon = "\xef\x9a\xae"; // \uf62e
            else if (s >= 60) icon = "\xef\x9a\xad"; // \uf62d
            else if (s >= 40) icon = "\xef\x9a\xac"; // \uf62c
            else if (s >= 20) icon = "\xef\x9a\xab"; // \uf62b
            else              icon = "\xef\x9a\xaa"; // \uf62a
            std::snprintf(buf, sizeof(buf), "%s  %s", icon,
                          truncateSsid(lastInfo_.ssid, 12).c_str());
            break;
        }
        case NetworkInfo::Type::Wired:
            icon = "\xef\x83\xa8"; // \uf0e8 ethernet
            std::snprintf(buf, sizeof(buf), "%s", icon);
            break;
        case NetworkInfo::Type::None:
        default:
            icon = "\xef\x84\xa7"; // \uf127 disconnected
            std::snprintf(buf, sizeof(buf), "%s", icon);
            break;
    }
    label_->setText(buf);
}
