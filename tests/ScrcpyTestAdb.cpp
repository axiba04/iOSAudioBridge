#include "Transport/Socket.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <array>

// A process-level adb/server double. Never invokes adb or contacts a phone.
int wmain(int argc,wchar_t** argv){try{
    std::vector<std::wstring> args(argv+1,argv+argc);
    if(args.empty())return 1;
    if(args[0]==L"echo-args"){
        for(size_t i=1;i<args.size();++i)std::wcout<<args[i]<<L'\n';return 0;
    }
    if(args[0]==L"devices"){
        std::cout<<"List of devices attached\nusb-device device usb:1-1 model:Test_Phone\n10.1.2.3:5555 device model:Wireless_Phone\n";return 0;
    }
    if(args[0]==L"connect"){std::cout<<"failed to connect to device\n";return 0;}
    if(args[0]==L"pair"){std::cout<<"Successfully paired\n";return 0;}
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto root=std::filesystem::path(exe).parent_path();
    if(args[0]==L"forward"&&args.size()==2&&args[1]==L"--list"){
        std::ifstream mapping(root/L"forward-hang.mapping");std::cout<<mapping.rdbuf();return 0;
    }
    if(args.size()<3||args[0]!=L"-s")return 2;
    const auto mode=args[1];
    const auto portFile=root/(mode+L".port");
    const auto log=[&](const char* value){std::ofstream out(root/(mode+L".log"),std::ios::app);out<<value<<'\n';};
    if(args[2]==L"get-state"){
        if(mode==L"unauthorized"){std::cerr<<"device unauthorized";return 1;}
        std::cout<<"device\n";return 0;
    }
    if(args[2]==L"push"){
        log("push");if(mode==L"push-hang")Sleep(60000);return 0;
    }
    if(args[2]==L"forward"){
        if(args.size()!=5)return 3;
        if(args[3]==L"--remove"){log("remove-forward");return 0;}
        if(args[3]!=L"tcp:0"||!args[4].starts_with(L"localabstract:scrcpy_"))return 4;
        iPhoneMirror::transport::ensure_winsock();
        iPhoneMirror::transport::Socket socket(::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(socket.native_handle(),reinterpret_cast<sockaddr*>(&address),sizeof(address)))return 5;
        int size=sizeof(address);getsockname(socket.native_handle(),reinterpret_cast<sockaddr*>(&address),&size);
        const auto port=ntohs(address.sin_port);std::ofstream(portFile)<<port;log("forward");
        if(mode==L"forward-hang"){
            {std::ofstream mapping(root/L"forward-hang.mapping");mapping<<"forward-hang tcp:"<<port<<' '<<std::string(args[4].begin(),args[4].end())<<'\n';}
            Sleep(60000);
        }
        std::cout<<port<<'\n';return 0;
    }
    if(args[2]!=L"shell"||args.size()!=4)return 6;
    const auto command=args[3];
    if(command==L"getprop ro.build.version.sdk"){std::cout<<(mode==L"old-android"?29:35)<<'\n';return 0;}
    if(command.starts_with(L"rm -f /data/local/tmp/audiobridge-")){log("remove-file");return 0;}
    for(const auto* option:{L"audio_codec=raw",L"video=false",L"control=false",L"send_device_meta=false",L"send_stream_meta=true",L"tunnel_forward=true"})
        if(command.find(option)==std::wstring::npos)return 7;
    unsigned short port{};std::ifstream(portFile)>>port;
    iPhoneMirror::transport::ensure_winsock();
    iPhoneMirror::transport::Socket listener(::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);
    if(bind(listener.native_handle(),reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(listener.native_handle(),1))return 8;
    iPhoneMirror::transport::Socket client(accept(listener.native_handle(),nullptr,nullptr));client.set_timeout(5000);
    client.send_all(std::array<uint8_t,1>{0});
    if(mode==L"no-codec"){Sleep(60000);return 0;}
    if(mode==L"crash")return 9;
    if(mode==L"disabled"){client.send_all(std::array<uint8_t,4>{});Sleep(100);return 0;}
    client.send_all(std::array<uint8_t,2>{0,0x72});Sleep(220);
    client.send_all(std::array<uint8_t,2>{0x61,0x77});
    std::array<uint8_t,12> header{};header[10]=0x10; // 4096 bytes
    if(mode==L"bad-size"){header[8]=0x7f;client.send_all(header);Sleep(100);return 0;}
    for(int packet=0;packet<2;++packet){
        for(const auto byte:header)client.send_all(std::array<uint8_t,1>{byte});
        if(mode==L"partial"){client.send_all(std::array<uint8_t,3>{0x80,0x3e,0xc0});Sleep(60000);return 0;}
        std::vector<uint8_t> pcm;
        for(int frame=0;frame<1024;++frame)pcm.insert(pcm.end(),{0x80,0x3e,0xc0,0xe0}); // 16000, -8000 PCM16LE
        client.send_all(pcm);
    }
    std::array<uint8_t,1> unused{};
    (void)client.receive(unused);return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 10;}}
