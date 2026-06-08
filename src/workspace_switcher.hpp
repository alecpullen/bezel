#pragma once
#include "widget.hpp"
#include "workspace_service.hpp"
#include <functional>
#include <vector>

class WorkspaceSwitcher : public Widget {
public:
    WorkspaceSwitcher(const Theme& theme, int fontHandle, wl_output* output, WorkspaceService& service);
    ~WorkspaceSwitcher() override;

    void setCallback(std::function<void()> cb);

    int preferredWidth() const override;
    int preferredHeight() const override;
    void layout(int x, int y, int w, int h) override;
    void render(const Renderer& renderer) const override;

    static constexpr int VISIBLE_COUNT = 4;

private:
    void updateViewOffset();

    const Theme& theme_;
    int fontHandle_;
    wl_output* output_;
    WorkspaceService& service_;
    std::function<void()> callback_;
    int subId_       = -1;
    int viewOffset_  = 0;
};
