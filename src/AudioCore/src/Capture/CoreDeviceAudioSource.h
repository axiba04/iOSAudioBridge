#pragma once
#include "Buffer/PcmPipe.h"
#include "Capture/SourceLifecycle.h"
#include <filesystem>
#include <mutex>
#include <thread>

namespace audiobridge {
class CoreDeviceAudioSource {
public:
    CoreDeviceAudioSource(PcmPipe& pipe,SourceLifecycle lifecycle);
    ~CoreDeviceAudioSource();
    void start(const AB_Config& config,const std::filesystem::path& host);
    void stop();
    AB_Status status() const;
private:
    void run(AB_Config config,std::filesystem::path host,std::stop_token token) noexcept;
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
