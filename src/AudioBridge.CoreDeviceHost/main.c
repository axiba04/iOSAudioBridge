/* Replaces cdhost's GUI/control server with one audio stream and a private PCM pipe. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "api_server.h"
#include "media.h"
#include "Capture/CoreDeviceProtocol.h"
#include "Decoder.h"

int cdhost_main(int,char **);
static HANDLE output, stop_event;
static CRITICAL_SECTION output_lock;
static volatile LONG failed;
static AB_EldDecoder *decoder;

static int send_message(uint32_t type,const void *data,uint32_t size) {
    AB_CD_Header header = {AB_CD_MAGIC,type,size}; DWORD written;
    EnterCriticalSection(&output_lock);
    int ok = WriteFile(output,&header,sizeof header,&written,NULL) && written == sizeof header;
    if(ok && size) ok = WriteFile(output,data,size,&written,NULL) && written == size;
    LeaveCriticalSection(&output_lock);
    if(!ok) InterlockedExchange(&failed,1);
    return ok ? 0 : -1;
}
static void error(const char *message) { send_message(AB_CD_ERROR,message,(uint32_t)strlen(message)); }
static int pcm(void *ctx,const int16_t *data,size_t bytes) {
    (void)ctx; return send_message(AB_CD_AUDIO,data,(uint32_t)bytes);
}
static void audio(void *ctx,const uint8_t *frame,size_t size,uint32_t timestamp) {
    (void)ctx; (void)timestamp;
    if(!failed && ab_eld_decode(decoder,frame,size,pcm,NULL) != 0) {
        error("AAC-ELD decode failed; expected 48 kHz stereo / 480 samples.");
        InterlockedExchange(&failed,1);
    }
}
int api_serve(api_session *session) {
    if(!session->display_port) {
        error("displayservice unavailable after DDI activation. Check iOS/iPadOS 27+, Developer Mode and the CoreDevice log.");
        return -1;
    }
    media_config cfg = {
        .device_addr=session->tunnel_addr, .our_addr=session->our_addr,
        .display_port=session->display_port, .ssrc=session->ssrc ^ 0x5a5a0001u,
        .audio=1, .on_audio=audio,
    };
    media_session *stream = media_start(&cfg,NULL,NULL);
    if(!stream) {error("Audio negotiation failed. See the CoreDevice log.");return -1;}
    const char *status = "Audio negotiated; waiting for device audio.";
    send_message(AB_CD_STATUS,status,(uint32_t)strlen(status));
    while(!failed && WaitForSingleObject(stop_event,50) == WAIT_TIMEOUT) {}
    media_stop(stream);
    return failed ? -1 : 0;
}
int wmain(int argc,wchar_t **argv) {
    const wchar_t *udid=NULL,*stop=NULL;
    for(int i=1;i<argc;++i) {
        if(!wcscmp(argv[i],L"--udid") && i+1<argc) udid=argv[++i];
        else if(!wcscmp(argv[i],L"--stop-event") && i+1<argc) stop=argv[++i];
        else if(!wcscmp(argv[i],L"--check-runtime")) {
            decoder=ab_eld_open(); if(!decoder)return 1;ab_eld_close(decoder);return 0;
        } else return 2;
    }
    if(!udid || !stop) {fputs("This helper is launched by iOSAudioBridge.\n",stderr);return 2;}
    stop_event=OpenEventW(SYNCHRONIZE,FALSE,stop);
    if(!stop_event) return 1;
    /* Preserve the binary pipe, then redirect the engine's printf output to the log. */
    if(!DuplicateHandle(GetCurrentProcess(),GetStdHandle(STD_OUTPUT_HANDLE),GetCurrentProcess(),
                        &output,0,FALSE,DUPLICATE_SAME_ACCESS)) return 1;
    _dup2(_fileno(stderr),_fileno(stdout));
    InitializeCriticalSection(&output_lock);
    decoder=ab_eld_open();
    if(!decoder) {error("Cannot initialize AAC-ELD decoder.");return 1;}
    wchar_t path[32768];
    DWORD length=GetModuleFileNameW(NULL,path,32768);
    if(!length || length>=32768) return 1;
    wchar_t *slash=wcsrchr(path,L'\\'); if(!slash)return 1; *slash=0;
    if(wcslen(path)+10>=32768) return 1;
    wcscat(path,L"\\iOS_DDI"); _wputenv_s(L"RPLAY_DDI",path);
    _putenv_s("RPLAY_USERSPACE_NET","1"); _putenv_s("RPLAY_ATTACH_TUNNEL","0");
    char serial[128];
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,udid,-1,serial,sizeof serial,NULL,NULL)) return 2;
    char *args[]={"AudioBridge.CoreDeviceHost","--udid",serial,NULL};
    const char *status="Connecting to device and activating DDI...";
    send_message(AB_CD_STATUS,status,(uint32_t)strlen(status));
    int rc=cdhost_main(3,args);
    if(WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0) rc=0;
    else if(rc && !failed) error("CoreDevice connection ended. Check trust, Developer Mode and the CoreDevice log.");
    ab_eld_close(decoder); CloseHandle(output); CloseHandle(stop_event); DeleteCriticalSection(&output_lock);
    return rc;
}
