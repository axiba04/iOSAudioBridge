#pragma once
#include "Capture/UsbAudioSource.h"
#include "Wasapi/WasapiOutput.h"
#include "Processing/IBlockProcessor.h"
namespace audiobridge {
class AudioEngine {
public:
    explicit AudioEngine(IBlockProcessor* processor=nullptr);
    ~AudioEngine();
    void start(const AB_Config& config) { source_.start(config); }
    void stop() { source_.stop(); }
    AB_Status status() const;
    AudioTelemetry telemetry() const noexcept { return output_.telemetry(); }
    void volume(float value);
private:
    PassthroughProcessor passthrough_;
    IBlockProcessor& processor_;
    PcmPipe pipe_;
    WasapiOutput output_;
    UsbAudioSource source_;
};
}
