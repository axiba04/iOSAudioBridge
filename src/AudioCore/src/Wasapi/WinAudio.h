#pragma once
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <format>
#include <stdexcept>
namespace audiobridge {
using Microsoft::WRL::ComPtr;
inline void check_hr(HRESULT hr,const char* operation) {
    if(FAILED(hr)) throw std::runtime_error(std::format("{}: HRESULT 0x{:08X}",operation,static_cast<unsigned>(hr)));
}
class ComScope {
public:
    ComScope() : hr_(CoInitializeEx(nullptr,COINIT_MULTITHREADED)) { if(hr_!=RPC_E_CHANGED_MODE) check_hr(hr_,"CoInitializeEx"); }
    ~ComScope(){ if(SUCCEEDED(hr_)) CoUninitialize(); }
private: HRESULT hr_;
};
}
