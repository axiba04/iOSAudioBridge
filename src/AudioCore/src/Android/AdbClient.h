#pragma once
#include "AudioCore.h"
#include <Windows.h>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace audiobridge::android {
enum class Transport { Usb, Wireless };
std::wstring quote_argument(std::wstring_view value);
std::vector<AB_Device> parse_devices(std::string_view output,Transport transport);
void validate_endpoint(std::wstring_view endpoint);

// Only this client process is owned; the shared adb daemon is never terminated.
class AdbProcess {
public:
    AdbProcess(const std::filesystem::path& executable,const std::vector<std::wstring>& arguments);
    ~AdbProcess();
    AdbProcess(const AdbProcess&)=delete;
    AdbProcess& operator=(const AdbProcess&)=delete;
    bool exited();
    DWORD exit_code() const;
    const std::string& output();
    void terminate() noexcept;
private:
    void drain();
    HANDLE process_{},read_{};
    std::string output_;
};

class AdbClient {
public:
    explicit AdbClient(std::filesystem::path executable):executable_(std::move(executable)){}
    std::string run(const std::vector<std::wstring>& arguments,std::stop_token stop={},DWORD timeout_ms=15000) const;
    std::vector<AB_Device> devices(Transport transport) const;
    void connect(std::wstring_view endpoint) const;
    void pair(std::wstring_view endpoint,std::wstring_view code) const;
    const std::filesystem::path& executable() const noexcept {return executable_;}
private:
    std::filesystem::path executable_;
};
}
