#pragma once
#include "Capture/UsbAudioSource.h"
#include "Capture/AirPlayAudioSource.h"
#include "Capture/ScrcpyAudioSource.h"
#include "Wasapi/WasapiOutput.h"
#include "Processing/IBlockProcessor.h"
namespace audiobridge {
class AudioEngine {
public:
    explicit AudioEngine(IBlockProcessor* processor=nullptr);
    ~AudioEngine();
    void start(const AB_Config& config);
    void startAirPlay(const AB_Config& config,const std::filesystem::path& host,std::wstring name=L"AudioBridge");
    void startScrcpy(const AB_Config& config,const std::filesystem::path& runtime,bool wireless=false);
    void stop();
    AB_Status status() const;
    AudioTelemetry telemetry() const noexcept { return output_.telemetry(); }
    void volume(float value);
private:
    SourceLifecycle lifecycle();
    enum class Input {Usb,AirPlay,Scrcpy};
    void selectSource(Input input,float volume);
    std::mutex source_control_;
    PassthroughProcessor passthrough_;
    IBlockProcessor& processor_;
    PcmPipe pipe_;
    WasapiOutput output_;
    UsbAudioSource source_;
    AirPlayAudioSource airplay_;
    ScrcpyAudioSource scrcpy_;
    std::atomic<Input> input_{Input::Usb};
    std::mutex output_control_;
    float volume_{1};
};
}
