#include "Capture/CoreDeviceProtocol.h"
#include <Windows.h>
#include <array>
#include <string>

int wmain(int argc,wchar_t** argv){
    std::wstring mode,stop;
    for(int i=1;i+1<argc;++i){
        if(std::wstring_view(argv[i])==L"--udid")mode=argv[++i];
        else if(std::wstring_view(argv[i])==L"--stop-event")stop=argv[++i];
    }
    HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,stop.c_str());if(!event)return 2;
    const auto send=[](uint32_t type,const void* data,uint32_t bytes){
        AB_CD_Header header{AB_CD_MAGIC,type,bytes};DWORD written{};
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),&header,sizeof header,&written,nullptr);
        if(bytes)WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),data,bytes,&written,nullptr);
    };
    if(mode==L"stream"){
        std::array<int16_t,960> pcm{};for(size_t i=0;i<480;++i){pcm[2*i]=8000;pcm[2*i+1]=-4000;}
        for(int i=0;i<3;++i){send(AB_CD_AUDIO,pcm.data(),sizeof pcm);Sleep(20);}
    }else if(mode==L"error"){
        const char text[]="displayservice unavailable after DDI activation";
        send(AB_CD_ERROR,text,sizeof text-1);
    }else if(mode==L"malformed"){
        AB_CD_Header h{AB_CD_MAGIC,AB_CD_AUDIO,7};DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),&h,sizeof h,&written,nullptr);
    }else if(mode==L"partial"){
        AB_CD_Header h{AB_CD_MAGIC,AB_CD_AUDIO,1920};DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),&h,5,&written,nullptr);
    }else if(mode==L"exit"){CloseHandle(event);return 1;}
    WaitForSingleObject(event,15000);CloseHandle(event);return 0;
}
