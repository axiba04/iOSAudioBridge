#include "Device/UsbDeviceRepair.h"
#include "Device/AppleUsbDiscovery.h"
#include <Windows.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace audiobridge {
namespace {
struct SetDeleter {
    void operator()(void* set) const { if (set != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(set); }
};
bool ascii_serial(std::wstring_view value) {
    return !value.empty() && value.size() <= 128 &&
        std::all_of(value.begin(), value.end(), [](wchar_t c) {
            return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') ||
                (c >= L'a' && c <= L'z') || c == L'-';
        });
}
std::vector<wchar_t> property(HDEVINFO set, SP_DEVINFO_DATA& info, DWORD key) {
    DWORD bytes{}, type{};
    SetupDiGetDeviceRegistryPropertyW(set, &info, key, &type, nullptr, 0, &bytes);
    if (!bytes) return {};
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 2, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(set, &info, key, &type,
            reinterpret_cast<BYTE*>(value.data()), bytes, nullptr)) return {};
    return value;
}
bool has_filter(const std::vector<wchar_t>& values, const wchar_t* wanted) {
    for (const wchar_t* p = values.data(); !values.empty() && *p; p += wcslen(p) + 1)
        if (_wcsicmp(p, wanted) == 0) return true;
    return false;
}
}
bool valid_usb_repair_target(const UsbRepairTarget& target) noexcept {
    try {
        if (target.instance_id.size() >= MAX_DEVICE_ID_LEN ||
            !iPhoneMirror::device::is_apple_mobile_capture_parent_instance_id(target.instance_id))
            return false;
        const auto suffix = std::wstring_view(target.instance_id).substr(target.instance_id.rfind(L'\\') + 1);
        const std::wstring serial(target.serial.begin(), target.serial.end());
        return ascii_serial(suffix) && ascii_serial(serial) &&
            iPhoneMirror::device::apple_usb_parent_instance_matches_serial(target.instance_id, target.serial);
    } catch (...) { return false; }
}
std::vector<UsbRepairTarget> list_usb_repair_targets() {
    HDEVINFO raw = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (raw == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot enumerate USB device nodes");
    std::unique_ptr<void, SetDeleter> set(raw);
    std::vector<UsbRepairTarget> targets;
    for (DWORD index{};; ++index) {
        SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(raw, index, &info)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            throw std::runtime_error("Cannot read USB device node");
        }
        wchar_t id[MAX_DEVICE_ID_LEN]{};
        if (!SetupDiGetDeviceInstanceIdW(raw, &info, id, MAX_DEVICE_ID_LEN, nullptr)) continue;
        if (!iPhoneMirror::device::is_apple_mobile_capture_parent_instance_id(id)) continue;
        UsbRepairTarget target; target.instance_id = id;
        const auto suffix = target.instance_id.substr(target.instance_id.rfind(L'\\') + 1);
        if (!ascii_serial(suffix)) continue;
        target.serial.assign(suffix.begin(), suffix.end());
        if (!valid_usb_repair_target(target)) continue;
        auto name = property(raw, info, SPDRP_FRIENDLYNAME);
        if (name.empty()) name = property(raw, info, SPDRP_DEVICEDESC);
        target.name = name.empty() ? L"iPhone / iPad" : name.data();
        targets.push_back(std::move(target));
    }
    return targets;
}
UsbRepairTarget select_usb_repair_target(std::span<const UsbRepairTarget> targets, std::string_view serial) {
    const UsbRepairTarget* match{};
    for (const auto& target : targets) {
        if (!valid_usb_repair_target(target) ||
            !iPhoneMirror::device::apple_usb_parent_instance_matches_serial(target.instance_id, serial)) continue;
        if (match) throw std::runtime_error("More than one USB parent matches the selected phone; repair cancelled");
        match = &target;
    }
    if (!match) throw std::runtime_error("Selected iPhone/iPad is no longer connected; repair cancelled");
    return *match;
}
std::uint32_t restart_usb_device_node(const UsbRepairTarget& target) {
    if (!valid_usb_repair_target(target)) return ERROR_INVALID_PARAMETER;
    // Resolve again after elevation and lease acquisition. An unplug or device
    // swap must never redirect this operation to a different parent.
    const auto current = select_usb_repair_target(list_usb_repair_targets(), target.serial);
    if (_wcsicmp(current.instance_id.c_str(), target.instance_id.c_str()) != 0) return ERROR_NOT_FOUND;
    HDEVINFO raw = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (raw == INVALID_HANDLE_VALUE) return GetLastError();
    std::unique_ptr<void, SetDeleter> set(raw);
    SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
    if (!SetupDiOpenDeviceInfoW(raw, target.instance_id.c_str(), nullptr, 0, &info)) return GetLastError();
    const auto service = property(raw, info, SPDRP_SERVICE);
    const auto upper = property(raw, info, SPDRP_UPPERFILTERS);
    if (service.empty() || _wcsicmp(service.data(), L"usbccgp") != 0 || !has_filter(upper, L"libusb0"))
        return ERROR_NOT_SUPPORTED;
    ULONG status{}, problem{};
    if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) != CR_SUCCESS) return ERROR_NOT_FOUND;
    SP_DEVINSTALL_PARAMS_W install{}; install.cbSize = sizeof(install);
    if (!SetupDiGetDeviceInstallParamsW(raw, &info, &install)) return GetLastError();
    if (install.Flags & DI_DONOTCALLCONFIGMG) return ERROR_SUCCESS_REBOOT_REQUIRED;
    SP_PROPCHANGE_PARAMS change{};
    change.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    change.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    change.StateChange = DICS_PROPCHANGE;
    change.Scope = DICS_FLAG_CONFIGSPECIFIC;
    if (!SetupDiSetClassInstallParamsW(raw, &info, &change.ClassInstallHeader, sizeof(change))) return GetLastError();
    // One atomic restart request, never a disable/enable pair that could leave
    // the phone disabled if the helper is interrupted.
    if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, raw, &info)) return GetLastError();
    if (!SetupDiGetDeviceInstallParamsW(raw, &info, &install)) return GetLastError();
    return install.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART) ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS;
}
}
