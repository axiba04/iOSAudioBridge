// RTSP transport helpers, binary-plist parser and SETUP fixture adapted from
// RayrenSX/iPhoneMirror 1270c443a29457751cadd20f6d7bbe1ce213b9db, GPL-3.0-only.
// Source: third_party/iPhoneMirror-AirPlay/WirelessHost/tests/WirelessReceiverProtocolSmoke.cpp
#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
bool send_all(SOCKET socket, std::string_view data) {
    while (!data.empty()) {
        const auto sent = send(socket, data.data(), static_cast<int>(data.size()), 0);
        if (sent <= 0) return false;
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
}

std::size_t response_size(std::string_view response) {
    const auto header_end = response.find("\r\n\r\n");
    if (header_end == std::string_view::npos) return 0;
    auto headers = std::string(response.substr(0, header_end));
    std::ranges::transform(headers, headers.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    constexpr std::string_view name = "\r\ncontent-length:";
    const auto position = headers.find(name);
    if (position == std::string::npos) return header_end + 4;
    const auto value_start = headers.find_first_not_of(" \t", position + name.size());
    if (value_start == std::string::npos) return 0;
    const auto value_end = headers.find("\r\n", value_start);
    try {
        const auto length = std::stoull(headers.substr(
            value_start, value_end - value_start));
        return header_end + 4 + static_cast<std::size_t>(length);
    } catch (...) {
        return 0;
    }
}
SOCKET connect_with_retry(unsigned short port, in_addr address) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
            nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
        if (socket == INVALID_SOCKET) return INVALID_SOCKET;
        const sockaddr_in endpoint{AF_INET, htons(port), address, {}};
        if (connect(socket, reinterpret_cast<const sockaddr*>(&endpoint),
                sizeof(endpoint)) == 0)
            return socket;
        closesocket(socket);
        Sleep(50);
    }
    return INVALID_SOCKET;
}

std::vector<std::uint8_t> socket_request(SOCKET socket,
    std::string_view request) {
    if (!send_all(socket, request)) return {};
    DWORD timeout = 5000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    std::vector<std::uint8_t> response;
    std::array<std::uint8_t, 4096> bytes{};
    while (response.size() < 1024U * 1024U) {
        const auto count = recv(socket, reinterpret_cast<char*>(bytes.data()),
            static_cast<int>(bytes.size()), 0);
        if (count <= 0) break;
        response.insert(response.end(), bytes.begin(), bytes.begin() + count);
        const auto expected = response_size(std::string_view(
            reinterpret_cast<const char*>(response.data()), response.size()));
        if (expected != 0 && response.size() >= expected) {
            response.resize(expected);
            break;
        }
    }
    return response;
}

std::string rtsp_request(std::string_view method, std::string_view path,
    unsigned int sequence, std::span<const std::uint8_t> body,
    std::string_view content_type) {
    auto request = std::string(method) + " " + std::string(path) +
        " RTSP/1.0\r\nCSeq: " + std::to_string(sequence) +
        "\r\nUser-Agent: AirPlay/550.10\r\nConnection: keep-alive\r\n";
    if (!content_type.empty())
        request += "Content-Type: " + std::string(content_type) + "\r\n";
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    request.append(reinterpret_cast<const char*>(body.data()), body.size());
    return request;
}

bool successful_rtsp_response(const std::vector<std::uint8_t>& response) {
    if (response.empty()) return false;
    const std::string_view text(reinterpret_cast<const char*>(response.data()),
        response.size());
    return text.starts_with("RTSP/1.0 200");
}

std::span<const std::uint8_t> response_body(
    const std::vector<std::uint8_t>& response) {
    constexpr std::array delimiter{std::uint8_t{'\r'}, std::uint8_t{'\n'},
        std::uint8_t{'\r'}, std::uint8_t{'\n'}};
    const auto body = std::search(response.begin(), response.end(),
        delimiter.begin(), delimiter.end());
    return body == response.end() ? std::span<const std::uint8_t>{} :
        std::span<const std::uint8_t>(body + delimiter.size(), response.end());
}

class BinaryPlist final {
public:
    explicit BinaryPlist(std::span<const std::uint8_t> bytes) : bytes_(bytes) {
        initialize();
    }

    bool valid() const noexcept { return valid_; }

    std::optional<std::uint64_t> root() const noexcept {
        return valid_ ? std::optional<std::uint64_t>(top_object_) :
            std::nullopt;
    }

    std::optional<std::uint64_t> dictionary_value(std::uint64_t dictionary,
        std::string_view key) const {
        const auto object = object_at(dictionary);
        if (!object || object->type != 0xD ||
            object->count > std::numeric_limits<std::size_t>::max())
            return std::nullopt;
        const auto count = static_cast<std::size_t>(object->count);
        const auto values = object->data_offset + count * ref_size_;
        for (std::size_t index = 0; index < count; ++index) {
            const auto key_ref = read_ref(object->data_offset + index * ref_size_);
            const auto value_ref = read_ref(values + index * ref_size_);
            if (!key_ref || !value_ref) return std::nullopt;
            const auto key_text = string_value(*key_ref);
            if (key_text && *key_text == key) return value_ref;
        }
        return std::nullopt;
    }

