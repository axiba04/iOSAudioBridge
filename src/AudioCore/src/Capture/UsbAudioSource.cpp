#include "Capture/UsbAudioSource.h"
#include "Device/DeviceCatalog.h"
#include "Device/AppleUsbDiscovery.h"
#include "Transport/LibUsb0Transport.h"
#include "Transport/UsbMuxClient.h"
#include "Protocol/QuickTimeSession.h"
#include "Pcm/PcmFormat.h"
#include "Capture/UsbConfigurationRestorePolicy.h"
#include "Capture/AudioStopDrainPolicy.h"
#include "Logging.h"
#include <chrono>
#include <format>
#include <cmath>
#include <functional>
#include <cctype>

namespace audiobridge {
namespace {
namespace transport=iPhoneMirror::transport;
namespace device=iPhoneMirror::device;
namespace logging=iPhoneMirror::logging;
namespace detail=iPhoneMirror::capture::detail;
using Clock=std::chrono::steady_clock;
bool conservative_stop_mode() noexcept {
    wchar_t mode[32]{};
    const auto length=GetEnvironmentVariableW(L"AB_USB_STOP_MODE",mode,32);
    // Missing or explicitly fast uses the tested fast path. Unknown values fail conservative.
    return length!=0 && (length>=32 || std::wstring_view(mode)!=L"fast");
}
#include "Session/UsbRestore.inl"
class DeviceLease {
public:
    explicit DeviceLease(std::string serial) {
        serial.erase(std::remove(serial.begin(),serial.end(),'-'),serial.end());
        std::transform(serial.begin(),serial.end(),serial.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        const auto name=L"Local\\iOSAudioBridge.USB."+widen(serial);
        handle_=CreateMutexW(nullptr,FALSE,name.c_str());
        if(!handle_) throw std::runtime_error("Cannot create device lease");
        const auto wait=WaitForSingleObject(handle_,0);
        if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED) { CloseHandle(handle_); handle_=nullptr; throw std::runtime_error("This iPhone is already used by another AudioBridge session"); }
    }
    ~DeviceLease(){ if(handle_){ReleaseMutex(handle_);CloseHandle(handle_);} }
private: HANDLE handle_{};
};
}
UsbAudioSource::UsbAudioSource(PcmPipe& pipe,SourceLifecycle lifecycle) : pipe_(pipe),lifecycle_(std::move(lifecycle)) { status_.struct_size=sizeof(status_);status_.version=AB_VERSION;status_.state=AB_STOPPED;copy_text(status_.message,L"未连接"); }
UsbAudioSource::~UsbAudioSource(){ stop(); }
void UsbAudioSource::state(uint32_t value,std::wstring_view message,int32_t error) {
    std::scoped_lock lock(status_mutex_); status_.state=value;status_.error_code=error;copy_text(status_.message,message);
}
void UsbAudioSource::start(const AB_Config& source) {
    std::scoped_lock lock(control_mutex_);
    if(running_) throw std::runtime_error("Session is already running or restoring USB");
    if(worker_.joinable()) worker_.join();
    AB_Config config=source;
    if(!config.startup_frames) config.startup_frames=3072;
    if(!config.high_water_frames) config.high_water_frames=4096;
    if(!config.device_id[0] || config.startup_frames<128 || config.high_water_frames<config.startup_frames || config.high_water_frames>7168 ||
       !std::isfinite(config.volume) || config.volume<0 || config.volume>1) throw std::invalid_argument("Invalid device, buffering or volume configuration");
    { std::scoped_lock status_lock(status_mutex_); status_={};status_.struct_size=sizeof(status_);status_.version=AB_VERSION;status_.state=AB_CONNECTING;copy_text(status_.message,L"正在初始化音频输出与 USB"); }
    running_=true;
    try { worker_=std::jthread([this,config](std::stop_token token){run(config,token);}); }
    catch(...) {running_=false;state(AB_ERROR,L"无法创建接收线程",AB_FAILURE);throw;}
}
void UsbAudioSource::stop() {
    std::scoped_lock lock(control_mutex_);
    if(worker_.joinable()) { worker_.request_stop();worker_.join(); }
}
AB_Status UsbAudioSource::status() const {
    AB_Status result; {std::scoped_lock lock(status_mutex_);result=status_;}
    return result;
}

void UsbAudioSource::run(AB_Config config,std::stop_token token) noexcept {
    using namespace iPhoneMirror;
    transport::LibUsb0Connection usb;
    std::optional<transport::AppleUsbIdentity> identity;
    std::unique_ptr<DeviceLease> lease;
    bool opened=false,activated=false,restore_authorized=false,peer_stopped=false;
    quicktime::SessionProtocol protocol;
    quicktime::StreamDecoder decoder;
    std::vector<uint8_t> read_buffer;
    std::wstring failure;
    Clock::time_point peer_pause_started{};
    try {
        const auto serial=narrow(config.device_id);
        lease=std::make_unique<DeviceLease>(serial);
        if(!device::is_apple_usb_parent_present(serial)) throw std::runtime_error("Selected iPhone is not connected by USB");
        const auto filter=device::inspect_apple_usb_filter_stack(serial);
        if(filter.safety==device::AppleUsbFilterSafety::Indeterminate) throw std::runtime_error("Cannot verify Apple USB driver state: "+filter.diagnostic);
        if(!transport::libusb0_available()) throw std::runtime_error("libusb0 runtime/filter is not available; use the existing iPhoneMirror driver setup");
        lifecycle_.prepare_output(config);
        if(token.stop_requested()) throw std::runtime_error("Connection cancelled");
        read_buffer.resize(1024*1024);
        const auto selected=transport::find_libusb0_device(serial);
        if(!selected || !selected->can_open || !selected->active_configuration_known)
            throw std::runtime_error("Cannot open selected iPhone or read its USB configuration");
        identity=transport::make_apple_usb_identity(*selected);
        const bool adopted=transport::is_libusb0_quicktime_configuration_active(*selected);
        restore_authorized=adopted;
        if(!adopted) {
            state(AB_CONNECTING,L"等待 iPhone 切换至 USB 音频配置");
            activated=transport::LibUsb0Connection::enable_quicktime_configuration(*identity);
            if(!activated) throw std::runtime_error("iPhone rejected USB capture activation");
        }
        // Complete a safe claim even if Stop arrived during activation, so the tested restore path can run.
        usb=transport::LibUsb0Connection::open_quicktime(*identity,{
            .allow_conventional_fallback=false,.allow_configuration_initialization=activated||adopted,
            .wait_for_activated_descriptor=activated,.stop_token={}});
        opened=true;restore_authorized=true;
        if(!usb.active_topology_id().empty()) identity->topology_id=usb.active_topology_id();
        state(AB_CONNECTING,L"正在协商 USB 仅音频");
        const auto started=Clock::now();auto presence_at=started;
        while(!token.stop_requested()) {
            lifecycle_.check_output();
            const auto now=Clock::now();
            if(now-presence_at>std::chrono::seconds(2)) {
                presence_at=now;
                if(!device::is_apple_usb_parent_present(serial)) throw std::runtime_error("iPhone USB disconnected");
            }
            if(!peer_stopped && !protocol.negotiated_audio() && !protocol.audio_packets() && now-started>std::chrono::seconds(20))
                throw std::runtime_error("USB audio negotiation timed out");
            const auto count=usb.read(read_buffer,250);
            if(!count) continue;
            for(const auto& packet:decoder.push(std::span(read_buffer).first(count))) {
                const auto event=protocol.process(packet);
                for(const auto& message:event.outbound) usb.write(message,1000);
                {std::scoped_lock lock(status_mutex_); status_.audio_packets=protocol.audio_packets();status_.video_packets=protocol.video_frames();status_.video_bytes=protocol.video_bytes();}
                if(packet.is_video_sample()) throw std::runtime_error("Unexpected video in an audio-only session; capture stopped");
                if(event.state==quicktime::SessionState::Error) throw std::runtime_error(event.warning);
                if(event.state==quicktime::SessionState::Paused) {
                    if(!peer_stopped) {
                        peer_pause_started=Clock::now();
                        logging::write(std::format("audio_source_paused reason=peer_stop audio_packets={} usb_retained=true",protocol.audio_packets()));
                    }
                    peer_stopped=true;
                    pipe_.paused.store(true,std::memory_order_release);
                    state(AB_READY,L"手机音频已暂停，保持连接等待恢复");
                }
                if(protocol.negotiated_audio()) {
                    validate_format(*protocol.negotiated_audio());
                    std::scoped_lock lock(status_mutex_);
                    status_.sample_rate=48000;status_.channels=2;status_.bits=16;
                    if(status_.state==AB_CONNECTING){status_.state=AB_READY;copy_text(status_.message,L"USB 音频已连接，请在手机上播放声音");}
                }
                if(event.audio_sample) {
                    const auto& sample=*event.audio_sample;
                    const auto* format=sample.format && sample.format->audio ? &*sample.format->audio :
                        protocol.audio_format() && protocol.audio_format()->audio ? &*protocol.audio_format()->audio :
                        protocol.negotiated_audio() ? &*protocol.negotiated_audio() : nullptr;
                    if(!format) throw std::runtime_error("PCM arrived before a valid audio format");
                    validate_format(*format);validate_sample(sample);
                    pipe_.enqueue(sample.sample_data);
                    if(peer_stopped && !sample.sample_data.empty()) {
                        logging::write(std::format("audio_source_resumed pause_ms={} audio_packets={} usb_retained=true",
                            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-peer_pause_started).count(),protocol.audio_packets()));
                        peer_stopped=false;
                        pipe_.paused.store(false,std::memory_order_release);
                    }
                    std::scoped_lock lock(status_mutex_);
                    status_.sample_rate=48000;status_.channels=2;status_.bits=16;status_.state=AB_STREAMING;copy_text(status_.message,L"USB 仅音频播放中");
                }
            }
        }
    } catch(const std::exception& e) { failure=widen(e.what());logging::write(std::format("audio_session_error stop_requested={} error={}",token.stop_requested(),e.what())); }
    catch(...) {failure=L"未知音频会话错误";}
    const bool conservative=conservative_stop_mode();
    const auto cleanup_started=Clock::now();
    const auto elapsed_ms=[&](auto since){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-since).count();};
    state(AB_STOPPING,L"正在结束手机音频会话…");
    lifecycle_.stop_output();
    const auto output_stop_ms=elapsed_ms(cleanup_started);
    const auto drain_started=Clock::now();
    // Cleanup runs for success, cancellation and every capture error, without using a cancelled token.
    if(opened) {
        try {
            const auto stop=protocol.stop_messages();
            for(const auto& message:stop) usb.write(message,500);
            size_t releases=0;
            AudioStopDrainPolicy drain(Clock::now(),peer_stopped,conservative);
            while(!drain.finished(Clock::now(),releases>=stop.size(),decoder.buffered_bytes()==0 && protocol.state()==quicktime::SessionState::Stopped)) {
                const auto count=usb.read(read_buffer,conservative?250:50);if(!count) continue;
                for(const auto& packet:decoder.push(std::span(read_buffer).first(count))) {
                    const auto event=protocol.process(packet);
                    for(const auto& message:event.outbound) usb.write(message,500);
                    if(packet.is_media_sample()) drain.media(Clock::now());
                    if(packet.kind==quicktime::PacketKind::Sync && packet.subtype==quicktime::fourcc('s','t','o','p') && event.state==quicktime::SessionState::Stopped)
                        drain.peer_stop(Clock::now());
                    if(packet.kind==quicktime::PacketKind::Async && packet.subtype==quicktime::fourcc('r','e','l','s')) ++releases;
                }
            }
            if(releases>=stop.size()) for(const auto& message:protocol.complete_stop_messages()) usb.write(message,500);
            logging::write(std::format("audio_stop_drain mode={} reason={} elapsed_ms={} releases={}/{} buffered_bytes={}",
                conservative?"conservative":"fast",drain.reason(Clock::now(),releases>=stop.size()),elapsed_ms(drain_started),releases,stop.size(),decoder.buffered_bytes()));
            logging::write(std::format("audio_only stop releases={}/{} audio_packets={} video_packets={} video_bytes={}",releases,stop.size(),protocol.audio_packets(),protocol.video_frames(),protocol.video_bytes()));
        } catch(const std::exception& e) {logging::write(e.what());} catch(...){}
    }
    const auto drain_ms=elapsed_ms(drain_started);
    // Never send a disconnecting control through the claimed stream handle.
    const auto close_started=Clock::now();
    usb.close();
    const auto close_ms=elapsed_ms(close_started);
    const auto restore_started=Clock::now();
    if(identity && restore_authorized && device::is_apple_usb_parent_present(identity->serial)) {
        state(AB_STOPPING,L"正在恢复手机 USB 连接…");
        const auto restored=restore_libusb0_configuration(*identity,false,std::chrono::milliseconds(conservative?3000:1000));
        if(!restored.normal_observed) failure+=L" USB 普通配置未确认恢复，请重新插拔数据线。";
    } else if(activated && !restore_authorized) failure+=L" USB 激活后未取得有效流句柄，请重新插拔数据线。";
    logging::write(std::format("audio_stop_timing mode={} output_ms={} drain_ms={} close_ms={} restore_ms={} total_ms={} failure={}",
        conservative?"conservative":"fast",output_stop_ms,drain_ms,close_ms,elapsed_ms(restore_started),elapsed_ms(cleanup_started),!failure.empty()));
    {std::scoped_lock lock(status_mutex_);status_.audio_packets=protocol.audio_packets();status_.video_packets=protocol.video_frames();status_.video_bytes=protocol.video_bytes();}
    if(protocol.video_frames() && failure.empty()) failure=L"停止阶段收到非请求的视频数据";
    lease.reset();
    if(failure.empty() || (token.stop_requested() && failure==L"Connection cancelled")) state(AB_STOPPED,L"已停止，USB 已释放");
    else state(AB_ERROR,failure,AB_FAILURE);
    running_=false;
}
}

