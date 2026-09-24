#include "AudioCore.h"
#include "Wasapi/WasapiOutput.h"
#include "Device/DeviceCatalog.h"
#include <Windows.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <string>
#include <atomic>

namespace {
std::atomic<bool> interrupted{};
BOOL WINAPI console_handler(DWORD event){if(event==CTRL_C_EVENT || event==CTRL_BREAK_EVENT){interrupted=true;return TRUE;}return FALSE;}
void check(int code){if(code){wchar_t error[1024]{};ab_last_error(error,1024);std::cerr<<"Native error "<<code<<": "<<audiobridge::narrow(error)<<'\n';throw std::runtime_error("AudioCore call failed");}}
template<class T,class F> std::vector<T> list(F fn){uint32_t n{};check(fn(nullptr,&n));for(int attempt=0;attempt<4;++attempt){std::vector<T> items(n);auto capacity=n;auto result=fn(items.data(),&capacity);n=capacity;if(result==AB_BUFFER_TOO_SMALL)continue;check(result);items.resize(n);return items;}throw std::runtime_error("Device list changed repeatedly");}
struct Handle {AB_Handle value{};~Handle(){if(value) ab_destroy(value);}};
}
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);SetConsoleCtrlHandler(console_handler,TRUE);
    std::cout<<std::unitbuf;
    try {
        std::wstring mode=argc>1?argv[1]:L"--help";
        if(mode==L"--help") {std::cout<<"AudioProbe --devices | --outputs | --abi | --silence-test [output-index]\nAudioProbe --capture seconds [device-index] [output-index]\nAudioProbe --capture-muted seconds [device-index] [output-index]\nIndexes are zero-based. Capture changes iPhone USB configuration and restores it before exit.\n";return 0;}
        if(mode==L"--abi"){for(uint32_t i=1;i<=4;++i)std::cout<<i<<":"<<ab_abi_size(i)<<"\n";return 0;}
        if(mode==L"--devices") {auto items=list<AB_Device>(ab_list_devices);for(size_t i=0;i<items.size();++i) std::cout<<i<<" "<<audiobridge::narrow(items[i].name)<<" "<<audiobridge::narrow(items[i].model)<<" iOS "<<audiobridge::narrow(items[i].os)<<" accessible="<<items[i].accessible<<"\n";std::cout<<"devices="<<items.size()<<"\n";return 0;}
        auto outputs=list<AB_OutputDevice>(ab_list_outputs);
        if(mode==L"--outputs"){for(size_t i=0;i<outputs.size();++i)std::cout<<i<<" "<<audiobridge::narrow(outputs[i].name)<<(outputs[i].is_default?" [default]":"")<<"\n";return 0;}
        if(mode==L"--silence-test" || mode==L"--pause-silence-test") {
            size_t selected=argc>2?std::stoul(argv[2]):0;if(selected>=outputs.size())throw std::runtime_error("Invalid output index");
            audiobridge::WasapiOutput output;output.start(outputs[selected].id,3072,4096,0);
            std::vector<uint8_t> pcm(480*4);
            auto next=std::chrono::steady_clock::now();
            for(int i=0;i<200 && !interrupted;++i){output.enqueue(pcm);next+=std::chrono::milliseconds(10);std::this_thread::sleep_until(next);}
            if(mode==L"--pause-silence-test") {
                for(int cycle=0;cycle<3;++cycle) {
                    AB_Status before{};output.snapshot(before);
                    output.source_paused(true);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    AB_Status paused{};output.snapshot(paused);
                    if(!paused.output_active || !paused.source_idle || paused.queued_frames || paused.underruns!=before.underruns)
                        throw std::runtime_error("WASAPI did not stay silent/active through the declared pause");
                    output.source_paused(false);
                    std::vector<uint8_t> burst(1024*4);
                    output.enqueue(burst);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    AB_Status preparing{};output.snapshot(preparing);
                    if(preparing.queued_frames!=1024 || !preparing.source_idle || preparing.rendered_frames!=paused.rendered_frames)
                        throw std::runtime_error("First resumed packet consumed before reserve was ready");
                    auto burst_at=std::chrono::steady_clock::now();
                    for(int i=0;i<100;++i){output.enqueue(burst);burst_at+=std::chrono::microseconds(21333);std::this_thread::sleep_until(burst_at);}
                    AB_Status resumed{};output.snapshot(resumed);
                    if(!resumed.output_active || resumed.source_idle || resumed.rendered_frames<=paused.rendered_frames || resumed.underruns!=paused.underruns)
                        throw std::runtime_error("WASAPI resume failed or underrun occurred");
                    std::cout<<"pause_cycle="<<cycle+1<<" active=1 resumed=1 underruns="<<resumed.underruns<<"\n";
                }
            }
            AB_Status s{};output.snapshot(s);output.stop();
            std::cout<<"active="<<s.output_active<<" rendered="<<s.rendered_frames<<" period="<<s.engine_period_frames<<" endpoint="<<s.endpoint_frames<<" underruns="<<s.underruns<<" autoconvert="<<s.autoconvert<<"\n";
            if(output.failed()) throw std::runtime_error(output.error());return s.output_active && s.rendered_frames>48000?0:1;
        }
        if((mode!=L"--capture" && mode!=L"--capture-muted") || argc<3) throw std::runtime_error("Unknown command; see --help");
        const auto seconds=std::stoi(argv[2]);if(seconds<1 || seconds>7200)throw std::runtime_error("Capture duration must be 1..7200 seconds");
        auto phones=list<AB_Device>(ab_list_devices);size_t phone=argc>3?std::stoul(argv[3]):0,out=argc>4?std::stoul(argv[4]):0;
        if(phone>=phones.size() || out>=outputs.size())throw std::runtime_error("No matching phone/output");
        AB_Config config{};config.struct_size=sizeof(config);config.version=AB_VERSION;config.volume=mode==L"--capture-muted"?0.0f:0.5f;
        wcscpy_s(config.device_id,phones[phone].id);wcscpy_s(config.output_id,outputs[out].id);
        Handle handle;check(ab_create(&handle.value));check(ab_start(handle.value,&config));
        AB_Status status{};status.struct_size=sizeof(status);status.version=AB_VERSION;
        for(int i=0;i<seconds && !interrupted;++i){std::this_thread::sleep_for(std::chrono::seconds(1));check(ab_get_status(handle.value,&status));std::cout<<"t="<<i+1<<" "<<audiobridge::narrow(status.message)<<" | audio="<<status.audio_packets<<" video="<<status.video_packets<<" queue="<<status.queued_frames<<" underruns="<<status.underruns<<" dropped="<<status.dropped_frames<<" state="<<status.state<<" output_active="<<status.output_active<<" source_idle="<<status.source_idle<<"\n";if(status.state==AB_ERROR || status.state==AB_STOPPED)break;}
        const auto stop_started=std::chrono::steady_clock::now();
        const auto stop=ab_stop(handle.value);
        const auto stop_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-stop_started).count();
        check(ab_get_status(handle.value,&status));
        std::cout<<audiobridge::narrow(status.message)<<" | final_audio="<<status.audio_packets<<" final_video="<<status.video_packets<<" final_video_bytes="<<status.video_bytes
            <<" rendered="<<status.rendered_frames<<" underruns="<<status.underruns<<" dropped="<<status.dropped_frames<<" format="<<status.sample_rate<<"/"<<status.bits<<"/"<<status.channels
            <<" input_db="<<status.input_left_db<<","<<status.input_right_db<<" output_db="<<status.output_left_db<<","<<status.output_right_db<<" period="<<status.engine_period_frames<<" endpoint="<<status.endpoint_frames<<" stop_ms="<<stop_ms<<"\n";
        return stop==0 && status.audio_packets>0 && status.video_packets==0 && status.rendered_frames>0?0:1;
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
