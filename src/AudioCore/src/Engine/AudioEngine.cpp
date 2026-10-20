#include "Engine/AudioEngine.h"
#include <cmath>
namespace audiobridge {
AudioEngine::AudioEngine(IBlockProcessor* processor)
    : processor_(processor?*processor:passthrough_),output_(pipe_),source_(pipe_,lifecycle()),airplay_(pipe_,lifecycle()),coredevice_(pipe_,lifecycle()) {}
SourceLifecycle AudioEngine::lifecycle(){
    return {
        [this](const AB_Config& c,uint32_t rate){
            std::scoped_lock lock(output_control_);
            output_.stop();processor_.prepare(rate,4096);
            output_.start(c.output_id,c.startup_frames,c.high_water_frames,volume_,&processor_,rate);
        },
        [this]{if(output_.failed()) throw std::runtime_error(output_.error());},
        [this]{std::scoped_lock lock(output_control_);output_.stop();processor_.release();}
    };
}
AudioEngine::~AudioEngine(){stop();}
void AudioEngine::selectSource(Source source,float value){
    const auto current=status().state;
    if(current==AB_CONNECTING || current==AB_READY || current==AB_STREAMING || current==AB_STOPPING)
        throw std::runtime_error("Stop the current audio source before switching inputs");
    source_.stop();airplay_.stop();coredevice_.stop();volume(value);selected_source_=source;
}
void AudioEngine::start(const AB_Config& config){
    std::scoped_lock lock(source_control_);selectSource(Source::Usb,config.volume);source_.start(config);
}
void AudioEngine::startAirPlay(const AB_Config& config,const std::filesystem::path& host,std::wstring name){
    std::scoped_lock lock(source_control_);selectSource(Source::AirPlay,config.volume);airplay_.start(config,host,std::move(name));
}
void AudioEngine::startCoreDevice(const AB_Config& config,const std::filesystem::path& host){
    std::scoped_lock lock(source_control_);selectSource(Source::CoreDevice,config.volume);coredevice_.start(config,host);
}
void AudioEngine::stop(){std::scoped_lock lock(source_control_);source_.stop();airplay_.stop();coredevice_.stop();}
AB_Status AudioEngine::status() const {
    const auto selected=selected_source_.load();
    auto s=selected==Source::CoreDevice?coredevice_.status():(selected==Source::AirPlay?airplay_.status():source_.status());
    output_.snapshot(s);return s;
}
void AudioEngine::volume(float value){
    if(!std::isfinite(value)||value<0||value>1) throw std::invalid_argument("Volume must be in [0,1]");
    std::scoped_lock lock(output_control_);volume_=value;output_.volume(value);
}
}
