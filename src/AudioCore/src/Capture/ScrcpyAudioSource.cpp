#include "Transport/Socket.h"
#include "Capture/ScrcpyAudioSource.h"
#include "Capture/ScrcpyProtocol.h"
#include "Android/AdbClient.h"
#include "Device/DeviceCatalog.h"
#include "Logging.h"
#include <bcrypt.h>
#include <cmath>
#include <format>
#include <sstream>

namespace audiobridge {
namespace {
using iPhoneMirror::transport::Socket;
// Session-scoped forwarding and remote file: never remove other adb clients' tunnels.
struct Session {
    android::AdbClient adb;
    std::wstring serial,remote,port,socket_name;
    std::unique_ptr<android::AdbProcess> server;
    Socket socket;
    Session(std::filesystem::path executable,std::wstring id):adb(std::move(executable)),serial(std::move(id)){}
    ~Session(){
        socket.close(); // Server observes EOF/broken pipe and releases AudioRecord.
        if(server){
            const auto deadline=GetTickCount64()+500;
            while(!server->exited()&&GetTickCount64()<deadline)Sleep(20);
            server.reset();
        }
        try{
            // Cancellation can race with successful forward allocation before its
            // stdout is read. Recover only this session's uniquely named mapping.
            if(port.empty()&&!socket_name.empty()){
                std::istringstream mappings(adb.run({L"forward",L"--list"},{},1500));
                std::string device,local,target;
                while(mappings>>device>>local>>target){
                    if(widen(device)==serial&&widen(target)==socket_name&&local.starts_with("tcp:")){
                        port=widen(local.substr(4));break;
                    }
                }
            }
            if(!port.empty())adb.run({L"-s",serial,L"forward",L"--remove",L"tcp:"+port},{},1500);
        }catch(...){}
        try{if(!remote.empty())adb.run({L"-s",serial,L"shell",L"rm -f "+remote},{},1500);}catch(...){}
    }
};
std::wstring port_number(const std::string& response){
    const auto start=response.find_first_not_of(" \r\n\t"),end=response.find_last_not_of(" \r\n\t");
    if(start==std::string::npos)throw std::runtime_error("ADB did not allocate a forward port");
    const auto port=response.substr(start,end-start+1);
    if(port.size()>5||port.find_first_not_of("0123456789")!=std::string::npos||std::stoul(port)==0||std::stoul(port)>65535)
        throw std::runtime_error("Invalid adb forward port");
    return widen(port);
}
}
ScrcpyAudioSource::ScrcpyAudioSource(PcmPipe& pipe,SourceLifecycle lifecycle):pipe_(pipe),lifecycle_(std::move(lifecycle)){
    status_.struct_size=sizeof(status_);status_.version=AB_VERSION;
}
ScrcpyAudioSource::~ScrcpyAudioSource(){stop();}
void ScrcpyAudioSource::state(uint32_t value,std::wstring_view message,int32_t error){
    std::scoped_lock lock(status_mutex_);status_.state=value;status_.error_code=error;copy_text(status_.message,message);
}
AB_Status ScrcpyAudioSource::status() const {std::scoped_lock lock(status_mutex_);return status_;}
void ScrcpyAudioSource::start(const AB_Config& source,const std::filesystem::path& runtime,bool wireless){
    std::scoped_lock lock(control_mutex_);
    if(running_)throw std::runtime_error("scrcpy is already running");
    if(worker_.joinable())worker_.join();
    AB_Config config=source;
    if(!config.startup_frames)config.startup_frames=wireless?3072:1024;
    if(!config.high_water_frames)config.high_water_frames=wireless?6144:3072;
    if(!config.device_id[0]||std::find(std::begin(config.device_id),std::end(config.device_id),L'\0')==std::end(config.device_id)||
       config.startup_frames<128||config.high_water_frames<config.startup_frames||config.high_water_frames>7168||
       !std::isfinite(config.volume)||config.volume<0||config.volume>1)
        throw std::invalid_argument("Invalid scrcpy device or audio configuration");
    if(!std::filesystem::is_regular_file(runtime/L"adb.exe")||!std::filesystem::is_regular_file(runtime/L"scrcpy-server"))
        throw std::invalid_argument("缺少 Android/adb.exe 或 scrcpy-server，请完整解压 AudioBridge");
    {std::scoped_lock status_lock(status_mutex_);status_={};status_.struct_size=sizeof(status_);status_.version=AB_VERSION;}
    state(AB_CONNECTING,L"正在连接 Android scrcpy 音频");running_=true;
    try{worker_=std::jthread([this,config,runtime](std::stop_token token){run(config,runtime,token);});}
    catch(...){running_=false;state(AB_ERROR,L"无法启动 Android 音频线程",AB_FAILURE);throw;}
}
void ScrcpyAudioSource::stop(){
    std::scoped_lock lock(control_mutex_);
    if(worker_.joinable()){worker_.request_stop();worker_.join();}
}
void ScrcpyAudioSource::run(AB_Config config,std::filesystem::path runtime,std::stop_token token) noexcept {
    std::wstring failure;bool prepared=false;
    try{
        Session session(runtime/L"adb.exe",config.device_id);
        // Stop output before session teardown, including on exceptions.
        struct OutputGuard {SourceLifecycle& lifecycle;PcmPipe& pipe;~OutputGuard(){lifecycle.stop_output();pipe.paused=true;}} guard{lifecycle_,pipe_};
        uint32_t nonce{};
        if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&nonce),sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
            throw std::runtime_error("Cannot allocate scrcpy session id");
        const auto id=std::format(L"{:08x}",nonce&0x7fffffff);
        session.adb.run({L"-s",session.serial,L"get-state"},token);
        const auto sdk=session.adb.run({L"-s",session.serial,L"shell",L"getprop ro.build.version.sdk"},token);
        if(std::stoi(sdk)<30)throw std::runtime_error("scrcpy 音频需要 Android 11 或更新版本");
        session.remote=L"/data/local/tmp/audiobridge-"+id+L".jar";
        session.adb.run({L"-s",session.serial,L"push",(runtime/L"scrcpy-server").wstring(),session.remote},token,30000);
        session.socket_name=L"localabstract:scrcpy_"+id;
        session.port=port_number(session.adb.run({L"-s",session.serial,L"forward",L"tcp:0",session.socket_name},token));
        const auto command=L"CLASSPATH="+session.remote+L" app_process / com.genymobile.scrcpy.Server "+widen(AB_SCRCPY_VERSION)+
            L" scid="+id+L" log_level=warn video=false audio=true control=false audio_codec=raw audio_source=output"
            L" tunnel_forward=true send_device_meta=false send_stream_meta=true send_frame_meta=true send_dummy_byte=true cleanup=false power_on=false";
        session.server=std::make_unique<android::AdbProcess>(session.adb.executable(),std::vector<std::wstring>{L"-s",session.serial,L"shell",command});
        pipe_.reset();
        scrcpy::AudioParser parser([&]{
            lifecycle_.prepare_output(config,48000);prepared=true;
            {std::scoped_lock lock(status_mutex_);status_.sample_rate=48000;status_.channels=2;status_.bits=16;}
            state(AB_READY,L"Android 音频已连接，等待播放");
        },[&](std::span<const uint8_t> pcm){
            pipe_.paused=false;pipe_.enqueue(pcm);lifecycle_.check_output();
            {std::scoped_lock lock(status_mutex_);++status_.audio_packets;}
            state(AB_STREAMING,L"Android scrcpy → ZLCompressor → 耳机");
        });
        const auto deadline=GetTickCount64()+15000;
        // ADB forward accepts TCP before the Android socket exists: retry until
        // its dummy byte arrives. After that, a disconnect is a real failure.
        bool connected=false;
        while(!token.stop_requested()&&!connected){
            if(session.server->exited())throw std::runtime_error("scrcpy-server exited: "+session.server->output());
            if(GetTickCount64()>=deadline)throw std::runtime_error("scrcpy 启动超时，请检查手机授权、解锁状态及网络");
            try{
                auto socket=Socket::connect_loopback(static_cast<uint16_t>(std::stoul(session.port)),100);
                std::array<uint8_t,1> dummy{};
                if(socket.receive(dummy)==1){parser.feed(dummy);session.socket=std::move(socket);connected=true;}
            }catch(const iPhoneMirror::transport::SocketError&){}
            if(!connected)Sleep(20);
        }
        std::array<uint8_t,8192> bytes{};
        while(!token.stop_requested()&&connected){
            if(prepared){
                lifecycle_.check_output();const auto last=pipe_.last_packet_ms.load();
                if(last&&GetTickCount64()-last>500)pipe_.paused=true;
            }else if(GetTickCount64()>=deadline)throw std::runtime_error("scrcpy 未返回音频格式，请解锁 Android 11 设备后重试");
            try{
                const auto count=session.socket.receive(bytes);
                if(!count)throw std::runtime_error("Android 音频连接已断开，请检查 USB / Wi-Fi 后重新开始。"+session.server->output());
                parser.feed(std::span(bytes).first(count));
            }catch(const iPhoneMirror::transport::SocketError& e){
                if(e.code()!=WSAETIMEDOUT&&e.code()!=WSAEWOULDBLOCK)throw;
            }
            if(session.server->exited())throw std::runtime_error("scrcpy-server exited: "+session.server->output());
        }
        state(AB_STOPPING,L"正在停止 Android 音频");
    }catch(const std::exception& e){if(!token.stop_requested())failure=widen(e.what());}
    catch(...){failure=L"未知 scrcpy 音频错误";}
    if(!failure.empty()){
        iPhoneMirror::logging::write("scrcpy_source failed: "+narrow(failure));state(AB_ERROR,failure,AB_FAILURE);
    }else state(AB_STOPPED,L"Android 音频已停止");
    running_=false;
}
}
