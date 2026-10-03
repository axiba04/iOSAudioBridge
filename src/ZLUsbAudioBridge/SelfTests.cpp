#include "SelfTests.h"
#include "Wasapi/WasapiOutput.h"
#include "Device/DeviceCatalog.h"
#include "dsp/analyzer/fft_analyzer/fft_analyzer_receiver.hpp"
#include <algorithm>
#include <cstdlib>
#include <new>
#include <numbers>
#include <vector>
#include <malloc.h>

namespace {
thread_local bool trace_allocations=false;
thread_local size_t traced_allocations=0;
void record() noexcept {if(trace_allocations)++traced_allocations;}
void require(bool result,const char* message){if(!result)throw std::runtime_error(message);}
struct Trace {Trace(){trace_allocations=true;}~Trace(){trace_allocations=false;}};

void checkFftHistoryModes() {
    using namespace zldsp::analyzer;
    FFTAnalyzerProcessor middle,large,small;
    middle.prepare(12);large.prepare(14);small.prepare(10);
    FFTAnalyzerReceiver receiver(middle);
    receiver.setON(true);
    // Reproduce Normal -> High -> Normal at 48 kHz, including a wrapped FIFO read.
    // A 750 Hz sine lands exactly on a bin for each of the three FFT sizes.
    for(const size_t history_size:{size_t(4096),size_t(16384),size_t(4096)}) {
        receiver.prepare(2,history_size);
        receiver.forward(StereoType::kStereoLR);
        for(const float power:receiver.getAbsSqrFFTBuffer())
            require(power==0.f,"FFT history retained samples after a quality switch");
        std::vector<std::vector<float>> fifo(2,std::vector<float>(history_size));
        for(size_t i=0;i<history_size;++i) {
            const auto value=.4f*static_cast<float>(std::sin(2.0*std::numbers::pi*static_cast<double>(i)/64.0));
            const auto index=(i+13)%history_size;
            fifo[0][index]=value;fifo[1][index]=value*.5f;
        }
        zldsp::container::FIFORange range{};
        range.start_index1=13;range.block_size1=static_cast<int>(history_size)-13;
        range.start_index2=0;range.block_size2=13;
        receiver.pull(range,fifo);receiver.pull(range,fifo);
        for(auto* fft:{&middle,&large,&small}) {
            if(history_size==4096 && fft!=&middle)continue;
            std::vector<float> spectrum(fft->getFFTSize()/2+1);
            receiver.forward(*fft,StereoType::kStereoLR,spectrum);
            for(const float power:spectrum)require(std::isfinite(power)&&power>=0.f,"Invalid FFT power");
            const auto peak=static_cast<size_t>(std::max_element(spectrum.begin(),spectrum.end())-spectrum.begin());
            require(peak==fft->getFFTSize()/64,"FFT quality switch changed the detected frequency");
            // Normalized Hann window: (0.4 / 2)^2 + (0.2 / 2)^2 = 0.05.
            require(std::abs(spectrum[peak]-.05f)<.0001f,"FFT quality switch changed the measured level");
        }
    }
}
}
// Diagnostic counters are disabled except around self-test processing calls.
// Covers C++ new (including aligned new), not direct malloc in third-party code.
void* operator new(std::size_t n){record();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t n,std::align_val_t alignment){record();if(auto* p=_aligned_malloc(n?n:1,static_cast<size_t>(alignment)))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n,std::align_val_t alignment){return ::operator new(n,alignment);}
void operator delete(void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {_aligned_free(p);}

juce::var runZlSelfTests(PluginProcessor& processor,ZlProcessorAdapter& adapter) {
    auto* result=new juce::DynamicObject;juce::var owned(result);
    result->setProperty("zl_version",ZL_PLUGIN_CURRENT_VERSION);
    result->setProperty("zl_commit",ZL_PLUGIN_CURRENT_HASH);
    result->setProperty("juce_version",juce::SystemStats::getJUCEVersion());
    checkFftHistoryModes();
    result->setProperty("fft_history_normal_high_normal",true);
    std::array<float,4097> left{},right{};
    auto process=[&](size_t count){Trace trace;return adapter.process(left.data(),right.data(),count);};
    ZlProcessorAdapter::set(processor,"comp_on",0);adapter.prepare(48000,4096);
    traced_allocations=0;
    for(const size_t n:{size_t(1),size_t(127),size_t(480),size_t(1024),size_t(4096)}) {
        left.fill(0.125f);right.fill(-0.25f);left[n]=42;right[n]=-42;
        require(process(n),"ZL failed a valid block size");
        for(size_t i=0;i<n;++i)require(std::abs(left[i]-.125f)<1e-6f&&std::abs(right[i]+.25f)<1e-6f,"ZL bypass changed channel order or samples");
        require(left[n]==42&&right[n]==-42,"ZL wrote beyond block boundary");
    }
    require(!process(4097),"Oversized ZL block was accepted");
    require(process(0),"Zero-length processing failed");
    result->setProperty("bypass_and_block_boundaries",true);
    ZlProcessorAdapter::gamePreset(processor);ZlProcessorAdapter::set(processor,"comp_on",1);
    ZlProcessorAdapter::set(processor,"attack",0);adapter.prepare(48000,4096);
    for(int block=0;block<100;++block){left.fill(0.5f);right.fill(0.25f);require(process(480),"Compression processing failed");}
    const auto output_db=20.0f*std::log10(std::abs(left[479]));
    const auto expected_db=-18.f+(20.f*std::log10(0.5f)+18.f)/4.f;
    require(std::abs(output_db-expected_db)<.3f,"Compression ratio/threshold steady state differs from expected curve");
    require(std::abs(right[479]/left[479]-.5f)<1e-5f,"Fully linked compression changed stereo balance");
    result->setProperty("compressed_db",output_db);result->setProperty("expected_db",expected_db);result->setProperty("stereo_link",true);
    auto constant=[&](float amplitude,int blocks){
        for(int i=0;i<blocks;++i){left.fill(amplitude);right.fill(amplitude*.5f);require(process(480),"Envelope processing failed");}
        return 20.f*std::log10(std::abs(left[479])/amplitude);
    };
    auto attackGain=[&](float attack){
        ZlProcessorAdapter::gamePreset(processor);ZlProcessorAdapter::set(processor,"attack",attack);
        adapter.prepare(48000,4096);constant(.02f,200);return constant(.5f,1);
    };
    const auto fast_attack=attackGain(1),slow_attack=attackGain(100);
    require(fast_attack<slow_attack-1,"Attack parameter did not change the step response");
    auto releaseGain=[&](float release){
        ZlProcessorAdapter::gamePreset(processor);ZlProcessorAdapter::set(processor,"attack",1);
        ZlProcessorAdapter::set(processor,"release",release);adapter.prepare(48000,4096);
        constant(.5f,200);return constant(.02f,10);
    };
    const auto fast_release=releaseGain(20),slow_release=releaseGain(500);
    require(fast_release>slow_release+1,"Release parameter did not change the recovery response");
    ZlProcessorAdapter::gamePreset(processor);ZlProcessorAdapter::set(processor,"attack",0);adapter.prepare(48000,4096);
    const auto makeup0=constant(.5f,200);ZlProcessorAdapter::set(processor,"makeup",6);const auto makeup6=constant(.5f,200);
    require(std::abs(makeup6-makeup0-6)<.15f,"Makeup gain differs from requested 6 dB");
    ZlProcessorAdapter::set(processor,"makeup",0);ZlProcessorAdapter::set(processor,"knee_width",0);
    const auto at_threshold=std::pow(10.f,-18.f/20.f);const auto hard_knee=constant(at_threshold,200);
    ZlProcessorAdapter::set(processor,"knee_width",12);const auto soft_knee=constant(at_threshold,200);
    require(std::abs(hard_knee)<.1f&&soft_knee<hard_knee-.1f&&soft_knee>-3,"Knee parameter did not soften the threshold region");
    result->setProperty("attack_gain_db_fast",fast_attack);result->setProperty("attack_gain_db_slow",slow_attack);
    result->setProperty("release_gain_db_fast",fast_release);result->setProperty("release_gain_db_slow",slow_release);
    result->setProperty("makeup_delta_db",makeup6-makeup0);result->setProperty("soft_knee_gain_db",soft_knee);
    const std::pair<const char*,std::pair<float,float>> parameters[]{
        {"threshold",{-24,-12}},{"ratio",{2,6}},{"attack",{1,10}},{"release",{50,200}},{"knee_width",{2,12}},{"makeup",{-3,3}}};
    for(int iteration=0;iteration<100;++iteration)for(const auto& [id,values]:parameters){
        ZlProcessorAdapter::set(processor,id,iteration%2?values.first:values.second);
        left.fill(.2f);right.fill(-.1f);require(process(127),"Live parameter update failed");
        for(size_t i=0;i<127;++i)require(std::isfinite(left[i])&&std::isfinite(right[i]),"Nonfinite DSP output");
    }
    result->setProperty("live_parameter_changes",600);result->setProperty("cpp_allocations_during_processing",static_cast<juce::int64>(traced_allocations));
    require(traced_allocations==0,"C++ allocation occurred in tested processing callbacks");
    ZlProcessorAdapter::set(processor,"oversample",2);ZlProcessorAdapter::set(processor,"lookahead",10);ZlProcessorAdapter::set(processor,"external_side",1);
    juce::MemoryBlock state;processor.getStateInformation(state);
    processor.restoreBridgeState(state.getData(),static_cast<int>(state.getSize()));
    for(const auto* id:{"oversample","lookahead","external_side"})require(processor.parameters_.getRawParameterValue(id)->load()==0,"Preset bypassed low-latency constraints");
    bool deferred=false;
    processor.bridgeStateRequested=[&](juce::MemoryBlock){deferred=true;};
    processor.setStateInformation(state.getData(),static_cast<int>(state.getSize()));processor.bridgeStateRequested={};
    require(deferred,"Full state loading did not use the bridge coordinator");
    result->setProperty("preset_constraints",true);result->setProperty("state_load_deferred",true);
    ZlProcessorAdapter::gamePreset(processor);adapter.prepare(48000,4096);
    require(processor.getLatencySamples()==0,"Bridge DSP introduced latency");
    result->setProperty("dsp_latency_samples",processor.getLatencySamples());
    result->setProperty("passed",true);result->setProperty("allocation_coverage","C++ new only; direct malloc not instrumented");
    return owned;
}

juce::var runZlWasapiTest(ZlProcessorAdapter& adapter) {
    adapter.prepare(48000,4096);
    audiobridge::WasapiOutput output;output.start(L"",3072,4096,0,&adapter);
    std::vector<audiobridge::StereoFrame> samples(1024,{8000,-4000});
    auto feed=[&](int blocks){
        auto at=std::chrono::steady_clock::now();
        for(int i=0;i<blocks;++i){output.enqueue(std::span(reinterpret_cast<const uint8_t*>(samples.data()),samples.size()*4));at+=std::chrono::microseconds(21333);std::this_thread::sleep_until(at);}
    };
    feed(100);
    for(int cycle=0;cycle<3;++cycle) {
        AB_Status before{};output.snapshot(before);output.source_paused(true);std::this_thread::sleep_for(std::chrono::milliseconds(500));
        AB_Status paused{};output.snapshot(paused);require(paused.output_active&&paused.source_idle&&!paused.queued_frames,"ZL WASAPI did not preserve paused output");
        output.source_paused(false);output.enqueue(std::span(reinterpret_cast<const uint8_t*>(samples.data()),samples.size()*4));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        AB_Status buffered{};output.snapshot(buffered);require(buffered.queued_frames==1024,"First resumed packet consumed before startup reserve");
        feed(100);AB_Status after{};output.snapshot(after);
        require(after.output_active&&!after.source_idle&&after.underruns==before.underruns,"ZL WASAPI resume underrun");
    }
    AB_Status status{};output.snapshot(status);const auto telemetry=output.telemetry();output.stop();
    require(!output.failed()&&status.output_active&&status.rendered_frames>48000,"ZL WASAPI output failed");
    require(status.output_left_db<=-120&&status.output_right_db<=-120,"ZL WASAPI mute failed");
    auto* result=new juce::DynamicObject;
    result->setProperty("passed",true);result->setProperty("pause_cycles",3);result->setProperty("muted",true);
    result->setProperty("rendered_frames",static_cast<juce::int64>(status.rendered_frames));result->setProperty("underruns",static_cast<juce::int64>(status.underruns));
    result->setProperty("missing_frames",static_cast<juce::int64>(status.missing_frames));result->setProperty("max_render_us",static_cast<juce::int64>(telemetry.max_render_us));
    result->setProperty("period_frames",static_cast<int>(status.engine_period_frames));return juce::var(result);
}
