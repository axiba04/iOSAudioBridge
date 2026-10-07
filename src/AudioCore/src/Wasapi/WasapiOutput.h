#pragma once
#include "AudioCore.h"
#include "Buffer/PcmPipe.h"
#include "Processing/PcmBlockRenderer.h"
#include <Windows.h>
#include <thread>
#include <future>
#include <mutex>

namespace audiobridge {
class WasapiOutput {
public:
    WasapiOutput();
    explicit WasapiOutput(PcmPipe& pipe);
    ~WasapiOutput();
    void start(const std::wstring& endpoint,uint32_t startup,uint32_t high_water,float volume,IBlockProcessor* processor=nullptr,uint32_t sample_rate=48000);
    void stop() noexcept;
    void enqueue(std::span<const uint8_t> pcm) noexcept;
    void source_paused(bool paused) noexcept { pipe_->paused.store(paused,std::memory_order_release); }
    void volume(float value) noexcept { volume_.store(value,std::memory_order_relaxed); }
    void snapshot(AB_Status& status) const noexcept;
    AudioTelemetry telemetry() const noexcept;
    bool failed() const noexcept { return failed_.load(); }
    std::string error() const;
private:
    void run(std::wstring endpoint,std::promise<void> ready) noexcept;
    void create_events();
    std::unique_ptr<PcmPipe> owned_pipe_;
    PcmPipe* pipe_{};
    PassthroughProcessor passthrough_;
    IBlockProcessor* processor_{};
    PcmBlockRenderer block_renderer_;
    HANDLE stop_event_{},render_event_{};
    std::jthread thread_;
    uint32_t startup_{3072},high_water_{4096};
    std::atomic<uint32_t> sample_rate_{48000};
    std::atomic<bool> active_{},failed_{},autoconvert_{},idle_{true};
    std::atomic<float> volume_{1},in_l_{-120},in_r_{-120},out_l_{-120},out_r_{-120},stream_ms_{};
    std::atomic<uint32_t> period_{},endpoint_frames_{},padding_{},min_queue_{8192},max_queue_{};
    std::atomic<uint64_t> rendered_{},underruns_{},missing_{},requested_{},max_render_us_{};
    std::atomic<uint64_t> first_underrun_ms_{},last_underrun_ms_{},clipped_{},nonfinite_{};
    mutable std::mutex error_mutex_;
    std::string error_;
};
}
