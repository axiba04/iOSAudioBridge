#pragma once
#include <cstddef>

namespace audiobridge {
// Render-thread only. Drain the old tail during a known pause, then rebuild the
// normal startup reserve before consuming the resumed stream. No extra queue.
class ResumeBufferGate {
public:
    bool hold(bool paused, std::size_t queued, std::size_t target) noexcept {
        if (paused) { waiting_ = true; return false; }
        if (waiting_ && queued < target) return true;
        waiting_ = false;
        return false;
    }
    bool waiting() const noexcept { return waiting_; }
private:
    bool waiting_{};
};
}
