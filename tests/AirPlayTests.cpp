#include "Capture/AirPlayAudioSource.h"
#include "Capture/AirPlayProtocol.h"
#include "Processing/PcmBlockRenderer.h"
#include "Engine/AudioEngine.h"
#include <barrier>
#include <iostream>
#include <chrono>
#include <limits>
using namespace audiobridge;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void await(F condition){
    const auto deadline=GetTickCount64()+5000;
    while(!condition()){if(GetTickCount64()>deadline)throw std::runtime_error("AirPlay test timed out");Sleep(10);}
}
struct Compressor : IBlockProcessor {
    void prepare(double,size_t) override{}
    void release() noexcept override{}
    bool process(float* l,float* r,size_t n) noexcept override{for(size_t i=0;i<n;++i){l[i]*=.25f;r[i]*=.25f;}return true;}
};
}
int wmain(int argc,wchar_t** argv){try{
    require(argc==2 || argc==3,"Test host path required");
    if(argc==3 && std::wstring_view(argv[2])==L"--receiver"){
        PcmPipe receiverPipe;
        AirPlayAudioSource receiver(receiverPipe,{[](const AB_Config&,uint32_t){throw std::runtime_error("Unexpected audio during receiver smoke");},[]{},[]{}});
        AB_Config config{};config.volume=0;
        receiver.start(config,argv[1],L"ZL Audio Bridge Test");
        await([&]{const auto state=receiver.status().state;return state==AB_READY || state==AB_ERROR;});
        const auto ready=receiver.status();receiver.stop();
        if(ready.state!=AB_READY)throw std::runtime_error("Real AirPlay receiver did not become ready");
        require(receiver.status().state==AB_STOPPED,"Real AirPlay receiver did not stop");
        std::cout<<"Real AirPlay receiver ready and stopped; no phone audio received or rendered\n";return 0;
    }
    airplay::MessageHeader h;h.type=airplay::MessageType::Audio;strcpy_s(h.device_id,"sender");
    h.sample_rate=44100;h.channels=2;h.bits_per_sample=16;h.payload_size=1408;
    airplay::validate_header(h);
    const auto reject=[](const airplay::MessageHeader& invalid){try{airplay::validate_header(invalid);return false;}catch(const std::runtime_error&){return true;}};
    auto invalid=h;invalid.sample_rate=22050;require(reject(invalid),"Wrong sample rate accepted");
    invalid=h;invalid.payload_size=7;require(reject(invalid),"Partial PCM frame accepted");
    invalid=h;invalid.channels=1;require(reject(invalid),"Unsupported channel format accepted");
    invalid=h;invalid.bits_per_sample=32;require(reject(invalid),"Float/wide PCM accepted as PCM16");
    invalid=h;invalid.magic=0;require(reject(invalid),"Bad magic accepted");
    invalid=h;invalid.version=0;require(reject(invalid),"Bad version accepted");
    invalid=h;std::memset(invalid.device_id,'a',sizeof(invalid.device_id));require(reject(invalid),"Unterminated sender accepted");
    invalid=h;invalid.payload_size=0xffffffff;require(reject(invalid),"Unbounded payload accepted");
    PcmPipe pipe;Compressor compressor;PcmBlockRenderer renderer;
    std::vector<uint32_t> rates;std::vector<int16_t> rendered;std::atomic<int> packets{};
    AirPlayAudioSource source(pipe,{
        [&](const AB_Config&,uint32_t rate){rates.push_back(rate);pipe.reset();renderer.reset();},
        [&]{
            std::vector<StereoFrame> frames(pipe.ring.available());
            if(frames.empty())return;
            const auto n=pipe.ring.read(reinterpret_cast<uint8_t*>(frames.data()),frames.size());
            PcmBlockRenderer::Levels levels;
            require(renderer.render(frames.data(),n,pipe.remote_gain,compressor,levels),"PCM processor failed");
            // Last frame is past the gain ramp. Verify compressor and remote gain.
            rendered.push_back(frames.back().left);require(frames.back().right==-frames.back().left/2,"Stereo channels changed");++packets;
        },[]{}});
    AB_Config config{};config.volume=.5;
    source.start(config,argv[1],L"stream");await([&]{return source.status().audio_packets==4 || source.status().state==AB_ERROR;});source.stop();
    require(source.status().state==AB_STOPPED,"Streaming receiver failed");
    require(packets==4,"Other sender mixed into selected stream");
    require(rates==std::vector<uint32_t>{44100,48000,44100},"Format change or reconnect did not reprepare output");
    require(rendered==std::vector<int16_t>{0,2000,2000,4000},"PCM bypassed compressor or remote mute/volume");
    for(const auto name:{L"partial",L"no-connect"}){
        source.start(config,argv[1],name);Sleep(100);const auto began=GetTickCount64();source.stop();
        require(GetTickCount64()-began<4500,"Stop hung on a blocked IPC operation");
        require(source.status().state==AB_STOPPED,"Normal cancellation reported an error");
    }
    for(const auto name:{L"bad-header",L"crash"}){
        source.start(config,argv[1],name);await([&]{return source.status().state==AB_ERROR;});source.stop();
        require(source.status().error_code==AB_FAILURE,"Host failure not surfaced");
    }
    // Reap a failed worker and allow a fresh receiver start.
    source.start(config,argv[1],L"partial");await([&]{return source.status().state==AB_READY;});source.stop();
    bool missing=false;try{source.start(config,L"missing-airplay-host.exe");}catch(const std::invalid_argument&){missing=true;}
    require(missing,"Missing runtime was accepted");
    AudioEngine engine;
    std::barrier startGate(3);std::atomic<int> started{},rejected{};
    const auto start=[&]{startGate.arrive_and_wait();try{engine.startAirPlay(config,argv[1],L"partial");++started;}catch(const std::runtime_error&){++rejected;}};
    std::jthread first(start),second(start);startGate.arrive_and_wait();first.join();second.join();
    require(started==1 && rejected==1,"Concurrent starts replaced an active source");
    bool usbRejected=false;try{engine.start(config);}catch(const std::runtime_error&){usbRejected=true;}
    require(usbRejected,"USB was allowed to replace an active AirPlay source");engine.stop();
    std::cout<<"AirPlay PCM routing, compression, sender isolation, rates, volume, reconnect, malformed IPC, crash and cancellation passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
