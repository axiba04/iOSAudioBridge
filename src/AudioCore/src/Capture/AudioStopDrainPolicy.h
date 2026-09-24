#pragma once
#include <chrono>

namespace audiobridge {
// SYNC STOP is a peer stop signal, not a substitute for USB/PnP restoration.
// Leave the legacy deadline in place if that signal or a quiet stream is absent.
class AudioStopDrainPolicy {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto QuietWindow = std::chrono::milliseconds(250);

    AudioStopDrainPolicy(Clock::time_point start, bool peer_already_stopped, bool conservative)
        : deadline_(start + (peer_already_stopped ? std::chrono::seconds(1) : std::chrono::seconds(6))),
          quiet_since_(start), peer_stopped_(peer_already_stopped), conservative_(conservative) {}

    void peer_stop(Clock::time_point now) noexcept {
        if (!peer_stopped_) quiet_since_ = now;
        peer_stopped_ = true;
    }
    void media(Clock::time_point now) noexcept { quiet_since_ = now; }
    bool finished(Clock::time_point now, bool releases_complete, bool decoder_empty) const noexcept {
        return releases_complete || now >= deadline_ ||
            (!conservative_ && peer_stopped_ && decoder_empty && now - quiet_since_ >= QuietWindow);
    }
    const char* reason(Clock::time_point now, bool releases_complete) const noexcept {
        return releases_complete ? "releases" : now >= deadline_ ? "deadline" : "peer_stop_quiet";
    }
private:
    Clock::time_point deadline_, quiet_since_;
    bool peer_stopped_, conservative_;
};
}
