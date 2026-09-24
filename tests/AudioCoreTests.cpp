#include "AudioCore.h"
#include "Protocol/QuickTimeSession.h"
#include "Pcm/PcmFormat.h"
#include "Buffer/SpscPcmRing.h"
#include "Buffer/ResumeBufferGate.h"
#include "Capture/UsbConfigurationRestorePolicy.h"
#include "Capture/AudioStopDrainPolicy.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <random>
#include <cstring>

namespace {
using namespace iPhoneMirror::quicktime;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> void rejects(F&& fn){bool threw=false;try{fn();}catch(...){threw=true;}require(threw,"Expected rejection");}
std::vector<uint8_t> fixture(const char* name){std::ifstream f(std::string("fixtures/quicktime_video_hack/")+name,std::ios::binary);require(bool(f),"Fixture missing");return {std::istreambuf_iterator<char>(f),{}};}
Packet framed(std::span<const uint8_t> b){StreamDecoder d;auto p=d.push(b);require(p.size()==1,"One frame required");return p.front();}
void protocol_tests(){
    auto bytes=fixture("afmt-request");
    for(size_t cut=0;cut<=bytes.size();++cut){StreamDecoder d;auto a=d.push(std::span(bytes).first(cut));auto b=d.push(std::span(bytes).subspan(cut));require(a.size()+b.size()==1,"Split packet lost");}
    StreamDecoder decoder;auto twice=bytes;twice.insert(twice.end(),bytes.begin(),bytes.end());require(decoder.push(twice).size()==2,"Coalesced packets lost");
    rejects([&]{(void)decoder.push(std::vector<uint8_t>{255,255,255,127});});
    rejects([]{SessionProtocol forbidden({.request_audio=true,.request_video=true});});
    SessionProtocol protocol;
    require(protocol.process(framed(make_ping())).outbound.size()==1,"PING");
    auto clock=protocol.process(framed(fixture("cwpa-request1")));
    require(clock.outbound.size()==2 && framed(clock.outbound[0]).kind==PacketKind::Reply && framed(clock.outbound[1]).subtype==fourcc('h','p','a','1'),"Audio-only CWPA must omit HPD1");
    auto afmt=protocol.process(framed(bytes));require(afmt.outbound.size()==1 && afmt.outbound[0]==fixture("afmt-reply"),"AFMT reply changed");
    require(protocol.negotiated_audio().has_value(),"AFMT missing");audiobridge::validate_format(*protocol.negotiated_audio());
    auto eat=parse_payload(fixture("asyn-eat"));auto event=protocol.process(eat);
    require(event.audio_sample.has_value() && event.state==SessionState::Streaming,"Audio must enter Streaming");
    require(event.audio_sample->sample_count==1024 && event.audio_sample->sample_data.size()==4096,"PCM fixture shape");
    audiobridge::validate_sample(*event.audio_sample);
    // Remove fdsc from the actual fixture, adjust sbuf length, and prove negotiated-format fallback.
    auto raw=eat.payload;
    auto u32=[&](size_t p){uint32_t x;std::memcpy(&x,raw.data()+p,4);return x;};
    size_t offset=24;
    while(offset<raw.size()){auto length=u32(offset);if(u32(offset+4)==fourcc('f','d','s','c')){raw.erase(raw.begin()+offset,raw.begin()+offset+length);auto size=static_cast<uint32_t>(raw.size()-16);std::memcpy(raw.data()+16,&size,4);break;}offset+=length;}
    SessionProtocol fallback;(void)fallback.process(framed(bytes));auto missing_format=fallback.process(parse_payload(raw));
    require(missing_format.audio_sample.has_value() && fallback.audio_format().has_value(),"AFMT fallback failed");
    auto stop=protocol.stop_messages();require(stop.size()==1 && framed(stop[0]).subtype==fourcc('h','p','a','0'),"Audio-only stop");
    require(protocol.process(eat).state==SessionState::Stopping,"Late audio undid stop");require(protocol.complete_stop_messages().empty(),"Unexpected HPD0");
    require(protocol.process(eat).state==SessionState::Stopped,"Late audio restarted session");
    require(protocol.begin_display_reconfigure(1,1).empty(),"Display reconfiguration enabled");
    protocol.reset();auto video_clock=protocol.process(framed(fixture("cvrp-request")));require(video_clock.outbound.size()==1 && framed(video_clock.outbound[0]).kind==PacketKind::Reply,"Unexpected NEED");
    auto feed=protocol.process(framed(fixture("asyn-feed")));require(!feed.video_sample && protocol.video_frames()==1 && protocol.video_bytes()>0,"Unrequested video detection");
    auto format=*fallback.negotiated_audio();format.sample_rate=44100;rejects([&]{audiobridge::validate_format(format);});
    auto bad=*event.audio_sample;bad.sample_data.pop_back();rejects([&]{audiobridge::validate_sample(bad);});
    Packet invalid_stop;invalid_stop.kind=PacketKind::Sync;invalid_stop.subtype=fourcc('s','t','o','p');invalid_stop.payload.resize(16);
    require(protocol.process(invalid_stop).state==SessionState::Error,"Malformed STOP must not authorize early drain completion");
}
void pause_resume_tests(){
    auto raw=framed(fixture("cwpa-request1")).payload;
    const auto subtype=fourcc('s','t','o','p');std::memcpy(raw.data()+12,&subtype,4);
    const auto stop=parse_payload(raw);
    const auto eat=parse_payload(fixture("asyn-eat"));
    const auto cwpa=framed(fixture("cwpa-request1"));
    SessionProtocol p;
    (void)p.process(cwpa);(void)p.process(framed(fixture("afmt-request")));
    require(p.process(eat).state==SessionState::Streaming,"Initial audio stream");
    auto paused=p.process(stop);
    require(paused.state==SessionState::Paused && paused.outbound.size()==1 && framed(paused.outbound[0]).kind==PacketKind::Reply,"Peer STOP must be acknowledged without host teardown");
    require(p.process(stop).state==SessionState::Paused,"Repeated peer STOP stays paused");
    require(p.process(eat).state==SessionState::Streaming && p.audio_packets()==2,"Audio can resume without a new USB handshake");
    (void)p.process(stop);
    auto renegotiated=p.process(cwpa);
    require(renegotiated.state==SessionState::Negotiating && renegotiated.outbound.size()==2 && framed(renegotiated.outbound[1]).subtype==fourcc('h','p','a','1'),"Resume clock may request audio only");
    require(p.process(eat).state==SessionState::Streaming,"Audio can resume after clock renegotiation");
    (void)p.process(stop);(void)p.stop_messages();
    require(p.process(stop).state==SessionState::Stopped,"Host stop still tears down after a pause");
    require(p.process(eat).state==SessionState::Stopped,"Late PCM must not undo host stop");
    auto late_clock=p.process(cwpa);
    require(late_clock.state==SessionState::Stopped && late_clock.outbound.size()==1,"Late CWPA must not request new audio during teardown");
    require(p.process(framed(make_ping())).state==SessionState::Stopped,"Late PING must not undo host stop");
    p.reset();require(p.process(stop).state==SessionState::Paused,"Reset clears host stop intent");
}
void ring_tests(){
    audiobridge::ResumeBufferGate gate;
    require(!gate.hold(false,0,3072),"Unpaused starvation must still count as underrun");
    require(!gate.hold(true,1024,3072) && gate.waiting(),"Drain existing tail while marking a source pause");
    require(gate.hold(false,1024,3072) && gate.hold(false,3071,3072),"Hold resumed data until startup reserve is ready");
    require(!gate.hold(false,3072,3072) && !gate.waiting(),"Resume at the startup target");
    require(!gate.hold(false,10,3072),"Do not conceal subsequent runtime underruns");
    (void)gate.hold(true,0,3072);(void)gate.hold(false,1024,3072);
    require(!gate.hold(true,1024,3072) && gate.hold(false,2048,3072),"Another pause while rebuffering keeps the reserve requirement");
    audiobridge::SpscPcmRing ring(7);
    std::vector<audiobridge::StereoFrame> input(10);for(int i=0;i<10;++i)input[i]={int16_t(i),int16_t(-i)};
    auto pcm=[](const auto& data){return std::span(reinterpret_cast<const uint8_t*>(data.data()),data.size()*4);};
    require(ring.write(pcm(input))==7,"Ring overflow");
    std::vector<audiobridge::StereoFrame> output(5);require(ring.read(output.data(),5)==5 && output[4].left==4,"Ring order");
    require(ring.write(pcm(input))==5,"Ring wrap write");require(ring.trim_to(3)==4,"Consumer trim");
    require(ring.read(output.data(),5)==3 && output[0].left==2 && output[2].left==4,"Trim/wrap order");
    ring.reset();require(!ring.available(),"Reset");
    audiobridge::SpscPcmRing concurrent(257);constexpr uint32_t total=500000;
    std::atomic<bool> good{true};
    std::jthread producer([&]{for(uint32_t n=0;n<total;){audiobridge::StereoFrame f{static_cast<int16_t>(n&0x7fff),static_cast<int16_t>(-(int(n&0x7fff)))};if(concurrent.write({reinterpret_cast<const uint8_t*>(&f),4}))++n;else std::this_thread::yield();}});
    for(uint32_t n=0;n<total;){audiobridge::StereoFrame f;if(concurrent.read(&f,1)){if(f.left!=int16_t(n&0x7fff) || f.right!=-int(n&0x7fff))good=false;++n;}else std::this_thread::yield();}
    producer.join();require(good && !concurrent.available(),"Concurrent PCM corruption");
}
void abi_tests(){
    require(ab_abi_size(1)==sizeof(AB_Device) && ab_abi_size(4)==sizeof(AB_Status),"ABI sizes");
    require(ab_create(nullptr)==AB_INVALID_ARGUMENT,"Null handle output");
    AB_Handle h{};require(ab_create(&h)==0,"Create");
    AB_Status s{};require(ab_get_status(h,&s)==AB_INVALID_ARGUMENT,"Version guard");
    s.struct_size=sizeof(s);s.version=AB_VERSION;require(ab_get_status(h,&s)==0 && s.state==AB_STOPPED,"Initial state");
    require(ab_set_volume(h,-1)==AB_INVALID_ARGUMENT,"Volume validation");
    wchar_t error[1024]{};require(ab_last_error(error,1024)==0 && wcsstr(error,L"Volume"),"Thread-local native error");
    wchar_t short_error[4]{};require(ab_last_error(short_error,4)==AB_BUFFER_TOO_SMALL && short_error[3]==0,"Error truncation/termination");
    AB_Config c{};require(ab_start(h,&c)==AB_INVALID_ARGUMENT,"Config version");
    require(ab_stop(h)==0,"Idle stop");ab_destroy(h);require(ab_get_status(h,&s)==AB_INVALID_ARGUMENT,"Stale handle");ab_destroy(h);
}
void restore_tests(){
    using namespace iPhoneMirror::capture::detail;
    using O=UsbConfigurationObservation;using A=UsbConfigurationRestoreAction;
    UsbConfigurationRestorePolicy policy;
    require(policy.observe(O::Missing)==A::Wait,"Missing device must not trigger restore");
    require(policy.observe(O::QuickTime)==A::DisableQuickTime,"Verified QuickTime needs restore");
    require(policy.observe(O::QuickTime)==A::Wait,"Do not repeat disconnect control");
    require(policy.observe(O::Normal)==A::Wait,"Old normal row is not restoration evidence");
    require(policy.observe(O::Missing)==A::Wait && policy.observe(O::QuickTime)==A::Wait,"Never issue a second restore");
    require(policy.observe(O::Normal)==A::Complete,"Removal and return completes restore");
    require(policy.observe(O::Missing)==A::Complete,"Completed restore stays complete");
    UsbConfigurationRestorePolicy stable(true);
    require(stable.observe(O::Normal)==A::Wait && stable.observe(O::VerifiedNormal)==A::Complete,"Stable PnP confirmation");
    require(!is_libusb0_quicktime_pnp_state(false,false,false,true),"Absent parent must not be opened during transition");
    require(stabilize_normal_configuration_observation(true,1)==O::Missing && stabilize_normal_configuration_observation(true,2)==O::VerifiedNormal,"Require repeated normal observations");
}
void stop_drain_tests(){
    using P=audiobridge::AudioStopDrainPolicy;
    using namespace std::chrono_literals;
    const auto start=P::Clock::time_point{};
    P drain(start,false,false);
    require(!drain.finished(start+5s,false,true),"Quiet without peer STOP must keep the legacy deadline");
    drain.peer_stop(start+10ms);
    require(!drain.finished(start+259ms,false,true),"Do not close immediately after peer STOP");
    require(drain.finished(start+260ms,false,true),"Peer STOP plus quiet stream may finish early");
    require(!drain.finished(start+300ms,false,false),"Do not early-close with a partial packet");
    drain.media(start+280ms);
    require(!drain.finished(start+529ms,false,true) && drain.finished(start+530ms,false,true),"Late media resets quiet interval");
    P legacy(start,false,true);legacy.peer_stop(start+10ms);
    require(!legacy.finished(start+5999ms,false,true) && legacy.finished(start+6s,false,true),"Conservative fallback keeps six seconds");
    require(legacy.finished(start+20ms,true,false),"Complete release replies end either mode");
    P absent_stop(start,false,false);
    require(absent_stop.finished(start+6s,false,false),"No STOP/malformed tail remains bounded");
    P stopped(start,true,false);P stopped_legacy(start,true,true);
    require(stopped.finished(start+250ms,false,true) && !stopped_legacy.finished(start+999ms,false,true) && stopped_legacy.finished(start+1s,false,true),"Already-stopped peer timing");
}
}
int main(){try{protocol_tests();pause_resume_tests();ring_tests();abi_tests();restore_tests();stop_drain_tests();std::cout<<"PASS: framing, audio-only negotiation/teardown, AFMT fallback, PCM validation, SPSC boundaries and 500000-frame concurrency, ABI lifetime/errors, one-shot USB restoration policy, STOP/quiet/late-media/partial-packet/deadline behavior, peer pause/resume vs host teardown\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
