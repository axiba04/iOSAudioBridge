#pragma once
#include "Media/CoreMedia.h"
#include "Protocol/QuickTimePacket.h"
#include <stdexcept>

namespace audiobridge {
inline void validate_format(const iPhoneMirror::coremedia::AudioStreamBasicDescription& f) {
    if(f.format_id!=iPhoneMirror::quicktime::fourcc('l','p','c','m') || f.sample_rate!=48000.0 ||
       f.channels_per_frame!=2 || f.bits_per_channel!=16 || f.bytes_per_frame!=4 ||
       f.frames_per_packet!=1 || f.bytes_per_packet!=4 || (f.format_flags&0x23)!=0 || (f.format_flags&0x0c)!=0x0c)
        throw std::runtime_error("Unsupported source format: require 48000 Hz, signed packed PCM16 little-endian stereo");
}
inline void validate_sample(const iPhoneMirror::coremedia::SampleBuffer& s) {
    if(s.sample_data.size()%4 || (s.sample_count && uint64_t(s.sample_count)*4!=s.sample_data.size()))
        throw std::runtime_error("PCM sample count/block alignment mismatch");
    for(auto size:s.sample_sizes) if(size!=4) throw std::runtime_error("Invalid PCM sample size");
}
}
