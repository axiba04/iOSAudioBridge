#include "Device/UsbDeviceRepair.h"
#include "Device/UsbDeviceLease.h"
#include <iostream>

// Deliberately has no libusb dependency: a broken filter handle is the reason
// this separate, explicitly elevated recovery path exists.
int wmain(int argc, wchar_t** argv) {
    if (argc != 4 || std::wstring_view(argv[1]) != L"restart") return ERROR_INVALID_PARAMETER;
    const std::wstring serial(argv[3]);
    if (!std::all_of(serial.begin(), serial.end(), [](wchar_t c) { return c > 0 && c < 128; }))
        return ERROR_INVALID_PARAMETER;
    const audiobridge::UsbRepairTarget target{argv[2], std::string(serial.begin(), serial.end()), {}};
    if (!audiobridge::valid_usb_repair_target(target)) return ERROR_INVALID_PARAMETER;
    try {
        audiobridge::UsbDeviceLease lease(target.serial);
        try { return static_cast<int>(audiobridge::restart_usb_device_node(target)); }
        catch (const std::exception& e) { std::cerr << e.what() << '\n'; return ERROR_NOT_FOUND; }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return ERROR_BUSY; }
}
