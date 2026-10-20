#include "Capture/CoreDeviceAudioSource.h"
#include "Processing/PcmBlockRenderer.h"
#include "Device/DeviceCatalog.h"
#include <iostream>
using namespace audiobridge;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void await(F condition,ULONGLONG timeout=5000){const auto until=GetTickCount64()+timeout;while(!condition()){if(GetTickCount64()>until)throw std::runtime_error("CoreDevice test timed out");Sleep(10);}}
struct Compressor : IBlockProcessor {
    void prepare(double,size_t) override{} void release() noexcept override{}
    bool process(float* l,float* r,size_t n) noexcept override{for(size_t i=0;i<n;++i){l[i]*=.25f;r[i]*=.25f;}return true;}
};
}
int wmain(int argc,wchar_t** argv){try{
    require(argc==2 || argc==3,"Test host required");
    if(argc==3){
        PcmPipe noDevicePipe;
        CoreDeviceAudioSource real(noDevicePipe,{[](const AB_Config&,uint32_t){throw std::runtime_error("Unexpected audio from nonexistent UDID");},[]{},[]{}});
        AB_Config config{};config.volume=0;copy_text(config.device_id,L"test-no-such-device");
        real.start(config,argv[1]);await([&]{return real.status().state==AB_ERROR;},15000);real.stop();
        require(real.status().audio_packets==0,"Unexpected audio from missing device");
        std::cout<<"PASS real CoreDevice host bootstrap failure reaches parent\n";return 0;
    }
    PcmPipe pipe;Compressor compressor;PcmBlockRenderer renderer;
    std::atomic<int> packets{},prepared{},stopped{};
    CoreDeviceAudioSource source(pipe,{
        [&](const AB_Config&,uint32_t rate){require(rate==48000,"Wrong sample rate");++prepared;renderer.reset();},
        [&]{
            std::vector<StereoFrame> frames(pipe.ring.available());if(frames.empty())return;
            auto n=pipe.ring.read(reinterpret_cast<uint8_t*>(frames.data()),frames.size());
            PcmBlockRenderer::Levels levels;
            require(renderer.render(frames.data(),n,1,compressor,levels),"PCM processing failed");
            require(frames.back().left==2000 && frames.back().right==-1000,"CoreDevice PCM bypassed compressor or changed channels");++packets;
        },[&]{++stopped;}});
    AB_Config config{};config.volume=.5f;copy_text(config.device_id,L"stream");
    source.start(config,argv[1]);await([&]{return source.status().audio_packets==3 || source.status().state==AB_ERROR;});
    require(source.status().state==AB_STREAMING,"Never reached streaming");source.stop();
    require(packets==3 && prepared==1 && stopped>0,"PCM routing/lifecycle failed");
    require(source.status().state==AB_STOPPED,"Stop failed");
    for(auto mode:{L"silent",L"partial"}){
        copy_text(config.device_id,mode);source.start(config,argv[1]);Sleep(100);
        require(source.status().state==AB_CONNECTING,"Reported streaming before PCM");
        auto start=GetTickCount64();source.stop();require(GetTickCount64()-start<4500,"Stop blocked on pipe/bootstrap");
    }
    for(auto mode:{L"error",L"malformed",L"exit"}){
        copy_text(config.device_id,mode);source.start(config,argv[1]);await([&]{return source.status().state==AB_ERROR;});
        if(std::wstring_view(mode)==L"error")require(std::wstring(source.status().message).find(L"displayservice")!=std::wstring::npos,"Lost host error");
        source.stop();
    }
    copy_text(config.device_id,L"stream");source.start(config,argv[1]);await([&]{return source.status().audio_packets==3;});source.stop();
    std::cout<<"PASS CoreDevice PCM through compressor, channels, lifecycle, restart, errors and cancellation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
