#include "iPhoneMirror/CoreApi.h"
#include <Windows.h>
#include <conio.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic_bool quit{};
BOOL WINAPI console_control(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        quit = true;
        return TRUE;
    }
    return FALSE;
}
std::string utf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
struct CoreLifetime {
    ~CoreLifetime() { im_shutdown(); }
};
struct SessionLifetime {
    iPhoneMirror::SessionHandle handle{};
    int stop() {
        if (!handle) return 0;
        const int result = im_session_stop(handle);
        if (result != 0) std::cerr << "停止提示：" << utf8(im_last_error()) << '\n';
        im_session_destroy(handle);
        handle = 0;
        return result;
    }
    ~SessionLifetime() { stop(); }
};
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(console_control, TRUE);
    std::cout << std::unitbuf;
    int verify_seconds{};
    bool list_only{};
    std::wstring selected_udid;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--list") list_only = true;
        else if (arg == L"--device" && i + 1 < argc) selected_udid = argv[++i];
        else if (arg == L"--verify" && i + 1 < argc) {
            wchar_t* end{};
            const long seconds = wcstol(argv[++i], &end, 10);
            if (*end || seconds < 20 || seconds > 600) {
                std::cerr << "--verify 需要 20 至 600 秒。\n";
                return 2;
            }
            verify_seconds = static_cast<int>(seconds);
        } else {
            std::cout << "用法：iPhoneMirror.UsbAudioOnly.exe [--list] [--device UDID] [--verify 30]\n";
            return arg == L"--help" ? 0 : 2;
        }
    }
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    const auto directory = std::filesystem::path(module).parent_path();
    if (verify_seconds) {
        // Invalidate any earlier pass before discovery/startup can fail.
        std::ofstream result(directory / L"verification-result.json");
        result << "{\"transport_passed\":false,\"completed\":false}\n";
        if (!result) {
            std::cerr << "无法写入验证结果，请检查程序目录写入权限。\n";
            return 1;
        }
    }
    const auto log_path = directory / L"usb-audio-only.log";
    SetEnvironmentVariableW(L"IPHONE_MIRROR_LOG_FILE", log_path.c_str());
    std::cout << "iPhoneMirror USB 仅音频 · 实验版\n"
                 "请先退出原版镜像程序，解锁并信任电脑，在手机上播放声音。\n"
                 "本模式不请求视频；若手机仍发送视频，将停止并报告错误。\n\n";
    if (im_initialize() != 0) {
        std::cerr << "初始化失败：" << utf8(im_last_error()) << '\n';
        return 1;
    }
    CoreLifetime core;
    std::uint32_t count{};
    if (im_refresh_devices(nullptr, &count) != 0 || count == 0) {
        std::cerr << "未发现 USB 设备，请检查连接并解锁手机。\n";
        return 1;
    }
    std::vector<iPhoneMirror::DeviceInfo> records(count);
    auto capacity = count;
    if (im_refresh_devices(records.data(), &capacity) != 0) {
        std::cerr << "设备枚举失败：" << utf8(im_last_error()) << '\n';
        return 1;
    }
    records.resize(std::min(capacity, count));
    std::vector<iPhoneMirror::DeviceInfo> devices;
    std::set<std::wstring> seen;
    for (const auto& device : records) {
        if (!device.usb_connected || !device.udid[0] || !seen.emplace(device.udid).second) continue;
        if (!selected_udid.empty() && selected_udid != device.udid) continue;
        devices.push_back(device);
    }
    if (devices.empty()) {
        std::cerr << "没有匹配的 USB iPhone/iPad。\n";
        return 1;
    }
    for (std::size_t i = 0; i < devices.size(); ++i) {
        std::cout << i + 1 << ". " << utf8(devices[i].name) << ' '
                  << utf8(devices[i].product_type) << " (iOS "
                  << utf8(devices[i].os_version) << ")\n";
        if (list_only) std::cout << "   " << utf8(devices[i].udid) << '\n';
    }
    if (list_only) return 0;
    std::size_t selection{};
    if (devices.size() > 1) {
        if (verify_seconds) {
            std::cerr << "多台 USB 设备已连接，请用 --device 指定。\n";
            return 2;
        }
        std::cout << "输入设备编号：";
        std::size_t number{};
        if (!(std::cin >> number) || number < 1 || number > devices.size()) return 2;
        selection = number - 1;
    }
    SessionLifetime session;
    iPhoneMirror::CaptureOptions options{};
    options.struct_size = sizeof(options);
    options.api_version = iPhoneMirror::ApiVersion;
    options.play_audio = 1;
    options.audio_volume = 1.0F;
    options.reserved[2] = 3; // USB audio-only, no display negotiation.
    if (im_session_create(devices[selection].udid, &options, &session.handle) != 0) {
        std::cerr << "连接失败：" << utf8(im_last_error()) << '\n';
        return 1;
    }
    std::cout << "\nQ：停止并退出   M：静音/恢复   +/-：调节音量\n"
                 "请使用 Q 正常退出，让手机恢复 USB 状态。\n\n";
    bool muted{};
    float volume = 1.0F;
    bool failed{};
    bool unexpected_video{};
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    auto last_print = started - std::chrono::seconds(1);
    auto last_audio = started;
    std::uint64_t previous_audio{};
    iPhoneMirror::CaptureStatus status{};
    status.struct_size = sizeof(status);
    while (!quit) {
        if (im_session_get_status(session.handle, &status) != 0) { failed = true; break; }
        const auto now = Clock::now();
        if (status.audio_packets > previous_audio) {
            last_audio = now;
            previous_audio = status.audio_packets;
        }
        unexpected_video = unexpected_video || status.video_frames != 0;
        if (now - last_print >= std::chrono::seconds(1)) {
            std::cout << utf8(status.message) << " | 音频包=" << status.audio_packets
                      << " 视频包=" << status.video_frames << " | " << status.audio_sample_rate
                      << "Hz " << status.audio_channels << "声道\n";
            last_print = now;
        }
        if (status.state == iPhoneMirror::CaptureState::Error ||
            status.state == iPhoneMirror::CaptureState::Stopped || unexpected_video) {
            std::cerr << "会话结束：" << utf8(status.message) << " (" << status.error_code << ")\n";
            failed = true;
            break;
        }
        if (verify_seconds && now - started >= std::chrono::seconds(verify_seconds)) break;
        if (!verify_seconds && _kbhit()) {
            const int key = _getch();
            if (key == 'q' || key == 'Q' || key == 27) quit = true;
            else if (key == 'm' || key == 'M') {
                muted = !muted;
                im_session_set_audio_enabled(session.handle, muted ? 0 : 1);
                std::cout << (muted ? "已静音\n" : "已恢复声音\n");
            } else if (key == '+' || key == '=' || key == '-') {
                volume = std::clamp(volume + (key == '-' ? -0.1F : 0.1F), 0.0F, 1.0F);
                im_session_set_audio_volume(session.handle, volume);
                std::cout << "音量 " << static_cast<int>(volume * 100 + 0.5F) << "%\n";
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const bool verified = !failed && !quit && verify_seconds && !unexpected_video &&
        status.audio_packets >= 25 && Clock::now() - last_audio < std::chrono::seconds(3) &&
        status.state == iPhoneMirror::CaptureState::Streaming;
    std::cout << "正在停止并恢复 USB，请稍候……\n";
    const int stop_result = session.stop();
    if (verify_seconds) {
        std::ofstream result(directory / L"verification-result.json");
        result << "{\n  \"completed\": true,\n  \"transport_passed\": " << (verified ? "true" : "false")
               << ",\n  \"audio_packets\": " << status.audio_packets
               << ",\n  \"video_packets\": " << status.video_frames
               << ",\n  \"stop_result\": " << stop_result << "\n}\n";
        result.close();
        if (!result) {
            std::cerr << "保存验证结果失败。\n";
            return 1;
        }
        std::cout << (verified ? "通过：持续收到音频，收到的视频包为零。\n" :
            "未通过：未确认持续音频且零视频，请查看日志。\n");
        return verified && stop_result == 0 ? 0 : 1;
    }
    return failed || stop_result != 0 ? 1 : 0;
}
