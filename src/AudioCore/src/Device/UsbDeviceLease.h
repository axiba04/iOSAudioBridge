#pragma once
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace audiobridge {
// Capture and the elevated repair helper use the same per-device exclusion.
// Keep the existing name so older bridge processes also block a repair.
class UsbDeviceLease final {
public:
    explicit UsbDeviceLease(std::string serial) {
        serial.erase(std::remove(serial.begin(), serial.end(), '-'), serial.end());
        if (serial.empty() || !std::all_of(serial.begin(), serial.end(),
                [](unsigned char c) { return std::isalnum(c) && c < 128; }))
            throw std::invalid_argument("Invalid USB device serial");
        std::transform(serial.begin(), serial.end(), serial.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto name = L"Local\\iOSAudioBridge.USB." +
            std::wstring(serial.begin(), serial.end());
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle_) throw std::runtime_error("Cannot create USB device lease");
        const auto wait = WaitForSingleObject(handle_, 0);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            CloseHandle(handle_); handle_ = nullptr;
            throw std::runtime_error("This iPhone/iPad is busy in another capture or repair session");
        }
    }
    ~UsbDeviceLease() { if (handle_) { ReleaseMutex(handle_); CloseHandle(handle_); } }
    UsbDeviceLease(const UsbDeviceLease&) = delete;
    UsbDeviceLease& operator=(const UsbDeviceLease&) = delete;
private:
    HANDLE handle_{};
};
}
