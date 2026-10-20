#include "Capture/CoreDeviceAudioSource.h"
#include "Capture/CoreDeviceProtocol.h"
#include "Device/DeviceCatalog.h"
#include "Device/UsbDeviceLease.h"
#include "Logging.h"
#include <objbase.h>
#include <cmath>
#include <format>

namespace audiobridge {
namespace {
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h=nullptr):value(h){}
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
std::wstring quote(std::wstring_view text){
    std::wstring result=L"\"";size_t slashes=0;
    for(auto c:text){if(c==L'\\'){++slashes;continue;}result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;}
    result.append(slashes*2,L'\\');return result+L'\"';
}
struct Child {
    Handle job,process,stop;
    ~Child(){
        if(stop.value)SetEvent(stop.value);
        if(process.value && WaitForSingleObject(process.value,3000)==WAIT_TIMEOUT){
            TerminateProcess(process.value,1);WaitForSingleObject(process.value,1000);
        }
    }
};
}
CoreDeviceAudioSource::CoreDeviceAudioSource(PcmPipe& pipe,SourceLifecycle lifecycle)
    :pipe_(pipe),lifecycle_(std::move(lifecycle)){
    status_.struct_size=sizeof(status_);status_.version=AB_VERSION;
}
CoreDeviceAudioSource::~CoreDeviceAudioSource(){stop();}
void CoreDeviceAudioSource::state(uint32_t value,std::wstring_view message,int32_t error){
    std::scoped_lock lock(status_mutex_);status_.state=value;status_.error_code=error;copy_text(status_.message,message);
}
AB_Status CoreDeviceAudioSource::status() const{std::scoped_lock lock(status_mutex_);return status_;}
void CoreDeviceAudioSource::start(const AB_Config& source,const std::filesystem::path& host){
    std::scoped_lock lock(control_mutex_);
    if(running_)throw std::runtime_error("CoreDevice is already running");
    if(worker_.joinable())worker_.join();
    AB_Config config=source;
    if(!config.startup_frames)config.startup_frames=3072;
    if(!config.high_water_frames)config.high_water_frames=4096;
    if(!config.device_id[0] || config.startup_frames<128 || config.high_water_frames<config.startup_frames ||
       config.high_water_frames>7168 || !std::isfinite(config.volume) || config.volume<0 || config.volume>1)
        throw std::invalid_argument("Invalid CoreDevice device or buffering configuration");
    if(!std::filesystem::is_regular_file(host))
        throw std::invalid_argument("Missing CoreDevice/AudioBridge.CoreDeviceHost.exe; keep the complete runtime folder");
    {std::scoped_lock status_lock(status_mutex_);status_={};status_.struct_size=sizeof(status_);status_.version=AB_VERSION;}
    state(AB_CONNECTING,L"正在连接 CoreDevice，请保持设备解锁");running_=true;
    try{worker_=std::jthread([this,config,host](std::stop_token token){run(config,host,token);});}
    catch(...){running_=false;state(AB_ERROR,L"无法启动 CoreDevice 线程",AB_FAILURE);throw;}
}
void CoreDeviceAudioSource::stop(){
    std::scoped_lock lock(control_mutex_);
    if(worker_.joinable()){worker_.request_stop();worker_.join();}
}
void CoreDeviceAudioSource::run(AB_Config config,std::filesystem::path host,std::stop_token token) noexcept{
    std::wstring failure,log_path;
    try{
        UsbDeviceLease lease(narrow(config.device_id));
        wchar_t local[32768]{};
        auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
        if(!length || length>=32768)throw std::runtime_error("LOCALAPPDATA unavailable for CoreDevice log");
        const auto logs=std::filesystem::path(local)/L"iOSAudioBridge-ZL"/L"Logs";
        std::filesystem::create_directories(logs);
        log_path=(logs/(L"CoreDevice-"+std::to_wstring(GetCurrentProcessId())+L".log")).wstring();
        SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE};
        Handle read,write;
        if(!CreatePipe(&read.value,&write.value,&security,65536) || !SetHandleInformation(read.value,HANDLE_FLAG_INHERIT,0))
            throw std::runtime_error("Cannot create CoreDevice audio pipe");
        Handle log(CreateFileW(log_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
        Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
        if(log.value==INVALID_HANDLE_VALUE || input.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open CoreDevice log/input");
        GUID id{};wchar_t nonce[40]{};
        if(FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id,nonce,40))throw std::runtime_error("Cannot create CoreDevice session id");
        const auto stop_name=L"Local\\iOSAudioBridge-CoreDevice-"+std::wstring(nonce);
        Child child;
        child.stop.value=CreateEventW(nullptr,TRUE,FALSE,stop_name.c_str());
        child.job.value=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!child.stop.value || !child.job.value || !SetInformationJobObject(child.job.value,JobObjectExtendedLimitInformation,&limits,sizeof limits))
            throw std::runtime_error("Cannot create CoreDevice process lifetime controls");
        auto command=quote(host.wstring())+L" --udid "+quote(config.device_id)+L" --stop-event "+quote(stop_name);
        STARTUPINFOW startup{};startup.cb=sizeof startup;startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdOutput=write.value;startup.hStdError=log.value;startup.hStdInput=input.value;
        PROCESS_INFORMATION info{};
        if(!CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,
                           nullptr,host.parent_path().c_str(),&startup,&info))
            throw std::runtime_error(std::format("Cannot launch CoreDevice host: {}",GetLastError()));
        child.process.value=info.hProcess;Handle thread(info.hThread);
        if(!AssignProcessToJobObject(child.job.value,child.process.value) || ResumeThread(thread.value)==DWORD(-1))
            throw std::runtime_error("Cannot start CoreDevice host job");
        CloseHandle(write.value);write.value=nullptr;
        pipe_.reset();
        bool prepared=false;auto deadline=GetTickCount64()+90000;
        const auto receive=[&](void* buffer,size_t bytes){
            auto* target=static_cast<uint8_t*>(buffer);
            while(bytes){
                if(token.stop_requested())return false;
                if(prepared){lifecycle_.check_output();if(GetTickCount64()-pipe_.last_packet_ms.load()>500)pipe_.paused=true;}
                DWORD available=0;
                if(!PeekNamedPipe(read.value,nullptr,0,nullptr,&available,nullptr))throw std::runtime_error("CoreDevice audio pipe closed");
                if(available){
                    DWORD got=0;
                    if(!ReadFile(read.value,target,static_cast<DWORD>(std::min<size_t>(bytes,available)),&got,nullptr) || !got)
                        throw std::runtime_error("CoreDevice audio read failed");
                    target+=got;bytes-=got;
                }else{
                    if(WaitForSingleObject(child.process.value,0)!=WAIT_TIMEOUT)throw std::runtime_error("CoreDevice host exited");
                    if(GetTickCount64()>deadline)throw std::runtime_error("CoreDevice received no audio before timeout");
                    Sleep(5);
                }
            }
            return true;
        };
        while(!token.stop_requested()){
            AB_CD_Header header{};
            if(!receive(&header,sizeof header))break;
            if(header.magic!=AB_CD_MAGIC || header.size>AB_CD_MAX_PAYLOAD ||
               (header.type!=AB_CD_STATUS && header.type!=AB_CD_AUDIO && header.type!=AB_CD_ERROR) ||
               (header.type==AB_CD_AUDIO && (!header.size || header.size%4)))
                throw std::runtime_error("Invalid CoreDevice PCM record");
            std::vector<uint8_t> payload(header.size);
            if(header.size && !receive(payload.data(),payload.size()))break;
            if(header.type==AB_CD_ERROR)throw std::runtime_error(std::string(payload.begin(),payload.end()));
            if(header.type==AB_CD_STATUS){
                iPhoneMirror::logging::write("coredevice_host "+std::string(payload.begin(),payload.end()));
                continue;
            }
            if(!prepared){lifecycle_.prepare_output(config,48000);prepared=true;}
            pipe_.paused=false;pipe_.enqueue(payload);lifecycle_.check_output();deadline=GetTickCount64()+10000;
            {std::scoped_lock lock(status_mutex_);++status_.audio_packets;status_.sample_rate=48000;status_.bits=16;status_.channels=2;}
            state(AB_STREAMING,L"CoreDevice 音频 → ZLCompressor → 耳机");
        }
        state(AB_STOPPING,L"正在停止 CoreDevice");lifecycle_.stop_output();pipe_.paused=true;
        SetEvent(child.stop.value);
    }catch(const std::exception& e){if(!token.stop_requested())failure=widen(e.what());}
    catch(...){failure=L"未知 CoreDevice 接收错误";}
    lifecycle_.stop_output();pipe_.paused=true;
    if(!failure.empty()){
        if(!log_path.empty())failure+=L"；日志："+log_path;
        iPhoneMirror::logging::write("coredevice_source failed: "+narrow(failure));state(AB_ERROR,failure,AB_FAILURE);
    }else state(AB_STOPPED,L"CoreDevice 已停止");
    running_=false;
}
}
