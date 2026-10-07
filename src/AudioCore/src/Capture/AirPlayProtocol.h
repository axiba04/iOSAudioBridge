#pragma once
#include "IpcProtocol.h"
#include <cstring>
#include <stdexcept>

namespace audiobridge::airplay {
using iPhoneMirror::wireless::MessageHeader;
using iPhoneMirror::wireless::MessageType;
// Audio-only bridge: bound allocations well below the upstream video limit.
inline void validate_header(const MessageHeader& h) {
    if(h.magic!=iPhoneMirror::wireless::IpcMagic || h.version!=iPhoneMirror::wireless::IpcVersion ||
       h.payload_size>1024*1024 || !std::memchr(h.device_id,0,sizeof(h.device_id)) ||
       !std::memchr(h.device_name,0,sizeof(h.device_name)))
        throw std::runtime_error("Invalid AirPlay IPC header");
    if(h.type==MessageType::Audio &&
       (!h.device_id[0] || (h.sample_rate!=44100 && h.sample_rate!=48000) ||
        h.channels!=2 || h.bits_per_sample!=16 || !h.payload_size || h.payload_size%4))
        throw std::runtime_error("AirPlay requires 44.1/48 kHz PCM16 stereo");
}
}
