#pragma once
#include "Processing/IBlockProcessor.h"
#include "Buffer/SpscPcmRing.h"
#include <array>
#include <cmath>
namespace audiobridge {
class PcmBlockRenderer {
public:
    static constexpr size_t max_frames=4096;
    struct Levels { float in_l{},in_r{},out_l{},out_r{}; uint64_t clipped{},nonfinite{}; };
    void reset() noexcept { gain_=0; }
    bool render(StereoFrame* frames,size_t count,float target,IBlockProcessor& processor,Levels& levels) noexcept {
        for(size_t offset=0;offset<count;offset+=max_frames) {
            const auto n=std::min(max_frames,count-offset);
            for(size_t i=0;i<n;++i) {
                left_[i]=frames[offset+i].left/32768.0f;right_[i]=frames[offset+i].right/32768.0f;
                levels.in_l=std::max(levels.in_l,std::abs(left_[i]));levels.in_r=std::max(levels.in_r,std::abs(right_[i]));
            }
            if(!processor.process(left_.data(),right_.data(),n)) {std::memset(frames,0,count*sizeof(StereoFrame));return false;}
            for(size_t i=0;i<n;++i) {
                gain_+=std::clamp(target-gain_,-1.0f/240,1.0f/240);
                auto convert=[&](float sample,float& peak) {
                    if(!std::isfinite(sample)){++levels.nonfinite;sample=0;}
                    sample*=gain_;peak=std::max(peak,std::abs(sample));
                    if(sample<-1 || sample>32767.0f/32768.0f) ++levels.clipped;
                    return static_cast<int16_t>(std::clamp(sample*32768.0f,-32768.0f,32767.0f));
                };
                frames[offset+i]={convert(left_[i],levels.out_l),convert(right_[i],levels.out_r)};
            }
        }
        return true;
    }
private:
    std::array<float,max_frames> left_{},right_{};
    float gain_{};
};
}
