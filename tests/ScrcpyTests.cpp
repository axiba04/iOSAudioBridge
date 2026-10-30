#include "Android/AdbClient.h"
#include "Capture/ScrcpyAudioSource.h"
#include "Capture/ScrcpyProtocol.h"
#include "Engine/AudioEngine.h"
#include "Device/DeviceCatalog.h"
#include "Processing/PcmBlockRenderer.h"
#include <fstream>
#include <iostream>

using namespace audiobridge;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> bool rejects(F action){try{action();return false;}catch(const std::exception&){return true;}}
template<class F> void await(F condition){
    const auto deadline=GetTickCount64()+7000;
    while(!condition()){if(GetTickCount64()>=deadline)throw std::runtime_error("scrcpy test timed out");Sleep(10);}
}
struct Compressor : IBlockProcessor {
    void prepare(double,size_t) override{}
    void release() noexcept override{}
    bool process(float* l,float* r,size_t n) noexcept override{for(size_t i=0;i<n;++i){l[i]*=.25f;r[i]*=.25f;}return true;}
};
void protocol_tests(){
    const std::vector<uint8_t> wire{0,0,0x72,0x61,0x77, 0,0,0,0,0,0,0,1,0,0,0,4, 0x80,0x3e,0xc0,0xe0};
    for(size_t split=0;split<=wire.size();++split){
        int ready=0,packets=0;scrcpy::AudioParser parser([&]{++ready;},[&](auto pcm){
            require(ready==1,"PCM before codec");require(pcm.size()==4&&pcm[1]==0x3e&&pcm[3]==0xe0,"PCM bytes changed");++packets;
        });
        parser.feed(std::span(wire).first(split));parser.feed(std::span(wire).subspan(split));
        require(ready==1&&packets==1,"Fragmented scrcpy packet lost");
    }
    auto reject=[](const auto& bytes){scrcpy::AudioParser parser([]{},[](auto){});return rejects([&]{parser.feed(bytes);});};
    auto malformed=wire;malformed[0]=1;require(reject(malformed),"Bad dummy accepted");
    for(uint8_t code:{uint8_t(0),uint8_t(1),uint8_t(2)}){
        malformed=wire;malformed[2]=malformed[3]=0;malformed[4]=code;require(reject(malformed),"Disabled/unknown codec accepted");
    }
    for(uint8_t flag:{uint8_t(0x80),uint8_t(0x40),uint8_t(0x20)}){
        malformed=wire;malformed[5]=flag;require(reject(malformed),"Raw stream flags accepted");
    }
    for(uint32_t size:{0u,3u,4097u,0xffffffffu}){
        malformed=wire;for(int i=0;i<4;++i)malformed[13+i]=static_cast<uint8_t>(size>>(24-i*8));
        require(reject(malformed),"Invalid/unbounded payload accepted");
    }
    int packets=0;scrcpy::AudioParser parser([]{},[&](auto){++packets;});
    for(auto byte:wire)parser.feed(std::span(&byte,1));parser.feed(std::span(wire).subspan(5));
    require(packets==2,"Bytewise/multiple packets lost");
}
}
int wmain(int argc,wchar_t** argv){try{
    require(argc==2,"Fake adb executable required");protocol_tests();
    const auto parsed=android::parse_devices("List of devices attached\nABC device usb:1-1 model:Pixel_9\nunauth unauthorized\noff offline\n10.0.0.1:1234 device model:Wifi\nadb-id._adb-tls-connect._tcp device\nemulator-5554 device\n",android::Transport::Usb);
    require(parsed.size()==3&&parsed[0].accessible&&!parsed[1].accessible&&!parsed[2].accessible,"ADB USB status parsing failed");
    require(android::parse_devices("10.0.0.1:1234 device\nadb-id._adb-tls-connect._tcp device\nABC device\n",android::Transport::Wireless).size()==2,"Wireless filtering failed");
    for(auto endpoint:{L"192.168.1.2:5555",L"[::1]:12345",L"phone.local:12345"})android::validate_endpoint(endpoint);
    for(auto endpoint:{L"",L"-x:5",L"1.2.3.4",L"host:0",L"host:65536",L"host:123 & calc",L"host:abc"})
        require(rejects([&]{android::validate_endpoint(endpoint);}),"Unsafe/bad endpoint accepted");
    // Exercise real CreateProcess quoting with spaces, Unicode and trailing slash.
    const auto root=std::filesystem::temp_directory_path()/(L"AudioBridge scrcpy 测试 "+std::to_wstring(GetCurrentProcessId())+L" "+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root);
    struct Cleanup {std::filesystem::path root;~Cleanup(){std::error_code ec;std::filesystem::remove_all(root,ec);}} cleanup{root};
    std::filesystem::copy_file(argv[1],root/L"adb.exe");std::ofstream(root/L"scrcpy-server")<<"fixture";
    android::AdbClient adb(root/L"adb.exe");
    const auto quoted=adb.run({L"echo-args",L"a b",L"c\"d",L"path with spaces\\"});
    require(quoted=="a b\r\nc\"d\r\npath with spaces\\\r\n","Windows argv quoting failed");
    require(adb.devices(android::Transport::Usb).size()==1,"ADB process enumeration failed");
    require(rejects([&]{adb.connect(L"host:1234");}),"adb connect exit=0 failure accepted");
    require(rejects([&]{adb.pair(L"host:1234",L"12345");}),"Bad pairing code accepted");
    adb.pair(L"host:1234",L"123456");
    PcmPipe pipe;Compressor compressor;PcmBlockRenderer renderer;std::atomic<int> packets{},prepared{},stopped{};std::atomic<bool> failOutput{};
    std::vector<StereoFrame> rendered;
    ScrcpyAudioSource source(pipe,{
        [&](const AB_Config& config,uint32_t rate){require(rate==48000,"Wrong scrcpy rate");require(config.startup_frames==1024||config.startup_frames==3072,"Wrong transport buffering");renderer.reset();++prepared;},
        [&]{
            if(failOutput)throw std::runtime_error("Simulated output disconnection");
            std::vector<StereoFrame> frames(pipe.ring.available());if(frames.empty())return;
            const auto count=pipe.ring.read(frames.data(),frames.size());PcmBlockRenderer::Levels levels;
            require(renderer.render(frames.data(),count,1,compressor,levels),"Processor failed");rendered.push_back(frames.back());++packets;
        },[&]{++stopped;}});
    AB_Config config{};config.volume=.5f;copy_text(config.device_id,L"stream");
    source.start(config,root,false);await([&]{return source.status().audio_packets==2||source.status().state==AB_ERROR;});source.stop();
    require(source.status().state==AB_STOPPED&&packets==2&&prepared==1&&stopped>0,"Streaming lifecycle failed");
    require(rendered.size()==2&&rendered.back().left==4000&&rendered.back().right==-2000,"Android PCM bypassed DSP/stereo changed");
    std::ifstream log(root/L"stream.log");const std::string events{std::istreambuf_iterator<char>(log),{}};
    require(events.find("remove-forward")!=std::string::npos&&events.find("remove-file")!=std::string::npos,"Session resources leaked");
    for(auto mode:{L"partial",L"no-codec",L"push-hang",L"forward-hang"}){
        copy_text(config.device_id,mode);source.start(config,root,true);Sleep(450);const auto began=GetTickCount64();source.stop();
        require(GetTickCount64()-began<4500&&source.status().state==AB_STOPPED,"Stop blocked/cancellation became an error");
    }
    std::ifstream cancelledLog(root/L"forward-hang.log");const std::string cancelledEvents{std::istreambuf_iterator<char>(cancelledLog),{}};
    require(cancelledEvents.find("remove-forward")!=std::string::npos,"Cancelled forward allocation leaked a tunnel");
    for(auto mode:{L"bad-size",L"disabled",L"crash",L"unauthorized",L"old-android"}){
        copy_text(config.device_id,mode);source.start(config,root,false);await([&]{return source.status().state==AB_ERROR;});source.stop();
        require(source.status().error_code==AB_FAILURE,"Device/protocol error not surfaced");
    }
    copy_text(config.device_id,L"stream");source.start(config,root,true);await([&]{return source.status().audio_packets==2||source.status().state==AB_ERROR;});source.stop();
    require(source.status().state==AB_STOPPED,"Restart after error failed");
    failOutput=true;source.start(config,root,false);await([&]{return source.status().state==AB_ERROR;});source.stop();
    require(source.status().error_code==AB_FAILURE,"Output failure not surfaced");failOutput=false;
    // No codec means the real output is never opened; still enforce cross-source exclusion.
    AudioEngine engine;copy_text(config.device_id,L"no-codec");engine.startScrcpy(config,root);
    require(rejects([&]{engine.start(config);}),"iOS USB replaced an active scrcpy stream");
    require(rejects([&]{engine.startAirPlay(config,L"missing.exe");}),"AirPlay replaced an active scrcpy stream");
    require(rejects([&]{engine.startScrcpy(config,root);}),"Concurrent Android start replaced a stream");engine.stop();
    require(rejects([&]{source.start(config,root/L"missing",false);}),"Missing runtime accepted");
    std::cout<<"scrcpy framing, ADB parsing/quoting, USB/Wi-Fi lifecycle, DSP, cleanup, cancellation, failure/restart and source isolation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
