#include "Device/DeviceCatalog.h"
#include "Device/AppleUsbDiscovery.h"
#include "Device/UsbDeviceRepair.h"
#include "Logging.h"
#include "Transport/UsbMuxClient.h"
#include "Wasapi/WinAudio.h"
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <memory>

namespace audiobridge {
std::wstring widen(std::string_view text) {
    if(text.empty()) return {};
    auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(!n) throw std::runtime_error("Invalid UTF-8");
    std::wstring result(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),n); return result;
}
std::string narrow(std::wstring_view text) {
    if(text.empty()) return {};
    auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(!n) throw std::runtime_error("Invalid UTF-16");
    std::string result(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),n,nullptr,nullptr); return result;
}
std::vector<AB_Device> list_phones() {
    using namespace iPhoneMirror;
    std::vector<AB_Device> result;
    try {
    if(transport::Socket::probe_loopback(27015)) {
    transport::UsbMuxClient mux(27015);
    for(const auto& d:mux.list_devices()) {
        if(d.connection_type!="USB" || !device::is_apple_mobile_capture_product_id(d.product_id)) continue;
        if(!device::is_apple_usb_parent_present(d.serial)) continue;
        AB_Device entry{}; copy_text(entry.id,widen(d.serial)); copy_text(entry.name,L"iPhone / iPad");
        try { entry.paired=mux.has_pair_record(d.serial); } catch(...) {}
        try {
            auto socket=mux.connect_device(d.device_id,62078);
            const auto xml=plist::to_xml(plist::Value::Dict({{"Label",plist::Value::String("iOSAudioBridge")},{"Request",plist::Value::String("GetValue")}}));
            const auto size=static_cast<uint32_t>(xml.size());
            std::vector<uint8_t> frame{uint8_t(size>>24),uint8_t(size>>16),uint8_t(size>>8),uint8_t(size)};
            frame.insert(frame.end(),xml.begin(),xml.end()); socket.send_all(frame);
            const auto header=socket.receive_exact(4);
            const auto length=(uint32_t(header[0])<<24)|(uint32_t(header[1])<<16)|(uint32_t(header[2])<<8)|header[3];
            if(!length || length>1024*1024) throw std::runtime_error("Invalid Lockdown length");
            const auto bytes=socket.receive_exact(length);
            auto response=plist::parse_xml({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
            if(const auto* values=response.find("Value")) {
                entry.accessible=1;
                if(auto v=values->find("DeviceName")) copy_text(entry.name,widen(v->string_or()));
                if(auto v=values->find("ProductType")) copy_text(entry.model,widen(v->string_or()));
                if(auto v=values->find("ProductVersion")) copy_text(entry.os,widen(v->string_or()));
            }
        } catch(...) { /* Presence remains visible while locked/untrusted. */ }
        result.push_back(entry);
    }
    }
    } catch(const std::exception& error) {
        logging::write(std::string("device_discovery usbmux_failed error=")+error.what());
    }
    // Keep physically present phones selectable for explicit repair even if
    // Apple discovery has lost their management interfaces. Do not probe USB
    // handles or change configuration while refreshing the list.
    for(const auto& target:list_usb_repair_targets()) {
        const auto found=std::any_of(result.begin(),result.end(),[&](const auto& entry){
            return device::apple_usb_parent_instance_matches_serial(target.instance_id,narrow(entry.id));
        });
        if(found || target.serial.size()>=std::size(AB_Device{}.id)) continue;
        AB_Device entry{};copy_text(entry.id,widen(target.serial));
        copy_text(entry.name,target.name+L"（USB 待检查）");
        result.push_back(entry);
    }
    return result;
}
std::vector<AB_OutputDevice> list_outputs() {
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator;
    check_hr(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)),"MMDeviceEnumerator");
    ComPtr<IMMDevice> default_device;
    std::wstring default_id;
    if(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,&default_device))) {
        LPWSTR id{}; if(SUCCEEDED(default_device->GetId(&id))) { default_id=id; CoTaskMemFree(id); }
    }
    ComPtr<IMMDeviceCollection> collection;
    check_hr(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&collection),"EnumAudioEndpoints");
    UINT count{}; check_hr(collection->GetCount(&count),"GetCount");
    std::vector<AB_OutputDevice> result;
    for(UINT i=0;i<count;++i) {
        ComPtr<IMMDevice> device; check_hr(collection->Item(i,&device),"Endpoint");
        LPWSTR id{}; check_hr(device->GetId(&id),"Endpoint ID");
        AB_OutputDevice entry{};
        const std::wstring endpoint_id(id); CoTaskMemFree(id);
        if(endpoint_id.size()>=std::size(entry.id)) continue; // Never return a truncated unusable ID.
        copy_text(entry.id,endpoint_id); entry.is_default=(endpoint_id==default_id);
        ComPtr<IPropertyStore> properties;
        check_hr(device->OpenPropertyStore(STGM_READ,&properties),"Endpoint properties");
        PROPVARIANT value; PropVariantInit(&value);
        if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&value)) && value.vt==VT_LPWSTR)
            copy_text(entry.name,value.pwszVal);
        PropVariantClear(&value); result.push_back(entry);
    }
    return result;
}
}
