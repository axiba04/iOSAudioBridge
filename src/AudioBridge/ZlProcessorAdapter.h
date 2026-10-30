#pragma once
#include "PluginProcessor.hpp"
#include "Processing/IBlockProcessor.h"
#include <array>

class ZlProcessorAdapter final : public audiobridge::IBlockProcessor {
public:
    explicit ZlProcessorAdapter(PluginProcessor& processor) : processor_(processor) {}
    void prepare(double sample_rate,size_t max_frames) override;
    bool process(float* left,float* right,size_t frames) noexcept override;
    void release() noexcept override {}
    static void gamePreset(PluginProcessor& processor);
    static void set(PluginProcessor& processor,const char* id,float value);
private:
    PluginProcessor& processor_;
    std::array<float,4096> left_{},right_{};
    float* pointers_[2]{left_.data(),right_.data()};
    juce::AudioBuffer<float> view_{pointers_,2,4096};
    juce::MidiBuffer midi_;
};
