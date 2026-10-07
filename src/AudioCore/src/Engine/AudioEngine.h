#pragma once
#include "Capture/UsbAudioSource.h"
#include "Capture/AirPlayAudioSource.h"
#include "Wasapi/WasapiOutput.h"
#include "Processing/IBlockProcessor.h"
namespace audiobridge {
class AudioEngine {
public:
    explicit AudioEngine(IBlockProcessor* processor=nullptr);
    ~AudioEngine();
    void start(const AB_Config& config);
    void startAirPlay(const AB_Config& config,const std::filesystem::path& host,std::wstring name=L"ZL Audio Bridge");
    void stop();
    AB_Status status() const;
    AudioTelemetry telemetry() const noexcept { return output_.telemetry(); }
    void volume(float value);
private:
    SourceLifecycle lifecycle();
    void selectSource(bool airplay,float volume);
    std::mutex source_control_;
    PassthroughProcessor passthrough_;
    IBlockProcessor& processor_;
    PcmPipe pipe_;
    WasapiOutput output_;
    UsbAudioSource source_;
    AirPlayAudioSource airplay_;
    std::atomic<bool> use_airplay_{};
    std::mutex output_control_;
    float volume_{1};
};
}
