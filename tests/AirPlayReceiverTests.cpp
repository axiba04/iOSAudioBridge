// Exercise the bridge's actual launch path with the real receiver DLL.
#include "AirPlayProtocolTestHelpers.h"
#include "Capture/AirPlayAudioSource.h"

namespace {
void check_audio(bool success, const char* message) {
    if (!success) throw std::runtime_error(message);
}
struct TestSocket {
    SOCKET value = INVALID_SOCKET;
    ~TestSocket() { if (value != INVALID_SOCKET) closesocket(value); }
};
struct TestWinsock {
    TestWinsock() {
        WSADATA data{};
        check_audio(WSAStartup(MAKEWORD(2, 2), &data) == 0, "WSAStartup failed");
    }
    ~TestWinsock() { WSACleanup(); }
};
// Binary plist: streams=[{type:96, audioFormat:0x40000, ct:2,
// controlPort:0, spf:352, sr:44100}]. ALAC, stereo 16-bit 44.1 kHz.
constexpr std::uint8_t audio_setup_plist[] = {
    0x62, 0x70, 0x6c, 0x69, 0x73, 0x74, 0x30, 0x30, 0xd1, 0x01, 0x02, 0x57,
    0x73, 0x74, 0x72, 0x65, 0x61, 0x6d, 0x73, 0xa1, 0x03, 0xd6, 0x04, 0x05,
    0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x54, 0x74,
    0x79, 0x70, 0x65, 0x5b, 0x61, 0x75, 0x64, 0x69, 0x6f, 0x46, 0x6f, 0x72,
    0x6d, 0x61, 0x74, 0x52, 0x63, 0x74, 0x5b, 0x63, 0x6f, 0x6e, 0x74, 0x72,
    0x6f, 0x6c, 0x50, 0x6f, 0x72, 0x74, 0x53, 0x73, 0x70, 0x66, 0x52, 0x73,
    0x72, 0x10, 0x60, 0x12, 0x00, 0x04, 0x00, 0x00, 0x10, 0x02, 0x10, 0x00,
    0x12, 0x00, 0x00, 0x01, 0x60, 0x12, 0x00, 0x00, 0xac, 0x44, 0x08, 0x0b,
    0x13, 0x15, 0x22, 0x27, 0x33, 0x36, 0x42, 0x46, 0x49, 0x4b, 0x50, 0x52,
    0x54, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5e,
};
void expect_ok(const std::vector<std::uint8_t>& response, const char* step) {
    const std::string text(response.begin(), response.end());
    std::cout << step << ": " << text.substr(0, text.find("\r\n")) << '\n';
    if (!successful_rtsp_response(response))
        throw std::runtime_error(std::string(step) + " rejected");
}
void probe_audio_session() {
    const in_addr loopback{.S_un = {.S_addr = htonl(INADDR_LOOPBACK)}};
    TestSocket timing{WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
        nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT)};
    check_audio(timing.value != INVALID_SOCKET, "Timing socket failed");
    sockaddr_in endpoint{AF_INET, 0, loopback, {}};
    check_audio(bind(timing.value, reinterpret_cast<const sockaddr*>(&endpoint),
        sizeof(endpoint)) == 0, "Timing bind failed");
    int size = sizeof(endpoint);
    check_audio(getsockname(timing.value, reinterpret_cast<sockaddr*>(&endpoint),
        &size) == 0, "Timing port failed");
    // A sender must answer timing packets; otherwise the receiver's timing
    // worker remains blocked waiting for a reply when TEARDOWN joins it.
    std::jthread timing_responder([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(timing.value, &readable);
            timeval timeout{0, 50000};
            if (select(0, &readable, nullptr, nullptr, &timeout) != 1) continue;
            std::array<std::uint8_t, 128> packet{};
            sockaddr_in sender{};
            int sender_size = sizeof(sender);
            const auto count = recvfrom(timing.value,
                reinterpret_cast<char*>(packet.data()), static_cast<int>(packet.size()),
                0, reinterpret_cast<sockaddr*>(&sender), &sender_size);
            if (count < 32) continue;
            std::array<std::uint8_t, 32> reply{0x80, 0xd3, packet[2], packet[3]};
            std::copy_n(packet.begin() + count - 8, 8, reply.begin() + 8);
            FILETIME now{};
            GetSystemTimeAsFileTime(&now);
            const auto ticks = (std::uint64_t{now.dwHighDateTime} << 32) | now.dwLowDateTime;
            const auto ntp = ((ticks / 10000000 - 9435484800ULL) << 32) |
                (((ticks % 10000000) << 32) / 10000000);
            for (int i = 0; i < 8; ++i) {
                reply[16 + i] = reply[24 + i] = static_cast<std::uint8_t>(ntp >> (56 - i * 8));
            }
            sendto(timing.value, reinterpret_cast<const char*>(reply.data()),
                static_cast<int>(reply.size()), 0,
                reinterpret_cast<const sockaddr*>(&sender), sender_size);
        }
    });
    TestSocket control{connect_with_retry(5002, loopback)};
    check_audio(control.value != INVALID_SOCKET, "Receiver connection failed");
    std::array<std::uint8_t, 16> fairplay_setup{};
    std::memcpy(fairplay_setup.data(), "FPLY", 4);
    fairplay_setup[4] = 3;
    std::array<std::uint8_t, 164> fairplay_handshake{};
    std::memcpy(fairplay_handshake.data(), "FPLY", 4);
    fairplay_handshake[4] = 3;
    expect_ok(socket_request(control.value, rtsp_request("POST", "/fp-setup",
        1, fairplay_setup, "application/octet-stream")), "FairPlay setup");
    expect_ok(socket_request(control.value, rtsp_request("POST", "/fp-setup",
        2, fairplay_handshake, "application/octet-stream")), "FairPlay handshake");

    auto setup = first_setup_plist(ntohs(endpoint.sin_port));
    // The upstream fixture marks isScreenMirroringSession=true. Explicitly
    // false reproduces the iPad music-picker rejection in preview.4.
    check_audio(setup.size() > 199 && setup[199] == 0x09, "Setup fixture changed");
    setup[199] = 0x08;
    expect_ok(socket_request(control.value, rtsp_request("SETUP", "/stream",
        3, setup, "application/x-apple-binary-plist")), "Audio-only session SETUP");
    const auto response = socket_request(control.value, rtsp_request("SETUP",
        "/stream", 4, audio_setup_plist, "application/x-apple-binary-plist"));
    expect_ok(response, "Audio stream SETUP (type 96, no video stream)");
    const BinaryPlist plist(response_body(response));
    const auto root = plist.root();
    const auto streams = root ? plist.dictionary_value(*root, "streams") : std::nullopt;
    const auto stream = streams ? plist.array_value(*streams, 0) : std::nullopt;
    check_audio(stream.has_value(), "Receiver returned no audio stream");
    const auto type = plist_integer(plist, *stream, "type");
    const auto data = plist_integer(plist, *stream, "dataPort");
    const auto control_port = plist_integer(plist, *stream, "controlPort");
    check_audio(type == 96 && data && *data > 0 && *data <= 65535 &&
        control_port && *control_port > 0 && *control_port <= 65535,
        "Invalid audio stream transport ports");
    std::cout << "Audio transport negotiated: type=96 dataPort=" << *data
        << " controlPort=" << *control_port << '\n';
    expect_ok(socket_request(control.value, rtsp_request("RECORD", "/stream",
        5, {}, {})), "RECORD");
    expect_ok(socket_request(control.value, rtsp_request("TEARDOWN", "/stream",
        6, audio_setup_plist, "application/x-apple-binary-plist")), "TEARDOWN audio stream");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        std::cout << std::unitbuf;
        check_audio(argc == 2, "Real receiver executable path required");
        TestWinsock winsock;
        audiobridge::PcmPipe pipe;
        audiobridge::AirPlayAudioSource receiver(pipe, {
            [](const AB_Config&, std::uint32_t) {
                throw std::runtime_error("Unexpected audio payload during handshake test");
            }, [] {}, [] {}});
        AB_Config config{};
        config.volume = 0;
        receiver.start(config, argv[1], L"ZL Audio Bridge Protocol Test");
        const auto deadline = GetTickCount64() + 5000;
        while (receiver.status().state != AB_READY &&
               receiver.status().state != AB_ERROR && GetTickCount64() < deadline)
            Sleep(10);
        check_audio(receiver.status().state == AB_READY, "Receiver did not start");
        // Test teardown and a fresh connection to the same live receiver.
        probe_audio_session();
        probe_audio_session();
        receiver.stop();
        check_audio(receiver.status().state == AB_STOPPED, "Receiver did not stop");
        std::cout << "Real DLL audio-only handshake and reconnect passed; no PCM sent\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
