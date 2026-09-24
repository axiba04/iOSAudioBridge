#pragma once

#include "Media/CoreMedia.h"
#include "Protocol/QuickTimePacket.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iPhoneMirror::quicktime {

enum class SessionState {
    WaitingForPing,
    WaitingForAudioClock,
    Negotiating,
    Streaming,
    Paused, // Peer STOP while the host still requests audio; retain the USB session.
    Stopping,
    Stopped,
    Error,
};

struct SessionOptions {
    bool request_audio{true};
    bool request_video{false}; // True is rejected by this audio-only build.
};

struct SessionEvent {
    std::vector<std::vector<std::uint8_t>> outbound;
    std::optional<coremedia::SampleBuffer> video_sample;
    std::optional<coremedia::SampleBuffer> audio_sample;
    std::string warning;
    SessionState state{SessionState::WaitingForPing};
};

class SessionProtocol {
public:
    explicit SessionProtocol(SessionOptions options = {});

    [[nodiscard]] SessionEvent process(const Packet& packet);
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> stop_messages();
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> complete_stop_messages();
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> begin_display_reconfigure(
        std::uint32_t width, std::uint32_t height);
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> complete_display_reconfigure();
    void reset();

    [[nodiscard]] SessionState state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t video_frames() const noexcept { return video_frames_; }
    [[nodiscard]] std::uint64_t video_bytes() const noexcept { return video_bytes_; }
    [[nodiscard]] std::uint64_t audio_packets() const noexcept { return audio_packets_; }
    [[nodiscard]] const std::optional<coremedia::FormatDescription>& video_format() const noexcept { return video_format_; }
    [[nodiscard]] const std::optional<coremedia::FormatDescription>& audio_format() const noexcept { return audio_format_; }
    [[nodiscard]] const std::optional<coremedia::AudioStreamBasicDescription>& negotiated_audio() const noexcept { return negotiated_audio_; }

private:
    SessionOptions options_;
    SessionState state_{SessionState::WaitingForPing};
    bool local_stop_requested_{};
    std::chrono::steady_clock::time_point epoch_{std::chrono::steady_clock::now()};
    std::uint64_t device_audio_clock_{};
    std::uint64_t local_audio_clock_{};
    std::uint64_t device_video_clock_{};
    std::uint64_t local_video_clock_{};
    std::uint64_t local_host_clock_{};
    std::uint64_t video_frames_{};
    std::uint64_t video_bytes_{};
    std::uint64_t audio_packets_{};
    std::optional<coremedia::FormatDescription> video_format_;
    std::optional<coremedia::FormatDescription> audio_format_;
    std::optional<coremedia::AudioStreamBasicDescription> negotiated_audio_;
};

} // namespace iPhoneMirror::quicktime
