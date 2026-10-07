#pragma once
#include "AudioCore.h"
#include "Buffer/PcmPipe.h"
#include "Capture/SourceLifecycle.h"
#include <functional>
#include <thread>
#include <mutex>
namespace audiobridge {
class UsbAudioSource {
public:
    UsbAudioSource(PcmPipe& pipe,SourceLifecycle lifecycle);
    ~UsbAudioSource();
    void start(const AB_Config& config);
    void stop();
    AB_Status status() const;
private:
    void run(AB_Config config,std::stop_token token) noexcept;
    void state(uint32_t value,std::wstring_view message,int32_t error=0);
    PcmPipe& pipe_;
    SourceLifecycle lifecycle_;
    mutable std::mutex status_mutex_;
    std::mutex control_mutex_;
    AB_Status status_{};
    std::atomic<bool> running_{};
    std::jthread worker_;
};
}
