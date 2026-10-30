#include "Capture/AirPlayAudioSource.h"
#include "Capture/AirPlayProtocol.h"
#include "Device/DeviceCatalog.h"
#include "Logging.h"
#include <objbase.h>
#include <sddl.h>
#include <cmath>
#include <format>
#include <unordered_map>

namespace audiobridge {
namespace {
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h=nullptr):value(h){}
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
struct LocalSecurity {
    PSECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
    LocalSecurity(){
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;OW)",SDDL_REVISION_1,&descriptor,nullptr))
            throw std::runtime_error("Cannot create AirPlay IPC security");
        attributes.lpSecurityDescriptor=descriptor;
    }
    ~LocalSecurity(){if(descriptor)LocalFree(descriptor);}
};
std::wstring quote(std::wstring_view text){
    std::wstring result=L"\"";size_t slashes=0;
    for(auto c:text){
        if(c==L'\\'){++slashes;continue;}
        result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;
    }
    result.append(slashes*2,L'\\');return result+L'\"';
}
// Ensures a blocked SDK shutdown cannot outlive the bridge or hold its ports.
struct Child {
    Handle process,stop;
    ~Child(){
        if(stop.value)SetEvent(stop.value);
        if(process.value && WaitForSingleObject(process.value,3000)==WAIT_TIMEOUT){
            TerminateProcess(process.value,1);WaitForSingleObject(process.value,1000);
        }
    }
};
class PipeReader {
public:
    PipeReader(HANDLE pipe,HANDLE process,std::stop_token token,std::function<void()> tick)
        :pipe_(pipe),process_(process),token_(token),tick_(std::move(tick)),event_(CreateEventW(nullptr,TRUE,FALSE,nullptr)){
        if(!event_.value)throw std::runtime_error("Cannot create AirPlay read event");
    }
    bool connect(){
        OVERLAPPED operation{};operation.hEvent=event_.value;ResetEvent(event_.value);
        if(ConnectNamedPipe(pipe_,&operation))return true;
        const auto error=GetLastError();
        if(error==ERROR_PIPE_CONNECTED)return true;
        if(error!=ERROR_IO_PENDING)throw std::runtime_error("AirPlay pipe connection failed");
        DWORD read{};return finish(operation,read,GetTickCount64()+15000);
    }
    bool read(void* data,size_t size,ULONGLONG deadline=0){
        auto* bytes=static_cast<uint8_t*>(data);
        while(size){
            if(token_.stop_requested())return false;
            OVERLAPPED operation{};operation.hEvent=event_.value;ResetEvent(event_.value);
            DWORD read{};
            if(!ReadFile(pipe_,bytes,static_cast<DWORD>(size),&read,&operation)){
                if(GetLastError()!=ERROR_IO_PENDING)throw std::runtime_error("AirPlay receiver pipe closed");
                if(!finish(operation,read,deadline))return false;
            }
            if(!read)throw std::runtime_error("AirPlay receiver ended the stream");
            bytes+=read;size-=read;
        }
        return true;
    }
private:
    bool finish(OVERLAPPED& operation,DWORD& read,ULONGLONG deadline){
        // Cancel and drain before OVERLAPPED or its buffer leaves scope.
        const auto cancel=[&]{CancelIoEx(pipe_,&operation);GetOverlappedResult(pipe_,&operation,&read,TRUE);};
        try{
            for(;;){
                if(token_.stop_requested()){cancel();return false;}
                tick_();
                if(deadline && GetTickCount64()>=deadline)throw std::runtime_error("AirPlay receiver startup timed out");
                const HANDLE waits[]{event_.value,process_};
                const auto wait=WaitForMultipleObjects(2,waits,FALSE,50);
                if(wait==WAIT_OBJECT_0){
                    if(!GetOverlappedResult(pipe_,&operation,&read,FALSE))throw std::runtime_error("AirPlay IPC read failed");
                    return true;
                }
                if(wait!=WAIT_TIMEOUT)throw std::runtime_error("AirPlay receiver exited; check Wireless runtime and ports 5002/7002");
            }
        }catch(...){cancel();throw;}
    }
    HANDLE pipe_,process_;std::stop_token token_;std::function<void()> tick_;Handle event_;
};
}
AirPlayAudioSource::AirPlayAudioSource(PcmPipe& pipe,SourceLifecycle lifecycle)
    :pipe_(pipe),lifecycle_(std::move(lifecycle)){
    status_.struct_size=sizeof(status_);status_.version=AB_VERSION;
}
AirPlayAudioSource::~AirPlayAudioSource(){stop();}
void AirPlayAudioSource::state(uint32_t value,std::wstring_view message,int32_t error){
    std::scoped_lock lock(status_mutex_);status_.state=value;status_.error_code=error;copy_text(status_.message,message);
}
AB_Status AirPlayAudioSource::status() const{std::scoped_lock lock(status_mutex_);return status_;}
void AirPlayAudioSource::start(const AB_Config& source,const std::filesystem::path& host,std::wstring name){
    std::scoped_lock lock(control_mutex_);
    if(running_)throw std::runtime_error("AirPlay receiver is already running");
    if(worker_.joinable())worker_.join();
    AB_Config config=source;
    // Original AirPlay buffering; retain caller-supplied buffer sizes.
    if(!config.startup_frames)config.startup_frames=4096;
    if(!config.high_water_frames)config.high_water_frames=6144;
    if(config.startup_frames<128 || config.high_water_frames<config.startup_frames || config.high_water_frames>7168 ||
       !std::isfinite(config.volume) || config.volume<0 || config.volume>1 || name.empty() || name.size()>80 ||
       name.find(L'\0')!=std::wstring::npos || !std::filesystem::is_regular_file(host))
        throw std::invalid_argument("Invalid AirPlay configuration or missing Wireless/AudioBridge.AirPlayHost.exe");
    iPhoneMirror::logging::write(std::format("airplay_source buffer_config startup_frames={} high_water_frames={}",
        config.startup_frames,config.high_water_frames));
    {std::scoped_lock status_lock(status_mutex_);status_={};status_.struct_size=sizeof(status_);status_.version=AB_VERSION;}
    state(AB_CONNECTING,L"正在启动 AirPlay 接收服务");running_=true;
    try{worker_=std::jthread([this,config,host,name=std::move(name)](std::stop_token token){run(config,host,name,token);});}
    catch(...){running_=false;state(AB_ERROR,L"无法启动 AirPlay 线程",AB_FAILURE);throw;}
}
void AirPlayAudioSource::stop(){
    std::scoped_lock lock(control_mutex_);
    if(worker_.joinable()){worker_.request_stop();worker_.join();}
}
void AirPlayAudioSource::run(AB_Config config,std::filesystem::path host,std::wstring name,std::stop_token token) noexcept{
    using namespace airplay;
    std::wstring failure;
    bool prepared=false;
    try{
        GUID guid{};wchar_t nonce[40]{};
        if(FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid,nonce,40))throw std::runtime_error("Cannot create AirPlay IPC nonce");
        const std::wstring suffix=std::to_wstring(GetCurrentProcessId())+nonce;
        const auto pipe_name=L"\\\\.\\pipe\\ZLAudioBridge-AirPlay-"+suffix;
        const auto stop_name=L"Local\\ZLAudioBridge-AirPlay-Stop-"+suffix;
        LocalSecurity security;
        Handle pipe(CreateNamedPipeW(pipe_name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,256*1024,0,&security.attributes));
        if(pipe.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create AirPlay pipe");
        Child child;
        child.stop.value=CreateEventW(&security.attributes,TRUE,FALSE,stop_name.c_str());
        if(!child.stop.value || GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Cannot create AirPlay stop event");
        auto command=quote(host.wstring())+L" --pipe "+quote(pipe_name)+L" --stop-event "+quote(stop_name)+
            L" --name "+quote(name)+L" --parent-pid "+std::to_wstring(GetCurrentProcessId())+
            L" --mode combined --width 960 --height 540 --fps 30 --raop-port 5002 --airplay-port 7002";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION info{};
        if(!CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,
            host.parent_path().c_str(),&startup,&info))throw std::runtime_error(std::format("Cannot launch AirPlay host: {}",GetLastError()));
        child.process.value=info.hProcess;CloseHandle(info.hThread);
        pipe_.reset();
        PipeReader reader(pipe.value,child.process.value,token,[&]{
            if(prepared){
                lifecycle_.check_output();
                const auto last=pipe_.last_packet_ms.load();
                if(last && GetTickCount64()-last>500)pipe_.paused=true;
            }
        });
        bool ready=false;const auto deadline=GetTickCount64()+15000;
        std::string selected;
        std::unordered_map<std::string,float> remote_volumes;
        uint32_t rate=0;
        if(reader.connect()){
            ULONG client{};
            if(!GetNamedPipeClientProcessId(pipe.value,&client) || client!=info.dwProcessId)
                throw std::runtime_error("Unexpected AirPlay IPC client");
            while(!token.stop_requested()){
                MessageHeader header;
                if(!reader.read(&header,sizeof(header),ready?0:deadline))break;
                validate_header(header);
                std::vector<uint8_t> payload(header.payload_size);
                if(!payload.empty() && !reader.read(payload.data(),payload.size(),ready?0:deadline))break;
                if(header.type==MessageType::Ready){ready=true;state(AB_READY,L"AirPlay 已就绪：在 iPhone 正在播放中选择 "+name);}
                else if(header.type==MessageType::Audio){
                    if(!ready)throw std::runtime_error("AirPlay audio arrived before receiver readiness");
                    if(!selected.empty() && selected!=header.device_id)continue; // One sender per compressor.
                    if(!prepared || rate!=header.sample_rate){
                        if(prepared){lifecycle_.stop_output();prepared=false;}
                        lifecycle_.prepare_output(config,header.sample_rate);prepared=true;rate=header.sample_rate;
                        const auto volume=remote_volumes.find(header.device_id);
                        pipe_.remote_gain=volume==remote_volumes.end()?1.f:volume->second;
                    }
                    selected=header.device_id;
                    pipe_.paused=false;pipe_.enqueue(payload);lifecycle_.check_output();
                    {std::scoped_lock lock(status_mutex_);++status_.audio_packets;status_.sample_rate=rate;status_.bits=16;status_.channels=2;}
                    state(AB_STREAMING,L"AirPlay 音频 → ZLCompressor → 耳机");
                }else if(header.type==MessageType::Disconnected && selected==header.device_id){
                    if(prepared){lifecycle_.stop_output();prepared=false;}
                    pipe_.reset();selected.clear();rate=0;
                    remote_volumes.erase(header.device_id);
                    {std::scoped_lock lock(status_mutex_);status_.sample_rate=status_.bits=status_.channels=0;}
                    state(AB_READY,L"AirPlay 已断开，等待重新连接 "+name);
                }else if(header.type==MessageType::MediaVolume && header.device_id[0] &&
                         (header.reserved&iPhoneMirror::wireless::MediaVolumeTargetMask)==static_cast<uint32_t>(iPhoneMirror::wireless::MediaVolumeTarget::MirroredStream) &&
                         std::isfinite(header.media_volume) && header.media_volume>=0 && header.media_volume<=1){
                    const auto gain=(header.reserved&iPhoneMirror::wireless::MediaVolumeMuted)?0.f:static_cast<float>(header.media_volume);
                    if(remote_volumes.size()<8 || remote_volumes.contains(header.device_id))remote_volumes[header.device_id]=gain;
                    if(selected==header.device_id)pipe_.remote_gain=gain;
                }else if(header.type==MessageType::Log){
                    iPhoneMirror::logging::write("airplay_host "+std::string(payload.begin(),payload.end()));
                }
            }
        }
        state(AB_STOPPING,L"正在停止 AirPlay");
        // Stop audible output before waiting for the host's network shutdown.
        lifecycle_.stop_output();prepared=false;pipe_.paused=true;
        SetEvent(child.stop.value);DisconnectNamedPipe(pipe.value);
    }catch(const std::exception& e){if(!token.stop_requested())failure=widen(e.what());}
    catch(...){failure=L"未知 AirPlay 接收错误";}
    lifecycle_.stop_output();pipe_.paused=true;
    if(!failure.empty()){iPhoneMirror::logging::write("airplay_source failed: "+narrow(failure));state(AB_ERROR,failure,AB_FAILURE);}
    else state(AB_STOPPED,L"AirPlay 已停止");
    running_=false;
}
}
