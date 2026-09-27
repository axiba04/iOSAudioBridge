#pragma once

#include <cstdint>

namespace iPhoneMirror::capture::detail {

// A port cycle is only a handoff repair after QuickTime has been disabled.
// Never use it to interrupt an active stream or an unidentified USB device.
[[nodiscard]] constexpr bool can_reconnect_restored_usb_device(
    bool exact_serial, std::uint8_t active_configuration,
    std::uint8_t quicktime_configuration, bool has_management,
    bool has_capture) noexcept {
    return exact_serial && active_configuration != 0 &&
        quicktime_configuration != 0 &&
        active_configuration != quicktime_configuration &&
        has_management && !has_capture;
}

} // namespace iPhoneMirror::capture::detail