    std::optional<std::uint64_t> array_value(std::uint64_t array,
        std::size_t index) const {
        const auto object = object_at(array);
        if (!object || object->type != 0xA ||
            index >= object->count ||
            object->count > (bytes_.size() - object->data_offset) / ref_size_)
            return std::nullopt;
        return read_ref(object->data_offset + index * ref_size_);
    }

    std::optional<std::uint64_t> integer_value(std::uint64_t reference) const {
        const auto object = object_at(reference);
        if (!object || object->type != 0x1 || object->count == 0 ||
            object->count > 8)
            return std::nullopt;
        return read_be(object->data_offset,
            static_cast<std::size_t>(object->count));
    }

    std::optional<std::string> string_value(std::uint64_t reference) const {
        const auto object = object_at(reference);
        if (!object || object->count > std::numeric_limits<std::size_t>::max())
            return std::nullopt;
        const auto count = static_cast<std::size_t>(object->count);
        if (object->type == 0x5) {
            if (count > bytes_.size() - object->data_offset) return std::nullopt;
            return std::string(reinterpret_cast<const char*>(
                bytes_.data() + object->data_offset), count);
        }
        if (object->type != 0x6 || count >
                (bytes_.size() - object->data_offset) / 2U)
            return std::nullopt;
        std::string result;
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto code_unit = read_be(object->data_offset + index * 2U, 2);
            if (!code_unit || *code_unit > 0x7f) return std::nullopt;
            result.push_back(static_cast<char>(*code_unit));
        }
        return result;
    }

private:
    struct Object {
        std::uint8_t type{};
        std::uint64_t count{};
        std::size_t data_offset{};
    };

    static std::optional<std::uint64_t> read_be(
        std::span<const std::uint8_t> bytes, std::size_t offset,
        std::size_t width) noexcept {
        if (width == 0 || width > 8 || offset > bytes.size() ||
            width > bytes.size() - offset)
            return std::nullopt;
        std::uint64_t value{};
        for (std::size_t index = 0; index < width; ++index)
            value = (value << 8U) | bytes[offset + index];
        return value;
    }

    std::optional<std::uint64_t> read_be(std::size_t offset,
        std::size_t width) const noexcept {
        return read_be(bytes_, offset, width);
    }

    void initialize() {
        if (bytes_.size() < 40 ||
            std::memcmp(bytes_.data(), "bplist00", 8) != 0)
            return;
        const auto trailer = bytes_.size() - 32U;
        offset_size_ = bytes_[trailer + 6U];
        ref_size_ = bytes_[trailer + 7U];
        const auto object_count = read_be(trailer + 8U, 8U);
        const auto top_object = read_be(trailer + 16U, 8U);
        const auto offset_table = read_be(trailer + 24U, 8U);
        if (!object_count || !top_object || !offset_table ||
            offset_size_ == 0 || offset_size_ > 8 || ref_size_ == 0 ||
            ref_size_ > 8 || *object_count == 0 ||
            *top_object >= *object_count || *offset_table > trailer ||
            *object_count > (trailer - static_cast<std::size_t>(*offset_table)) /
                offset_size_ || *object_count > std::numeric_limits<std::size_t>::max())
            return;
        const auto count = static_cast<std::size_t>(*object_count);
        offsets_.reserve(count);
        const auto table = static_cast<std::size_t>(*offset_table);
        for (std::size_t index = 0; index < count; ++index) {
            const auto offset = read_be(table + index * offset_size_, offset_size_);
            if (!offset || *offset >= table || *offset >= bytes_.size()) return;
            offsets_.push_back(static_cast<std::size_t>(*offset));
        }
        top_object_ = *top_object;
        valid_ = true;
    }

    std::optional<Object> object_at(std::uint64_t reference) const {
        if (!valid_ || reference >= offsets_.size()) return std::nullopt;
        const auto offset = offsets_[static_cast<std::size_t>(reference)];
        if (offset >= bytes_.size()) return std::nullopt;
        const auto marker = bytes_[offset];
        const auto type = static_cast<std::uint8_t>(marker >> 4U);
        const auto info = static_cast<std::uint8_t>(marker & 0x0fU);
        std::size_t cursor = offset + 1U;
        std::uint64_t count{};
        if (type == 0x1 || type == 0x2) {
            if (info > 3) return std::nullopt;
            count = std::uint64_t{1} << info;
        }
        else if (info == 0x0f) {
            if (cursor >= bytes_.size()) return std::nullopt;
            const auto length_marker = bytes_[cursor++];
            if ((length_marker >> 4U) != 0x1 ||
                (length_marker & 0x0fU) > 3)
                return std::nullopt;
            const auto width = std::size_t{1} << (length_marker & 0x0fU);
            const auto length = read_be(cursor, width);
            if (!length) return std::nullopt;
            cursor += width;
            count = *length;
        }
        else count = info;

        std::uint64_t unit{1};
        switch (type) {
        case 0x0:
            count = 0;
            unit = 0;
            break;
        case 0x1:
        case 0x2:
        case 0x4:
        case 0x5:
            break;
        case 0x3:
            count = 8;
            break;
        case 0x6:
            unit = 2;
            break;
        case 0x8:
            count = info + 1U;
            break;
        case 0xA:
            unit = ref_size_;
            break;
        case 0xD:
            unit = static_cast<std::uint64_t>(ref_size_) * 2U;
            break;
        default:
            return std::nullopt;
        }
        if (unit != 0 && count >
                std::numeric_limits<std::uint64_t>::max() / unit)
            return std::nullopt;
        const auto payload_size = count * unit;
        if (payload_size > std::numeric_limits<std::size_t>::max() ||
            cursor > bytes_.size() || static_cast<std::size_t>(payload_size) >
                bytes_.size() - cursor)
            return std::nullopt;
        return Object{type, count, cursor};
    }

    std::optional<std::uint64_t> read_ref(std::size_t offset) const noexcept {
        const auto value = read_be(offset, ref_size_);
        if (!value || *value >= offsets_.size()) return std::nullopt;
        return value;
    }

    std::span<const std::uint8_t> bytes_;
    std::vector<std::size_t> offsets_;
    std::uint8_t offset_size_{};
    std::uint8_t ref_size_{};
    std::uint64_t top_object_{};
    bool valid_{};
};

