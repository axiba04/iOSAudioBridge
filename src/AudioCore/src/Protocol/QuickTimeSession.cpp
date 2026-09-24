#include "Protocol/QuickTimeSession.h"

#include <bit>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace iPhoneMirror::quicktime {
namespace {

constexpr std::uint64_t EmptyClockRef = 1;

std::uint32_t u32le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8U) |
        (static_cast<std::uint32_t>(p[2]) << 16U) |
        (static_cast<std::uint32_t>(p[3]) << 24U);
}

std::uint64_t u64le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint64_t>(u32le(p)) | (static_cast<std::uint64_t>(u32le(p + 4)) << 32U);
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    append_u32(bytes, static_cast<std::uint32_t>(value));
    append_u32(bytes, static_cast<std::uint32_t>(value >> 32U));
}

std::vector<std::uint8_t> wrap_value(std::uint32_t magic, std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> result;
    result.reserve(8 + payload.size());
    append_u32(result, static_cast<std::uint32_t>(8 + payload.size()));
    append_u32(result, magic);
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

std::vector<std::uint8_t> number_u32(std::uint32_t value) {
    std::vector<std::uint8_t> payload{3};
    append_u32(payload, value);
    return wrap_value(fourcc('n', 'm', 'b', 'v'), payload);
}

std::vector<std::uint8_t> number_f64(double value) {
    std::vector<std::uint8_t> payload{6};
    append_u64(payload, std::bit_cast<std::uint64_t>(value));
    return wrap_value(fourcc('n', 'm', 'b', 'v'), payload);
}

std::vector<std::uint8_t> string_value(std::string_view value) {
    return wrap_value(fourcc('s', 't', 'r', 'v'),
        std::span(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

std::vector<std::uint8_t> data_value(std::span<const std::uint8_t> value) {
    return wrap_value(fourcc('d', 'a', 't', 'v'), value);
}

using Entry = std::pair<std::string, std::vector<std::uint8_t>>;

std::vector<std::uint8_t> dictionary(std::vector<Entry> entries) {
    std::vector<std::uint8_t> payload;
    for (auto& [key, value] : entries) {
        const auto key_bytes = wrap_value(fourcc('s', 't', 'r', 'k'),
            std::span(reinterpret_cast<const std::uint8_t*>(key.data()), key.size()));
        std::vector<std::uint8_t> pair_payload;
        pair_payload.reserve(key_bytes.size() + value.size());
        pair_payload.insert(pair_payload.end(), key_bytes.begin(), key_bytes.end());
        pair_payload.insert(pair_payload.end(), value.begin(), value.end());
        const auto pair = wrap_value(fourcc('k', 'e', 'y', 'v'), pair_payload);
        payload.insert(payload.end(), pair.begin(), pair.end());
    }
    return wrap_value(fourcc('d', 'i', 'c', 't'), payload);
}

std::vector<std::uint8_t> async_dict(std::uint32_t subtype, std::uint64_t clock,
    std::vector<std::uint8_t> dict) {
    std::vector<std::uint8_t> result;
    result.reserve(20 + dict.size());
    append_u32(result, static_cast<std::uint32_t>(20 + dict.size()));
    append_u32(result, fourcc('a', 's', 'y', 'n'));
    append_u64(result, clock);
    append_u32(result, subtype);
    result.insert(result.end(), dict.begin(), dict.end());
    return result;
}

std::vector<std::uint8_t> default_audio_descriptor() {
    std::vector<std::uint8_t> result;
    result.reserve(56);
    append_u64(result, std::bit_cast<std::uint64_t>(48000.0));
    append_u32(result, fourcc('l', 'p', 'c', 'm'));
    append_u32(result, 12);
    append_u32(result, 4);
    append_u32(result, 1);
    append_u32(result, 4);
    append_u32(result, 2);
    append_u32(result, 16);
    append_u32(result, 0);
    append_u64(result, std::bit_cast<std::uint64_t>(48000.0));
    append_u64(result, std::bit_cast<std::uint64_t>(48000.0));
    return result;
}

std::vector<std::uint8_t> make_hpa1(std::uint64_t clock) {
    const auto descriptor = default_audio_descriptor();
    return async_dict(fourcc('h', 'p', 'a', '1'), clock, dictionary({
        {"BufferAheadInterval", number_f64(0.07300000000000001)},
        {"deviceUID", string_value("Valeria")},
        {"ScreenLatency", number_f64(0.04)},
        {"formats", data_value(descriptor)},
        {"EDIDAC3Support", number_u32(0)},
        {"deviceName", string_value("Valeria")},
    }));
}

std::vector<std::uint8_t> clock_reply(std::uint64_t correlation, std::uint64_t clock) {
    std::vector<std::uint8_t> result;
    result.reserve(28);
    append_u32(result, 28);
    append_u32(result, fourcc('r', 'p', 'l', 'y'));
    append_u64(result, correlation);
    append_u32(result, 0);
    append_u64(result, clock);
    return result;
}

std::vector<std::uint8_t> zero_reply(std::uint64_t correlation) {
    std::vector<std::uint8_t> result;
    result.reserve(24);
    append_u32(result, 24);
    append_u32(result, fourcc('r', 'p', 'l', 'y'));
    append_u64(result, correlation);
    append_u64(result, 0);
    return result;
}

std::vector<std::uint8_t> afmt_reply(std::uint64_t correlation) {
    const auto dict = dictionary({{"Error", number_u32(0)}});
    std::vector<std::uint8_t> result;
    result.reserve(20 + dict.size());
    append_u32(result, static_cast<std::uint32_t>(20 + dict.size()));
    append_u32(result, fourcc('r', 'p', 'l', 'y'));
    append_u64(result, correlation);
    append_u32(result, 0);
    result.insert(result.end(), dict.begin(), dict.end());
    return result;
}

std::vector<std::uint8_t> time_reply(std::uint64_t correlation, std::int64_t nanoseconds) {
    std::vector<std::uint8_t> result;
    result.reserve(44);
    append_u32(result, 44);
    append_u32(result, fourcc('r', 'p', 'l', 'y'));
    append_u64(result, correlation);
    append_u32(result, 0);
    append_u64(result, static_cast<std::uint64_t>(nanoseconds));
    append_u32(result, 1'000'000'000);
    append_u32(result, 1);
    append_u64(result, 0);
    return result;
}

std::vector<std::uint8_t> skew_reply(std::uint64_t correlation, double skew) {
    std::vector<std::uint8_t> result;
    result.reserve(28);
    append_u32(result, 28);
    append_u32(result, fourcc('r', 'p', 'l', 'y'));
    append_u64(result, correlation);
    append_u32(result, 0);
    append_u64(result, std::bit_cast<std::uint64_t>(skew));
    return result;
}

std::vector<std::uint8_t> async_control(std::uint32_t subtype, std::uint64_t clock) {
    std::vector<std::uint8_t> result;
    result.reserve(20);
    append_u32(result, 20);
    append_u32(result, fourcc('a', 's', 'y', 'n'));
    append_u64(result, clock);
    append_u32(result, subtype);
    return result;
}

std::uint64_t correlation(const Packet& packet) {
    if (packet.payload.size() < 24) throw std::runtime_error("SYNC packet is shorter than correlation id");
    return u64le(packet.payload.data() + 16);
}

} // namespace

SessionProtocol::SessionProtocol(SessionOptions options) : options_(options) {
    if(options_.request_video) throw std::invalid_argument("Video is disabled in AudioCore");
    reset();
}

void SessionProtocol::reset() {
    state_ = SessionState::WaitingForPing;
    local_stop_requested_ = false;
    epoch_ = std::chrono::steady_clock::now();
    device_audio_clock_ = local_audio_clock_ = device_video_clock_ = local_video_clock_ = local_host_clock_ = 0;
    video_frames_ = audio_packets_ = 0;
    video_bytes_ = 0;
    video_format_.reset();
    audio_format_.reset();
    negotiated_audio_.reset();
}

SessionEvent SessionProtocol::process(const Packet& packet) {
    SessionEvent event;
    event.state = state_;
    try {
        if (packet.kind == PacketKind::Ping) {
            event.outbound.push_back(make_ping());
            if (!local_stop_requested_) state_ = SessionState::WaitingForAudioClock;
        } else if (packet.kind == PacketKind::Sync) {
            const auto id = correlation(packet);
            if (packet.subtype == fourcc('c', 'w', 'p', 'a')) {
                if (packet.payload.size() < 32) throw std::runtime_error("CWPA has no device audio clock");
                device_audio_clock_ = u64le(packet.payload.data() + 24);
                local_audio_clock_ = device_audio_clock_ + 0x1000;
                event.outbound.push_back(clock_reply(id, local_audio_clock_));
                if (options_.request_audio && !local_stop_requested_) event.outbound.push_back(make_hpa1(device_audio_clock_));
                if (!local_stop_requested_) state_ = SessionState::Negotiating;
            } else if (packet.subtype == fourcc('a', 'f', 'm', 't')) {
                if (packet.payload.size() < 64) throw std::runtime_error("AFMT has no ASBD");
                negotiated_audio_ = coremedia::parse_audio_format(std::span(packet.payload).subspan(24, 40));
                event.outbound.push_back(afmt_reply(id));
            } else if (packet.subtype == fourcc('c', 'v', 'r', 'p')) {
                if (packet.payload.size() < 32) throw std::runtime_error("CVRP has no device video clock");
                device_video_clock_ = u64le(packet.payload.data() + 24);
                local_video_clock_ = device_video_clock_ + 0x1000af;
                event.outbound.push_back(clock_reply(id, local_video_clock_));
            } else if (packet.subtype == fourcc('c', 'l', 'o', 'k')) {
                local_host_clock_ = packet.clock_ref + 0x10000;
                event.outbound.push_back(clock_reply(id, local_host_clock_));
            } else if (packet.subtype == fourcc('t', 'i', 'm', 'e')) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - epoch_).count();
                event.outbound.push_back(time_reply(id, elapsed));
            } else if (packet.subtype == fourcc('s', 'k', 'e', 'w')) {
                event.outbound.push_back(skew_reply(id, 48000.0));
            } else if (packet.subtype == fourcc('g', 'o', '!', ' ')) {
                event.outbound.push_back(zero_reply(id));
            } else if (packet.subtype == fourcc('s', 't', 'o', 'p')) {
                event.outbound.push_back(zero_reply(id));
                state_ = local_stop_requested_ ? SessionState::Stopped : SessionState::Paused;
            } else {
                event.warning = "unsupported SYNC subtype " + fourcc_string(packet.subtype);
            }
        } else if (packet.kind == PacketKind::Async) {
            if (packet.is_video_sample()) {
                // Count incoming video even when it was not requested. Never
                // mistake locally discarded video for audio-only transport.
                ++video_frames_;
                video_bytes_ += packet.payload.size();
                if (!options_.request_video) {
                    event.warning = "The device sent video during an audio-only session";
                    return event;
                }
            } else if (packet.is_audio_sample()) {
                const auto envelope = coremedia::parse_sample_envelope(packet.payload);
                auto sample = coremedia::parse_sample_buffer(envelope.serialized_sample_buffer);
                if (sample.format) audio_format_ = sample.format;
                else if (!options_.request_video && !audio_format_ && negotiated_audio_) {
                    coremedia::FormatDescription format;
                    format.media_type = fourcc('s', 'o', 'u', 'n');
                    format.audio = negotiated_audio_;
                    audio_format_ = std::move(format);
                }
                event.audio_sample = std::move(sample);
                ++audio_packets_;
                if (!options_.request_video && !local_stop_requested_)
                    state_ = SessionState::Streaming;
            }
        }
    } catch (const std::exception& error) {
        state_ = SessionState::Error;
        event.warning = error.what();
    }
    event.state = state_;
    return event;
}

std::vector<std::vector<std::uint8_t>> SessionProtocol::stop_messages() {
    local_stop_requested_ = true;
    state_ = SessionState::Stopping;
    std::vector<std::vector<std::uint8_t>> messages;
    if (options_.request_audio) {
        // Use the negotiated device audio clock when available. If the
        // session failed before CWPA/PING, still send the same audio-stop
        // control as the reference clients with the protocol empty clock.
        const auto audio_clock = device_audio_clock_ != 0 ? device_audio_clock_ : EmptyClockRef;
        messages.push_back(async_control(fourcc('h', 'p', 'a', '0'), audio_clock));
    }
    if (options_.request_video || video_frames_ != 0 || device_video_clock_ != 0)
        messages.push_back(async_control(fourcc('h', 'p', 'd', '0'), EmptyClockRef));
    return messages;
}

std::vector<std::vector<std::uint8_t>> SessionProtocol::complete_stop_messages() {
    local_stop_requested_ = true;
    state_ = SessionState::Stopped;
    if (!options_.request_video && video_frames_ == 0 && device_video_clock_ == 0)
        return {};
    // Apple's reference teardown sends HPD0 once to request display release,
    // waits for both audio/video RELS notifications, then sends HPD0 once more
    // before the USB interface is released.
    return {async_control(fourcc('h', 'p', 'd', '0'), EmptyClockRef)};
}

std::vector<std::vector<std::uint8_t>> SessionProtocol::begin_display_reconfigure(
    std::uint32_t, std::uint32_t) { return {}; }
std::vector<std::vector<std::uint8_t>> SessionProtocol::complete_display_reconfigure() { return {}; }
} // namespace iPhoneMirror::quicktime
