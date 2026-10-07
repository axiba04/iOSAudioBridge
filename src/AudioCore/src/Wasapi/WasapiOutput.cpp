#include "Wasapi/WasapiOutput.h"
#include "Wasapi/WinAudio.h"
#include "Buffer/ResumeBufferGate.h"
#include <avrt.h>
#include <cmath>
#include <cstring>

namespace audiobridge {
static_assert(std::atomic<float>::is_always_lock_free);
namespace {
float db(float value) noexcept { return value>0.000001f ? 20.0f*std::log10(value) : -120.0f; }
struct MmcssScope {
    DWORD index{}; HANDLE handle=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);
    ~MmcssScope(){ if(handle) AvRevertMmThreadCharacteristics(handle); }
};
}
WasapiOutput::WasapiOutput() : owned_pipe_(std::make_unique<PcmPipe>()),pipe_(owned_pipe_.get()) {create_events();}
WasapiOutput::WasapiOutput(PcmPipe& pipe) : pipe_(&pipe) {create_events();}
void WasapiOutput::create_events() {
    stop_event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);

    render_event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!stop_event_ || !render_event_) {
        if(stop_event_) CloseHandle(stop_event_); if(render_event_) CloseHandle(render_event_);
        throw std::runtime_error("Cannot create audio events");
    }
}
WasapiOutput::~WasapiOutput(){ stop(); CloseHandle(stop_event_); CloseHandle(render_event_); }
void WasapiOutput::start(const std::wstring& endpoint,uint32_t startup,uint32_t high_water,float volume_value,IBlockProcessor* processor,uint32_t sample_rate) {
    if(sample_rate!=44100 && sample_rate!=48000) throw std::invalid_argument("Unsupported PCM sample rate");
    stop(); pipe_->reset(); block_renderer_.reset();processor_=processor?processor:&passthrough_; ResetEvent(stop_event_); ResetEvent(render_event_);
    sample_rate_=sample_rate;
    startup_=startup; high_water_=high_water; volume_=volume_value;
    rendered_=0;underruns_=0;missing_=0;requested_=0;max_render_us_=0;first_underrun_ms_=0;last_underrun_ms_=0;clipped_=0;nonfinite_=0;min_queue_=8192;max_queue_=0;
    period_=0; endpoint_frames_=0; padding_=0; stream_ms_=0;
    in_l_=-120; in_r_=-120; out_l_=-120; out_r_=-120; active_=false; failed_=false; idle_=true; autoconvert_=false;

    {std::scoped_lock lock(error_mutex_); error_.clear();}
    std::promise<void> ready; auto future=ready.get_future();
    thread_=std::jthread([this,endpoint,p=std::move(ready)]() mutable { run(endpoint,std::move(p)); });
    future.get();
}
void WasapiOutput::stop() noexcept { SetEvent(stop_event_); if(thread_.joinable()) thread_.join(); active_=false; }
void WasapiOutput::enqueue(std::span<const uint8_t> pcm) noexcept {pipe_->enqueue(pcm);}
std::string WasapiOutput::error() const { std::scoped_lock lock(error_mutex_); return error_; }
void WasapiOutput::snapshot(AB_Status& s) const noexcept {
    s.output_active=active_; s.engine_period_frames=period_; s.endpoint_frames=endpoint_frames_;
    s.queued_frames=static_cast<uint32_t>(pipe_->ring.available()); s.capacity_frames=static_cast<uint32_t>(pipe_->ring.capacity());
    s.rendered_frames=rendered_; s.dropped_frames=pipe_->dropped; s.underruns=underruns_; s.missing_frames=missing_;
    s.input_left_db=in_l_; s.input_right_db=in_r_; s.output_left_db=out_l_; s.output_right_db=out_r_;
    s.host_latency_ms=(s.queued_frames+padding_.load())*1000.0f/sample_rate_.load();
    s.stream_latency_ms=stream_ms_; s.autoconvert=autoconvert_; s.source_idle=pipe_->paused.load() || idle_.load();
}
AudioTelemetry WasapiOutput::telemetry() const noexcept {
    return {pipe_->received.load(),requested_.load(),pipe_->max_gap_us.load(),max_render_us_.load(),
        first_underrun_ms_.load(),last_underrun_ms_.load(),clipped_.load(),nonfinite_.load(),min_queue_.load(),max_queue_.load()};
}
void WasapiOutput::run(std::wstring endpoint,std::promise<void> ready) noexcept {
    bool announced=false;
    try {
        ComScope com; MmcssScope mmcss;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check_hr(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)),"MMDeviceEnumerator");
        ComPtr<IMMDevice> device;
        if(endpoint.empty()) check_hr(enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,&device),"Default output");
        else check_hr(enumerator->GetDevice(endpoint.c_str(),&device),"Selected output unavailable");
        WAVEFORMATEX format{}; format.wFormatTag=WAVE_FORMAT_PCM; format.nChannels=2; format.nSamplesPerSec=sample_rate_;
        format.wBitsPerSample=16; format.nBlockAlign=4; format.nAvgBytesPerSec=format.nSamplesPerSec*4;
        ComPtr<IAudioClient> client; ComPtr<IAudioClient3> client3;
        HRESULT init=device->Activate(__uuidof(IAudioClient3),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client3.GetAddressOf()));
        if(SUCCEEDED(init)) {
            WAVEFORMATEX* closest{};
            init=client3->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,&format,&closest); CoTaskMemFree(closest);
            if(init==S_OK) {
                UINT32 normal{},base{},minimum{},maximum{};
                init=client3->GetSharedModeEnginePeriod(&format,&normal,&base,&minimum,&maximum);
                if(SUCCEEDED(init)) {
                    init=client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,minimum,&format,nullptr);
                    if(SUCCEEDED(init)) { check_hr(client3.As(&client),"IAudioClient"); period_=minimum; }
                }
            }
        }
        if(!client) {
            client3.Reset();
            check_hr(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())),"Audio client");
            check_hr(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,0,0,&format,nullptr),"Shared PCM16 conversion");
            REFERENCE_TIME normal{},minimum{}; if(SUCCEEDED(client->GetDevicePeriod(&normal,&minimum))) period_=static_cast<uint32_t>(normal*format.nSamplesPerSec/10000000);
            autoconvert_=true;
        }
        check_hr(client->SetEventHandle(render_event_),"Audio event");
        UINT32 buffer_frames{}; check_hr(client->GetBufferSize(&buffer_frames),"Audio buffer");
        if(buffer_frames>=pipe_->ring.capacity()) throw std::runtime_error("Output buffer is too large for low-latency playback");
        endpoint_frames_=buffer_frames;
        REFERENCE_TIME latency{}; if(SUCCEEDED(client->GetStreamLatency(&latency))) stream_ms_=static_cast<float>(latency/10000.0);
        ComPtr<IAudioRenderClient> render; check_hr(client->GetService(IID_PPV_ARGS(&render)),"Render client");
        std::vector<StereoFrame> scratch(buffer_frames);
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        announced=true; ready.set_value();
        const HANDLE startup_events[]{stop_event_,pipe_->data_event()};
        while(WaitForSingleObject(stop_event_,0)!=WAIT_OBJECT_0) {
            const auto packet=std::min<size_t>(pipe_->packet_max.load(),pipe_->ring.capacity());
            const auto target=std::min(pipe_->ring.capacity(),std::max<size_t>(startup_,buffer_frames+packet));
            if(pipe_->ring.available()>=target) break;
            if(WaitForMultipleObjects(2,startup_events,FALSE,500)==WAIT_OBJECT_0) return;
        }
        if(WaitForSingleObject(stop_event_,0)==WAIT_OBJECT_0) return;

        ResumeBufferGate resume_gate;
        const auto fill=[&](bool count_underrun) {
            UINT32 padding{}; check_hr(client->GetCurrentPadding(&padding),"Output padding"); padding_=padding;
            if(padding>=buffer_frames) return;
            const auto packet=std::min<size_t>(pipe_->packet_max.load(),pipe_->ring.capacity());
            const auto water=std::min(pipe_->ring.capacity(),std::max<size_t>(high_water_,buffer_frames+packet));
            const auto trimmed=pipe_->ring.trim_to(water); pipe_->dropped.fetch_add(trimmed,std::memory_order_relaxed);
            if(trimmed) block_renderer_.reset();
            const auto frames=buffer_frames-padding;
            const bool source_paused=pipe_->paused.load(std::memory_order_acquire);
            const auto resume_target=std::min(pipe_->ring.capacity(),std::max<size_t>(startup_,buffer_frames+packet));
            const bool was_waiting=resume_gate.waiting();
            const bool buffering=resume_gate.hold(source_paused,pipe_->ring.available(),resume_target);
            if(was_waiting && !resume_gate.waiting()) block_renderer_.reset();
            LARGE_INTEGER began{};QueryPerformanceCounter(&began);
            auto* bytes=reinterpret_cast<BYTE*>(scratch.data());
            const auto queued=static_cast<uint32_t>(pipe_->ring.available());
            if(queued<min_queue_) min_queue_=queued;if(queued>max_queue_) max_queue_=queued;
            const auto copied=buffering?size_t{0}:pipe_->ring.read(bytes,frames);
            if(copied<frames) std::memset(bytes+copied*4,0,(frames-copied)*4);
            const auto last=pipe_->last_packet_ms.load(std::memory_order_acquire);
            const bool idle=source_paused || buffering || ((!last || GetTickCount64()-last>250) && copied==0);
            idle_=idle;
            if(count_underrun && copied<frames && !idle) {
                ++underruns_;missing_.fetch_add(frames-copied,std::memory_order_relaxed);
                const auto now=GetTickCount64();if(!first_underrun_ms_)first_underrun_ms_=now;last_underrun_ms_=now;
            }
            requested_.fetch_add(frames,std::memory_order_relaxed);
            PcmBlockRenderer::Levels levels;
            const auto processed=block_renderer_.render(scratch.data(),frames,volume_.load(std::memory_order_relaxed)*pipe_->remote_gain.load(std::memory_order_relaxed),*processor_,levels);
            clipped_.fetch_add(levels.clipped,std::memory_order_relaxed);nonfinite_.fetch_add(levels.nonfinite,std::memory_order_relaxed);
            in_l_=db(levels.in_l);in_r_=db(levels.in_r);out_l_=db(levels.out_l);out_r_=db(levels.out_r);
            LARGE_INTEGER ended{};QueryPerformanceCounter(&ended);
            const auto us=static_cast<uint64_t>((ended.QuadPart-began.QuadPart)*1000000/frequency.QuadPart);
            if(us>max_render_us_.load(std::memory_order_relaxed))max_render_us_=us;
            BYTE* output_bytes{};check_hr(render->GetBuffer(frames,&output_bytes),"Get render buffer");
            std::memcpy(output_bytes,scratch.data(),frames*4);
            check_hr(render->ReleaseBuffer(frames,0),"Release render buffer");
            if(!processed) throw std::runtime_error("Audio processor failed; output muted");
            rendered_.fetch_add(copied,std::memory_order_relaxed); padding_=padding+frames;
            if(copied<frames) block_renderer_.reset();
        };
        fill(false); check_hr(client->Start(),"Start output"); active_=true;
        struct StopClient { IAudioClient* client; ~StopClient(){ client->Stop(); } } stop{client.Get()};
        const HANDLE events[]{stop_event_,render_event_};
        while(true) {
            const auto wait=WaitForMultipleObjects(2,events,FALSE,2000);
            if(wait==WAIT_OBJECT_0) break;
            if(wait!=WAIT_OBJECT_0+1) throw std::runtime_error("Output event timed out or device was removed");
            fill(true);
        }
    } catch(const std::exception& e) {
        {std::scoped_lock lock(error_mutex_); error_=e.what();} failed_=true;
        if(!announced) { try {ready.set_exception(std::current_exception());}catch(...){} }
    } catch(...) {
        {std::scoped_lock lock(error_mutex_); error_="Unknown output error";} failed_=true;
        if(!announced) {try {ready.set_exception(std::current_exception());}catch(...){} }
    }
    active_=false;
}
}

