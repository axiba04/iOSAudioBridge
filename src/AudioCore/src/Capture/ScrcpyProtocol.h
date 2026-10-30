#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace audiobridge::scrcpy {
// scrcpy 5.0.1: dummy byte, raw codec id, then big-endian PTS/size + PCM16LE.
// Device metadata is disabled. Keep stream/frame metadata to detect failures.
class AudioParser {
public:
    AudioParser(std::function<void()> ready,std::function<void(std::span<const uint8_t>)> packet)
        :ready_(std::move(ready)),packet_(std::move(packet)){buffer_.reserve(4096);}
    void feed(std::span<const uint8_t> bytes){
        while(!bytes.empty()){
            const auto count=std::min(needed_-buffer_.size(),bytes.size());
            buffer_.insert(buffer_.end(),bytes.begin(),bytes.begin()+count);bytes=bytes.subspan(count);
            if(buffer_.size()!=needed_)continue;
            if(stage_==Stage::Dummy){
                if(buffer_[0]!=0)throw std::runtime_error("Invalid scrcpy handshake");
                stage_=Stage::Codec;needed_=4;
            }else if(stage_==Stage::Codec){
                const auto codec=be32(buffer_.data());
                if(codec==0)throw std::runtime_error("Android 音频采集不可用：需要 Android 11+，Android 11 启动时请解锁屏幕");
                if(codec==1)throw std::runtime_error("scrcpy 音频初始化失败，请检查设备系统支持");
                if(codec!=0x00726177)throw std::runtime_error("scrcpy codec/version mismatch: raw PCM required");
                stage_=Stage::Header;needed_=12;ready_();
            }else if(stage_==Stage::Header){
                const auto size=be32(buffer_.data()+8);
                if((buffer_[0]&0xe0)!=0||!size||size>4096||size%4)
                    throw std::runtime_error("Invalid scrcpy raw audio packet");
                stage_=Stage::Payload;needed_=size;
            }else{
                packet_(buffer_);stage_=Stage::Header;needed_=12;
            }
            buffer_.clear();
        }
    }
private:
    static uint32_t be32(const uint8_t* b){return (uint32_t(b[0])<<24)|(uint32_t(b[1])<<16)|(uint32_t(b[2])<<8)|b[3];}
    enum class Stage {Dummy,Codec,Header,Payload};
    Stage stage_{Stage::Dummy};size_t needed_{1};std::vector<uint8_t> buffer_;
    std::function<void()> ready_;
    std::function<void(std::span<const uint8_t>)> packet_;
};
}
