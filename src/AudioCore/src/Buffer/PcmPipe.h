#pragma once
#include "Buffer/SpscPcmRing.h"
#include <Windows.h>
namespace audiobridge {
struct AudioTelemetry {
    uint64_t received_frames{},requested_frames{},max_packet_gap_us{},max_render_us{};
    uint64_t first_underrun_ms{},last_underrun_ms{},clipped_samples{},nonfinite_samples{};
    uint32_t min_queue{},max_queue{};
};
class PcmPipe {
public:
    PcmPipe() : data_event_(CreateEventW(nullptr,FALSE,FALSE,nullptr)) {
        if(!data_event_) throw std::runtime_error("Cannot create PCM event");
        LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency_=f.QuadPart;
    }
    ~PcmPipe(){CloseHandle(data_event_);}
    PcmPipe(const PcmPipe&)=delete;
    PcmPipe& operator=(const PcmPipe&)=delete;
    // Reset only after both queue owners have stopped.
    void reset() noexcept {
        ring.reset();paused=false;remote_gain=1;packet_max=0;last_packet_ms=0;received=0;dropped=0;max_gap_us=0;
        last_tick_=0;ResetEvent(data_event_);++generation;
    }
    void enqueue(std::span<const uint8_t> pcm) noexcept {
        if(pcm.empty()) return;
        const auto frames=pcm.size()/4;
        if(frames>packet_max.load(std::memory_order_relaxed)) packet_max=static_cast<uint32_t>(frames);
        received.fetch_add(frames,std::memory_order_relaxed);dropped.fetch_add(frames-ring.write(pcm),std::memory_order_relaxed);
        LARGE_INTEGER now{};QueryPerformanceCounter(&now);
        if(last_tick_) {
            const auto gap=static_cast<uint64_t>((now.QuadPart-last_tick_)*1000000/frequency_);
            if(gap>max_gap_us.load(std::memory_order_relaxed)) max_gap_us=gap;
        }
        last_tick_=now.QuadPart;last_packet_ms.store(GetTickCount64(),std::memory_order_release);SetEvent(data_event_);
    }
    HANDLE data_event() const noexcept { return data_event_; }
    SpscPcmRing ring;
    std::atomic<bool> paused{};
    std::atomic<float> remote_gain{1};
    std::atomic<uint32_t> packet_max{};
    std::atomic<uint64_t> last_packet_ms{},received{},dropped{},max_gap_us{},generation{};
private:
    HANDLE data_event_{};
    int64_t frequency_{},last_tick_{};
};
}
