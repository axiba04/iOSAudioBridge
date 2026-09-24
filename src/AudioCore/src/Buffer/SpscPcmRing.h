#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <stdexcept>

namespace audiobridge {
struct StereoFrame { int16_t left{}, right{}; };
static_assert(sizeof(StereoFrame)==4);
static_assert(std::atomic<uint64_t>::is_always_lock_free);
// Only the producer writes head; only the consumer writes tail. reset requires both stopped.
class SpscPcmRing {
public:
    explicit SpscPcmRing(size_t capacity=8192) : data_(capacity) {
        if (!capacity) throw std::invalid_argument("Ring capacity must be positive");
    }
    void reset() noexcept { head_=0; tail_=0; }
    size_t capacity() const noexcept { return data_.size(); }
    size_t available() const noexcept {
        // Telemetry may sample concurrently with both owners. Clamp an inconsistent sample.
        const auto tail=tail_.load(std::memory_order_acquire);
        const auto head=head_.load(std::memory_order_acquire);
        return head>=tail ? static_cast<size_t>(std::min<uint64_t>(head-tail,capacity())) : 0;
    }
    size_t write(std::span<const uint8_t> pcm) noexcept {
        const auto head=head_.load(std::memory_order_relaxed);
        const auto tail=tail_.load(std::memory_order_acquire);
        const auto count=std::min(pcm.size()/4, capacity()-static_cast<size_t>(head-tail));
        const auto start=static_cast<size_t>(head%capacity());
        const auto first=std::min(count,capacity()-start);
        if(first) std::memcpy(data_.data()+start,pcm.data(),first*4);
        if(count>first) std::memcpy(data_.data(),pcm.data()+first*4,(count-first)*4);
        head_.store(head+count,std::memory_order_release);
        return count;
    }
    size_t read(void* destination,size_t requested) noexcept {
        const auto tail=tail_.load(std::memory_order_relaxed);
        const auto head=head_.load(std::memory_order_acquire);
        const auto count=std::min(requested,static_cast<size_t>(head-tail));
        const auto start=static_cast<size_t>(tail%capacity());
        const auto first=std::min(count,capacity()-start);
        if(first) std::memcpy(destination,data_.data()+start,first*4);
        if(count>first) std::memcpy(static_cast<uint8_t*>(destination)+first*4,data_.data(),(count-first)*4);
        tail_.store(tail+count,std::memory_order_release);
        return count;
    }
    size_t trim_to(size_t high_water) noexcept {
        const auto tail=tail_.load(std::memory_order_relaxed);
        const auto head=head_.load(std::memory_order_acquire);
        const auto queued=static_cast<size_t>(head-tail);
        const auto drop=queued>high_water ? queued-high_water : 0;
        tail_.store(tail+drop,std::memory_order_release);
        return drop;
    }
private:
    std::vector<StereoFrame> data_;
    alignas(64) std::atomic<uint64_t> head_{};
    alignas(64) std::atomic<uint64_t> tail_{};
};
}
