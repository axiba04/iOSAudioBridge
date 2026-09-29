#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace audiobridge {
struct UsbRepairTarget {
    std::wstring instance_id;
    std::string serial;
    std::wstring name;
};
// Only an exact supported Apple composite parent can be restarted. No wildcards,
// child interfaces, topology fallback, or other Apple accessories are accepted.
bool valid_usb_repair_target(const UsbRepairTarget& target) noexcept;
std::vector<UsbRepairTarget> list_usb_repair_targets();
UsbRepairTarget select_usb_repair_target(std::span<const UsbRepairTarget> targets,
    std::string_view serial);
// Elevated helper only. Returns a Win32 error, including 3010 for reboot required.
std::uint32_t restart_usb_device_node(const UsbRepairTarget& target);

// User-invoked client: elevates the helper, then independently verifies PnP,
// Apple discovery and read-only libusb access. Never starts audio output.
void repair_usb_device_connection(std::string_view serial);
}
