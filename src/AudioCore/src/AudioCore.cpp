#include "AudioCore.h"
#include "Session/AudioSession.h"
#include "Device/DeviceCatalog.h"
#include <unordered_map>
#include <memory>
#include <atomic>
#include <cmath>

namespace {
std::mutex handles_mutex;
std::unordered_map<uintptr_t,std::shared_ptr<audiobridge::AudioSession>> handles;
std::atomic<uintptr_t> next_handle{1};
thread_local wchar_t last_error[1024]{};
void set_error(const char* message) noexcept {
    if(!MultiByteToWideChar(CP_UTF8,0,message,-1,last_error,1024))
        audiobridge::copy_text(last_error,L"Native operation failed; error text unavailable");
}
std::shared_ptr<audiobridge::AudioSession> find(AB_Handle handle) {
    std::scoped_lock lock(handles_mutex);
    auto found=handles.find(reinterpret_cast<uintptr_t>(handle));
    if(found==handles.end()) throw std::invalid_argument("Invalid AudioCore handle");
    return found->second;
}
template<class F> int32_t boundary(F&& fn) noexcept {
    try {last_error[0]=0;fn();return AB_OK;}
    catch(const std::invalid_argument& e){set_error(e.what());return AB_INVALID_ARGUMENT;}
    catch(const std::exception& e){set_error(e.what());return AB_FAILURE;}
    catch(...){set_error("Unknown native error");return AB_FAILURE;}
}
template<class T,class F> int32_t enumerate(T* items,uint32_t* count,F&& fn) noexcept {
    if(!count) {set_error("Device count pointer is required");return AB_INVALID_ARGUMENT;}
    int32_t result=AB_OK;
    auto error=boundary([&]{
        const auto values=fn();const auto capacity=*count;*count=static_cast<uint32_t>(values.size());
        if(!items) return;
        if(capacity<values.size()){result=AB_BUFFER_TOO_SMALL;return;}
        std::copy(values.begin(),values.end(),items);
    });return error==AB_OK?result:error;
}
}
int32_t __cdecl ab_list_devices(AB_Device* items,uint32_t* count){return enumerate(items,count,audiobridge::list_phones);}
int32_t __cdecl ab_list_outputs(AB_OutputDevice* items,uint32_t* count){return enumerate(items,count,audiobridge::list_outputs);}
int32_t __cdecl ab_create(AB_Handle* handle) {
    if(!handle) {set_error("Handle output pointer is required");return AB_INVALID_ARGUMENT;}*handle=nullptr;
    return boundary([&]{auto session=std::make_shared<audiobridge::AudioSession>();auto id=next_handle.fetch_add(1);std::scoped_lock lock(handles_mutex);handles.emplace(id,std::move(session));*handle=reinterpret_cast<AB_Handle>(id);});
}
int32_t __cdecl ab_start(AB_Handle handle,const AB_Config* config) {
    return boundary([&]{
        if(!config || config->struct_size!=sizeof(AB_Config) || config->version!=AB_VERSION || config->reserved ||
           !std::char_traits<wchar_t>::find(config->device_id,128,L'\0') || !std::char_traits<wchar_t>::find(config->output_id,512,L'\0'))
            throw std::invalid_argument("Invalid configuration size, version, reserved field or string termination");
        find(handle)->start(*config);
    });
}
int32_t __cdecl ab_stop(AB_Handle handle) {return boundary([&]{auto session=find(handle);session->stop();if(session->status().state==AB_ERROR) throw std::runtime_error(audiobridge::narrow(session->status().message));});}
void __cdecl ab_destroy(AB_Handle handle) {
    (void)boundary([&]{std::shared_ptr<audiobridge::AudioSession> session;{std::scoped_lock lock(handles_mutex);auto it=handles.find(reinterpret_cast<uintptr_t>(handle));if(it==handles.end()) return;session=std::move(it->second);handles.erase(it);}session->stop();});
}
int32_t __cdecl ab_get_status(AB_Handle handle,AB_Status* status) {
    return boundary([&]{
        if(!status || status->struct_size!=sizeof(AB_Status) || status->version!=AB_VERSION)
            throw std::invalid_argument("Invalid status size or version");
        *status=find(handle)->status();
    });
}
int32_t __cdecl ab_set_volume(AB_Handle handle,float volume){return boundary([&]{find(handle)->volume(volume);});}
int32_t __cdecl ab_last_error(wchar_t* buffer,uint32_t capacity) {
    if(!buffer || !capacity) return AB_INVALID_ARGUMENT;
    const auto length=std::char_traits<wchar_t>::length(last_error);
    const auto n=std::min<size_t>(capacity-1,length);std::copy_n(last_error,n,buffer);buffer[n]=0;
    return n==length?AB_OK:AB_BUFFER_TOO_SMALL;
}
uint32_t __cdecl ab_abi_size(uint32_t structure){switch(structure){case 1:return sizeof(AB_Device);case 2:return sizeof(AB_OutputDevice);case 3:return sizeof(AB_Config);case 4:return sizeof(AB_Status);default:return 0;}}