std::optional<std::uint64_t> plist_integer(const BinaryPlist& plist,
    std::uint64_t dictionary, std::string_view key) {
    const auto value = plist.dictionary_value(dictionary, key);
    return value ? plist.integer_value(*value) : std::nullopt;
}
std::vector<std::uint8_t> first_setup_plist(unsigned short timing_port) {
    // Binary plist fixture generated from the documented first mirror SETUP.
    constexpr std::uint8_t bytes[] = {
        0x62, 0x70, 0x6c, 0x69, 0x73, 0x74, 0x30, 0x30, 0xd8, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x53, 0x65, 0x69, 0x76, 0x54, 0x65, 0x6b, 0x65, 0x79, 0x5a, 0x74,
        0x69, 0x6d, 0x69, 0x6e, 0x67, 0x50, 0x6f, 0x72, 0x74, 0x5f, 0x10, 0x18,
        0x69, 0x73, 0x53, 0x63, 0x72, 0x65, 0x65, 0x6e, 0x4d, 0x69, 0x72, 0x72,
        0x6f, 0x72, 0x69, 0x6e, 0x67, 0x53, 0x65, 0x73, 0x73, 0x69, 0x6f, 0x6e,
        0x54, 0x6e, 0x61, 0x6d, 0x65, 0x58, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65,
        0x49, 0x44, 0x55, 0x6d, 0x6f, 0x64, 0x65, 0x6c, 0x59, 0x6f, 0x73, 0x56,
        0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x4f, 0x10, 0x10, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x4f, 0x10, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x11, 0xa5, 0x5a, 0x09, 0x5f, 0x10, 0x13, 0x52,
        0x6f, 0x75, 0x74, 0x65, 0x20, 0x42, 0x69, 0x6e, 0x64, 0x69, 0x6e, 0x67,
        0x20, 0x53, 0x6d, 0x6f, 0x6b, 0x65, 0x5f, 0x10, 0x11, 0x30, 0x32, 0x3a,
        0x30, 0x30, 0x3a, 0x30, 0x30, 0x3a, 0x30, 0x30, 0x3a, 0x30, 0x30, 0x3a,
        0x30, 0x31, 0x5a, 0x69, 0x50, 0x68, 0x6f, 0x6e, 0x65, 0x31, 0x34, 0x2c,
        0x32, 0x54, 0x31, 0x38, 0x2e, 0x30, 0x00, 0x08, 0x00, 0x19, 0x00, 0x1d,
        0x00, 0x22, 0x00, 0x2d, 0x00, 0x48, 0x00, 0x4d, 0x00, 0x56, 0x00, 0x5c,
        0x00, 0x66, 0x00, 0x79, 0x00, 0xc4, 0x00, 0xc7, 0x00, 0xc8, 0x00, 0xde,
        0x00, 0xf2, 0x00, 0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
    };
    std::vector<std::uint8_t> result(std::begin(bytes), std::end(bytes));
    constexpr std::size_t port_marker = 196;
    if (result.size() <= port_marker + 2 || result[port_marker] != 0x11 ||
        result[port_marker + 1] != 0xa5 || result[port_marker + 2] != 0x5a)
        return {};
    result[port_marker + 1] = static_cast<std::uint8_t>(timing_port >> 8U);
    result[port_marker + 2] = static_cast<std::uint8_t>(timing_port);
    return result;
}
} // namespace
