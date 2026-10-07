#include "IpcProtocol.h"
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
using namespace iPhoneMirror::wireless;
namespace {
std::wstring argument(int argc,wchar_t** argv,const wchar_t* name){
    for(int i=1;i+1<argc;++i)if(std::wstring_view(argv[i])==name)return argv[i+1];return {};
}
bool write(HANDLE pipe,const void* data,size_t count){
    const auto* bytes=static_cast<const unsigned char*>(data);
    // Deliberately fragment both headers and payloads to exercise stream reads.
    while(count){DWORD sent{};if(!WriteFile(pipe,bytes,static_cast<DWORD>(std::min<size_t>(count,137)),&sent,nullptr)||!sent)return false;bytes+=sent;count-=sent;}
    return true;
}
void send(HANDLE pipe,MessageHeader h,const std::vector<int16_t>& pcm={}){
    h.payload_size=static_cast<uint32_t>(pcm.size()*2);write(pipe,&h,sizeof(h));if(!pcm.empty())write(pipe,pcm.data(),pcm.size()*2);
}
MessageHeader device(MessageType type,const char* id="phone-a"){
    MessageHeader h;h.type=type;strcpy_s(h.device_id,id);return h;
}
}
int wmain(int argc,wchar_t** argv){
    const auto name=argument(argc,argv,L"--name");
    const auto stop=OpenEventW(SYNCHRONIZE,FALSE,argument(argc,argv,L"--stop-event").c_str());
    if(name==L"no-connect"){WaitForSingleObject(stop,10000);CloseHandle(stop);return 0;}
    const auto pipe=CreateFileW(argument(argc,argv,L"--pipe").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    if(pipe==INVALID_HANDLE_VALUE)return 2;
    send(pipe,MessageHeader{});
    if(name==L"bad-header"){
        MessageHeader h;h.payload_size=0xffffffff;write(pipe,&h,sizeof(h));
    }else if(name==L"partial"){
        MessageHeader h;write(pipe,&h,9);
    }else if(name==L"crash"){
        CloseHandle(pipe);CloseHandle(stop);return 3;
    }else if(name==L"stream"){
        auto volume=device(MessageType::MediaVolume);volume.reserved=static_cast<uint32_t>(MediaVolumeTarget::MirroredStream);volume.media_volume=0;
        send(pipe,volume); // RAOP can send mute before Connected or first PCM.
        std::vector<int16_t> pcm(1024*2);
        for(size_t i=0;i<pcm.size();i+=2){pcm[i]=16000;pcm[i+1]=-8000;}
        auto audio=device(MessageType::Audio);audio.sample_rate=44100;audio.channels=2;audio.bits_per_sample=16;
        send(pipe,audio,pcm);
        volume.media_volume=.5;send(pipe,volume);send(pipe,audio,pcm);
        auto second=audio;strcpy_s(second.device_id,"phone-b");send(pipe,second,pcm);
        send(pipe,device(MessageType::Disconnected,"phone-b")); // Must not disconnect phone-a.
        audio.sample_rate=48000;send(pipe,audio,pcm); // Format change within the route.
        send(pipe,device(MessageType::Disconnected));
        audio.sample_rate=44100;send(pipe,audio,pcm); // First PCM reconnect, no Connected.
    }
    WaitForSingleObject(stop,10000);CloseHandle(pipe);CloseHandle(stop);return 0;
}
