#include "Device/UsbDeviceRepair.h"
#include "Device/UsbDeviceLease.h"
#include "Device/UsbRepairVerification.h"
#include "Device/AppleUsbDiscovery.h"
#include "Transport/LibUsb0Transport.h"
#include "Transport/UsbMuxClient.h"
#include "Logging.h"
#include <ShellAPI.h>
#include <objbase.h>
#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <thread>

namespace audiobridge {
void repair_usb_device_connection(std::string_view serial) {
    namespace device = iPhoneMirror::device;
    namespace transport = iPhoneMirror::transport;
    namespace logging = iPhoneMirror::logging;
    const auto target = select_usb_repair_target(list_usb_repair_targets(), serial);
    { UsbDeviceLease available(target.serial); } // Fail before UAC if another session owns it.
    std::wstring module(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length >= module.size()) throw std::runtime_error("无法定位 USB 修复助手");
    module.resize(length);
    const auto helper = std::filesystem::path(module).parent_path() / L"AudioBridge.UsbRepair.exe";
    if (!std::filesystem::is_regular_file(helper)) throw std::runtime_error("缺少 AudioBridge.UsbRepair.exe，请使用完整程序包");
    // The target validator rejects quotes, whitespace and command metacharacters.
    // ShellExecute launches this exact executable; no command interpreter is used.
    const auto parameters = L"restart \"" + target.instance_id + L"\" \"" +
        std::wstring(target.serial.begin(), target.serial.end()) + L"\"";
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) throw std::runtime_error("无法初始化 Windows 修复授权界面");
    struct ComRelease { ~ComRelease() { CoUninitialize(); } } release_com;
    SHELLEXECUTEINFOW launch{}; launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    launch.lpVerb = L"runas"; launch.lpFile = helper.c_str(); launch.lpParameters = parameters.c_str();
    launch.nShow = SW_HIDE;
    logging::write(std::format("usb_repair action=request device={}", logging::fingerprint(serial)));
    if (!ShellExecuteExW(&launch)) {
        const auto error = GetLastError();
        if (error == ERROR_CANCELLED) throw std::runtime_error("已取消 USB 修复的管理员授权");
        throw std::runtime_error(std::format("无法启动 USB 修复助手，Windows 错误 {}", error));
    }
    if (!launch.hProcess) throw std::runtime_error("未取得 USB 修复助手的进程句柄");
    const auto process = std::unique_ptr<void, decltype(&CloseHandle)>(launch.hProcess, &CloseHandle);
    const auto wait = WaitForSingleObject(launch.hProcess, 30000);
    // Never kill a helper inside the OS restart. It retains the device lease
    // until Windows returns, preventing another capture/repair from racing it.
    if (wait == WAIT_TIMEOUT) throw std::runtime_error("Windows 设备重启尚未完成；请等待后重新检查，必要时拔插设备");
    if (wait != WAIT_OBJECT_0) throw std::runtime_error(std::format("等待 USB 修复失败，Windows 错误 {}", GetLastError()));
    DWORD exit_code{};
    if (!GetExitCodeProcess(launch.hProcess, &exit_code)) throw std::runtime_error("无法读取 USB 修复结果");
    logging::write(std::format("usb_repair action=restart_completed exit_code={}", exit_code));
    if (exit_code == ERROR_SUCCESS_REBOOT_REQUIRED) throw std::runtime_error("Windows 要求重启电脑才能完成设备恢复");
    if (exit_code == ERROR_BUSY) throw std::runtime_error("设备正被其他采集或修复会话占用，请关闭后重试");
    if (exit_code == ERROR_NOT_SUPPORTED) throw std::runtime_error("该设备的 USB 驱动绑定不符合修复条件，请运行驱动检查");
    if (exit_code != ERROR_SUCCESS) throw std::runtime_error(std::format("USB 设备重启失败，Windows 错误 {}；未确认恢复", exit_code));

    UsbDeviceLease verification(target.serial);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    UsbRepairVerification readiness;
    while (std::chrono::steady_clock::now() < deadline) {
        bool mux_present{};
        const bool normal_pnp = device::is_apple_normal_usb_stack_present(target.serial);
        if (normal_pnp) {
            try {
                transport::UsbMuxClient mux(27015);
                for (const auto& entry : mux.list_devices())
                    if (entry.connection_type == "USB" && transport::apple_usb_serial_equal(entry.serial, target.serial))
                        mux_present = true;
            } catch (...) {}
        }
        if (readiness.observe(normal_pnp, mux_present)) {
            const auto usb = transport::probe_libusb0_device(target.serial);
            if (usb.active_configuration == 0 || transport::is_libusb0_quicktime_configuration_active(usb))
                throw std::runtime_error("设备已重启，但尚未恢复普通 USB 配置，请重新插拔");
            logging::write("usb_repair result=verified pnp=true usbmux=true usb_open=true configuration_read=true");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    logging::write(logging::Level::Warning, "usb_repair", "result=verification_timeout");
    throw std::runtime_error("设备重启已执行，但 Apple USB 连接未恢复，请解锁并重新插拔");
}
}
