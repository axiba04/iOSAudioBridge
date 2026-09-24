#include "ZlProcessorAdapter.h"
#include <stdexcept>
void ZlProcessorAdapter::set(PluginProcessor& processor,const char* id,float value) {
    auto* p=processor.parameters_.getParameter(id);
    if(!p)throw std::runtime_error("Missing ZL parameter");
    p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(value));p->endChangeGesture();
}
void ZlProcessorAdapter::gamePreset(PluginProcessor& p) {
    const std::pair<const char*,float> settings[]{
        {"comp_on",1},{"comp_direction",0},{"comp_style",0},{"threshold",-18},{"ratio",4},{"attack",2},{"release",100},{"knee_width",6},{"makeup",0},
        {"oversample",0},{"lookahead",0},{"rms_on",0},{"external_side",0},{"side_stereo_mode",3},{"side_stereo_link",100},
        {"side_stereo_wet1",100},{"side_stereo_wet2",100},{"side_stereo_swap",0},{"side_out",0},{"comp_delta",0},{"wet",100},{"clipper_drive",0}};
    for(const auto& [id,value]:settings)set(p,id,value);
}
void ZlProcessorAdapter::prepare(double sample_rate,size_t max_frames) {
    if(sample_rate!=48000 || max_frames>4096)throw std::runtime_error("ZL bridge requires 48 kHz and at most 4096 frames");
    auto layout=processor_.getBusesLayout();
    layout.inputBuses.set(0,juce::AudioChannelSet::stereo());layout.inputBuses.set(1,juce::AudioChannelSet::disabled());
    layout.outputBuses.set(0,juce::AudioChannelSet::stereo());
    if(!processor_.setBusesLayout(layout))throw std::runtime_error("ZL rejected stereo main / disabled sidechain layout");
    processor_.setProcessingPrecision(juce::AudioProcessor::singlePrecision);processor_.setNonRealtime(false);
    processor_.setRateAndBufferSizeDetails(sample_rate,4096);processor_.prepareToPlay(sample_rate,4096);
    left_.fill(0);right_.fill(0);view_.setDataToReferTo(pointers_,2,4096);
    processor_.processBlock(view_,midi_); // warm deferred parameter updates off the render thread
}
bool ZlProcessorAdapter::process(float* left,float* right,size_t frames) noexcept {
    if(frames>4096)return false;
    if(!frames)return true;
    try {
        float* pointers[]{left,right};view_.setDataToReferTo(pointers,2,static_cast<int>(frames));
        processor_.processBlock(view_,midi_);return true;
    }catch(...){return false;}
}
