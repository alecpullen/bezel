#pragma once
#include <functional>
#include <map>

class Service {
public:
    virtual ~Service() = default;
    virtual bool init() = 0;
    virtual bool tick() = 0; // returns true if state changed

    int subscribe(std::function<void()> cb) {
        int id = nextId_++;
        observers_[id] = cb;
        return id;
    }

    void unsubscribe(int id) {
        observers_.erase(id);
    }

protected:
    void notify() {
        for (auto& [id, cb] : observers_) {
            if (cb) cb();
        }
    }

private:
    std::map<int, std::function<void()>> observers_;
    int nextId_ = 0;
};
