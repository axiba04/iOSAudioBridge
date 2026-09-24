#include "Engine/AudioEngine.h"
#include <cmath>
namespace audiobridge {
AudioEngine::AudioEngine(IBlockProcessor* processor)
    : processor_(processor?*processor:passthrough_),output_(pipe_),source_(pipe_,{
        [this](const AB_Config& c){processor_.prepare(48000,4096);output_.start(c.output_id,c.startup_frames,c.high_water_frames,c.volume,&processor_);},
        [this]{if(output_.failed()) throw std::runtime_error(output_.error());},
        [this]{output_.stop();processor_.release();}
    }) {}
AudioEngine::~AudioEngine(){stop();}
AB_Status AudioEngine::status() const {auto s=source_.status();output_.snapshot(s);return s;}
void AudioEngine::volume(float value){
    if(!std::isfinite(value)||value<0||value>1) throw std::invalid_argument("Volume must be in [0,1]");
    output_.volume(value);
}
}
