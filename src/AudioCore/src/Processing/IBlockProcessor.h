#pragma once
#include <cstddef>
namespace audiobridge {
// Owner outlives the engine; prepare/release run with rendering stopped.
class IBlockProcessor {
public:
    virtual ~IBlockProcessor() = default;
    virtual void prepare(double sample_rate, size_t max_frames) = 0;
    virtual bool process(float* left, float* right, size_t frames) noexcept = 0;
    virtual void release() noexcept = 0;
};
class PassthroughProcessor final : public IBlockProcessor {
public:
    void prepare(double,size_t) override {}
    bool process(float*,float*,size_t) noexcept override { return true; }
    void release() noexcept override {}
};
}
