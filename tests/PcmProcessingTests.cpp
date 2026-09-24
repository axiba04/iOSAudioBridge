#include "Processing/PcmBlockRenderer.h"
#include "Buffer/PcmPipe.h"
#include <iostream>
#include <limits>
using namespace audiobridge;
namespace {
void require(bool result,const char* message){if(!result)throw std::runtime_error(message);}
struct Processor final : IBlockProcessor {
    size_t calls{},total{},max{};bool fail{},invalid{};
    void prepare(double,size_t) override {}
    void release() noexcept override {}
    bool process(float* l,float* r,size_t n) noexcept override {
        ++calls;total+=n;max=std::max(max,n);
        if(invalid){l[0]=std::numeric_limits<float>::quiet_NaN();r[0]=std::numeric_limits<float>::infinity();}
        return !fail;
    }
};
}
int main(){try{
    for(const size_t count:{size_t(1),size_t(127),size_t(480),size_t(1024),size_t(4096),size_t(8191)}) {
        Processor p;PcmBlockRenderer renderer;PcmBlockRenderer::Levels levels;
        std::vector<StereoFrame> warm(480);require(renderer.render(warm.data(),warm.size(),1,p,levels),"Warmup failed");p.calls=0;p.total=0;
        std::vector<StereoFrame> samples(count+1,{-32768,32767});const auto guard=samples.back();
        require(renderer.render(samples.data(),count,1,p,levels),"Passthrough failed");
        require(p.total==count&&p.max<=4096,"Wrong block size / extra samples processed");
        for(size_t i=0;i<count;++i)require(samples[i].left==-32768&&samples[i].right==32767,"PCM extrema or channel order changed");
        require(samples.back().left==guard.left&&samples.back().right==guard.right,"Tail guard overwritten");
    }
    Processor p;PcmBlockRenderer renderer;PcmBlockRenderer::Levels levels;
    std::vector<StereoFrame> signal(480,{16000,-8000});
    renderer.render(signal.data(),signal.size(),0,p,levels);
    require(p.calls==1,"Mute skipped DSP / analyzer processing");
    for(auto f:signal)require(f.left==0&&f.right==0,"Mute is not silent");
    p.invalid=true;require(renderer.render(signal.data(),480,1,p,levels),"Nonfinite handling failed");require(levels.nonfinite==2,"Nonfinite not counted");
    p.fail=true;require(!renderer.render(signal.data(),480,1,p,levels),"DSP failure not reported");
    for(auto f:signal)require(f.left==0&&f.right==0,"Failed block leaked audio");
    PcmPipe pipe;std::vector<uint8_t> overflow((pipe.ring.capacity()+12)*4);pipe.enqueue(overflow);
    require(pipe.dropped==12&&pipe.received==8204,"Producer overflow telemetry lost");
    pipe.reset();require(pipe.ring.available()==0&&pipe.received==0&&pipe.generation==1,"Stopped pipe reset failed");
    std::cout<<"PCM conversion, channel order, chunk limits, mute, NaN/Inf, processor failure and pipe telemetry passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
