#include "ZlProcessorAdapter.h"
#include "SelfTests.h"
#include "PluginEditor.hpp"
#include "Engine/AudioEngine.h"
#include "Device/DeviceCatalog.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <future>
#include <fstream>
#include <filesystem>
#include <chrono>

namespace {
using namespace audiobridge;
juce::File settingsFolder(){return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("iOSAudioBridge-ZL");}
void writeJson(const juce::File& file,juce::DynamicObject* object){
    if(!file.getParentDirectory().createDirectory().wasOk())throw std::runtime_error("Cannot create report directory");
    if(!file.replaceWithText(juce::JSON::toString(juce::var(object),true)))throw std::runtime_error("Cannot save report");
}
class BridgePanel final : public juce::Component,private juce::Timer {
public:
    BridgePanel(PluginProcessor& processor,ZlProcessorAdapter& adapter,AudioEngine& engine,
                juce::String arguments,std::function<void(int)> finished)
        : processor_(processor),adapter_(adapter),engine_(engine),arguments_(arguments),finished_(std::move(finished)) {
        editor_.reset(processor_.createEditorAndMakeActive());
        editor_->setResizable(false,false);
        addAndMakeVisible(*editor_);
        for(auto* c : std::array<juce::Component*,9>{&phones_,&outputs_,&refresh_,&start_,&stop_,&preset_,&mute_,&advanced_,&status_})addAndMakeVisible(c);
        for(auto* c : std::array<juce::Component*,3>{&volume_,&volume_label_,&volume_hint_})addChildComponent(c);
        for(auto* box:{&phones_,&outputs_}) {
            box->setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xfff1f5f9));
            box->setColour(juce::ComboBox::textColourId,juce::Colour(0xff162033));
        }
        for(auto* button:{&refresh_,&start_,&stop_,&preset_,&advanced_}) {
            button->setColour(juce::TextButton::buttonColourId,juce::Colour(0xffdbeafe));
            button->setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffbfdbfe));
            button->setColour(juce::TextButton::textColourOffId,juce::Colour(0xff162033));
            button->setColour(juce::TextButton::textColourOnId,juce::Colour(0xff162033));
        }
        phones_.setTextWhenNothingSelected(juce::String::fromUTF8("选择 USB iPhone"));
        outputs_.setTextWhenNothingSelected(juce::String::fromUTF8("选择输出设备"));
        volume_.setRange(0,100,1);volume_.setValue(50,juce::dontSendNotification);
        volume_.setSliderStyle(juce::Slider::LinearHorizontal);volume_.setTextBoxStyle(juce::Slider::TextBoxRight,false,50,24);
        volume_.setTextValueSuffix(" %");volume_.setName(juce::String::fromUTF8("监听音量"));
        volume_label_.setText(juce::String::fromUTF8("监听音量"),juce::dontSendNotification);
        volume_hint_.setText(juce::String::fromUTF8("仅调节本软件输出，不改变 Windows 音量"),juce::dontSendNotification);
        for(auto* label:{&volume_label_,&volume_hint_}) {
            label->setColour(juce::Label::textColourId,juce::Colour(0xffe2e8f0));
            label->setFont(juce::FontOptions(14.0f));
        }
        advanced_.setClickingTogglesState(true);
        advanced_.onClick=[this]{
            const auto open=advanced_.getToggleState();
            advanced_.setButtonText(juce::String::fromUTF8(open?"收起高级设置":"高级设置"));
            volume_.setVisible(open);volume_label_.setVisible(open);volume_hint_.setVisible(open);
            resized();repaint();
        };
        mute_.setToggleState(true,juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId,juce::Colour(0xffe2e8f0));
        status_.setJustificationType(juce::Justification::centredLeft);
        status_.setFont(juce::FontOptions(14.0f));
        refresh_.onClick=[this]{refreshDevices();};start_.onClick=[this]{startCapture();};stop_.onClick=[this]{stopCapture();};
        preset_.onClick=[this]{pending_preset_=true;restart_after_=isRunning();stopCapture();};
        mute_.onClick=[this]{engine_.volume(gain());};volume_.onValueChange=[this]{engine_.volume(gain());};
        processor_.bridgeStateRequested=[this](juce::MemoryBlock state){
            if(closing_ || busy())return;
            pending_state_=std::move(state);restart_after_=isRunning();stopCapture();
        };
        const auto settings=settingsFolder().getChildFile("devices.json");
        if(settings.existsAsFile())saved_output_=juce::JSON::parse(settings)["output"].toString();
        auto tokens=juce::StringArray::fromTokens(arguments_,true);
        const auto capture=tokens.indexOf("--capture-muted");
        if(capture>=0) {
            capture_seconds_=capture+1<tokens.size()?tokens[capture+1].getIntValue():30;
            capture_seconds_=juce::jlimit(1,7200,capture_seconds_);test_=true;
        }
        smoke_=tokens.contains("--ui-smoke");test_=test_||smoke_;
        const auto result=tokens.indexOf("--result");
        report_=result>=0 && result+1<tokens.size()?juce::File::getCurrentWorkingDirectory().getChildFile(tokens[result+1]):
            juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("validation.json");
        if(capture_seconds_) {
            report_.getParentDirectory().createDirectory();
            samples_.open(std::filesystem::path(report_.withFileExtension("jsonl").getFullPathName().toWideCharPointer()));
        }
        setSize(std::max(1100,editor_->getWidth()),std::max(800,editor_->getHeight()+172));
        started_=std::chrono::steady_clock::now();refreshDevices();startTimer(100);
    }
    ~BridgePanel() override {
        stopTimer();processor_.bridgeStateRequested={};
        if(task_.valid())task_.wait();
        engine_.stop();editor_.reset();
        if(!test_)try{auto* o=new juce::DynamicObject;o->setProperty("output",saved_output_);writeJson(settingsFolder().getChildFile("devices.json"),o);}catch(...){}
    }
    bool testMode() const noexcept{return test_;}
    void close() {closing_=true;if(!busy())stopCapture();}
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colour(0xff172033));}
    void resized() override {
        auto bounds=getLocalBounds();auto top=bounds.removeFromTop(82).reduced(10,6);
        auto row=top.removeFromTop(32);refresh_.setBounds(row.removeFromRight(90));
        phones_.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(0,1));outputs_.setBounds(row.reduced(8,1));
        top.removeFromTop(6);start_.setBounds(top.removeFromLeft(100));top.removeFromLeft(8);stop_.setBounds(top.removeFromLeft(100));
        top.removeFromLeft(12);preset_.setBounds(top.removeFromLeft(140));top.removeFromLeft(16);mute_.setBounds(top.removeFromLeft(120));
        advanced_.setBounds(top.removeFromLeft(120));
        if(advanced_.getToggleState()) {
            auto settings=bounds.removeFromTop(44).reduced(10,6);
            volume_label_.setBounds(settings.removeFromLeft(90));volume_.setBounds(settings.removeFromLeft(260));
            settings.removeFromLeft(16);volume_hint_.setBounds(settings);
        }
        status_.setBounds(bounds.removeFromBottom(90).reduced(10,4));
        // Small desktops can constrain the native window below its requested size.
        // Preserve the editor's minimum aspect ratio instead of clipping its controls.
        const auto scale=std::min(1.f,static_cast<float>(bounds.getHeight())/(bounds.getWidth()*.47f));
        editor_->setBounds(0,0,juce::roundToInt(bounds.getWidth()/scale),juce::roundToInt(bounds.getHeight()/scale));
        editor_->setTransform(juce::AffineTransform::scale(scale).translated(static_cast<float>(bounds.getX()),static_cast<float>(bounds.getY())));
    }
private:
    bool busy() const{return task_.valid();}
    bool isRunning() const {const auto s=engine_.status().state;return s==AB_CONNECTING||s==AB_READY||s==AB_STREAMING||s==AB_STOPPING;}
    float gain() const{return (test_||mute_.getToggleState())?0.f:static_cast<float>(volume_.getValue()/100.0);}
    void refreshDevices() {
        if(busy()||isRunning())return;
        pending_=Pending::refresh;
        task_=std::async(std::launch::async,[this]{new_phones_=list_phones();new_outputs_=list_outputs();});
    }
    void startCapture() {
        if(busy()||closing_||isRunning())return;
        const auto phone=phones_.getSelectedItemIndex(),output=outputs_.getSelectedItemIndex();
        if(phone<0 || output<0){error_=juce::String::fromUTF8("请选择已连接的手机和输出设备");return;}
        AB_Config c{};c.struct_size=sizeof(c);c.version=AB_VERSION;c.volume=gain();
        copy_text(c.device_id,phone_items_[static_cast<size_t>(phone)].id);copy_text(c.output_id,output_items_[static_cast<size_t>(output)].id);
        saved_output_=juce::String(c.output_id);error_.clear();editor_->setEnabled(false);
        try{engine_.start(c);capture_started_=std::chrono::steady_clock::now();capture_begun_=true;}
        catch(const std::exception& e){error_=e.what();}
    }
    void stopCapture() {
        if(busy())return;
        editor_->setEnabled(false);pending_=Pending::stop;
        task_=std::async(std::launch::async,[this]{engine_.stop();});
    }
    void finishReport() {
        const auto s=engine_.status();const auto t=engine_.telemetry();
        auto* o=new juce::DynamicObject;
        o->setProperty("state",static_cast<int>(s.state));o->setProperty("message",juce::String(s.message));
        o->setProperty("audio_packets",static_cast<juce::int64>(s.audio_packets));o->setProperty("video_packets",static_cast<juce::int64>(s.video_packets));
        o->setProperty("video_bytes",static_cast<juce::int64>(s.video_bytes));o->setProperty("rendered_frames",static_cast<juce::int64>(s.rendered_frames));
        o->setProperty("underruns",static_cast<juce::int64>(s.underruns));o->setProperty("missing_frames",static_cast<juce::int64>(s.missing_frames));
        o->setProperty("dropped_frames",static_cast<juce::int64>(s.dropped_frames));o->setProperty("max_render_us",static_cast<juce::int64>(t.max_render_us));
        o->setProperty("max_packet_gap_us",static_cast<juce::int64>(t.max_packet_gap_us));o->setProperty("received_frames",static_cast<juce::int64>(t.received_frames));
        o->setProperty("requested_frames",static_cast<juce::int64>(t.requested_frames));o->setProperty("first_underrun_ms",static_cast<juce::int64>(t.first_underrun_ms));
        o->setProperty("last_underrun_ms",static_cast<juce::int64>(t.last_underrun_ms));o->setProperty("clipped_samples",static_cast<juce::int64>(t.clipped_samples));
        o->setProperty("nonfinite_samples",static_cast<juce::int64>(t.nonfinite_samples));o->setProperty("dsp_latency_samples",processor_.getLatencySamples());
        o->setProperty("output_db_left",s.output_left_db);o->setProperty("output_db_right",s.output_right_db);
        o->setProperty("phones",phones_.getNumItems());o->setProperty("outputs",outputs_.getNumItems());
        o->setProperty("editor_created",editor_!=nullptr);o->setProperty("muted",test_||mute_.getToggleState());
        o->setProperty("synthetic_ui_blocks",synthetic_blocks_);
        writeJson(report_,o);
    }
    void timerCallback() override {
        try {
            if(busy() && task_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                auto completed=pending_;pending_=Pending::none;
                try{task_.get();}catch(const std::exception& e){error_=e.what();closing_=test_;}
                if(completed==Pending::refresh) {
                    phone_items_=std::move(new_phones_);output_items_=std::move(new_outputs_);phones_.clear();outputs_.clear();
                    for(size_t i=0;i<phone_items_.size();++i)phones_.addItem(juce::String(phone_items_[i].name),static_cast<int>(i+1));
                    int selected=1;
                    for(size_t i=0;i<output_items_.size();++i){outputs_.addItem(juce::String(output_items_[i].name),static_cast<int>(i+1));if(output_items_[i].is_default)selected=static_cast<int>(i+1);}
                    for(size_t i=0;i<output_items_.size();++i)if(saved_output_==juce::String(output_items_[i].id))selected=static_cast<int>(i+1);
                    phones_.setSelectedId(1);outputs_.setSelectedId(selected);
                    if(capture_seconds_&&!closing_){startCapture();if(!capture_begun_)closing_=true;}
                    if(closing_)stopCapture();
                } else if(completed==Pending::stop) {
                    if(closing_){
                        if(test_)finishReport();
                        const auto s=engine_.status();
                        const auto result=(!error_.isEmpty()||s.state==AB_ERROR||(capture_seconds_ && (!s.audio_packets||s.video_packets)))?1:0;
                        finished_(result);return;
                    }
                    if(pending_state_.getSize()) {
                        processor_.restoreBridgeState(pending_state_.getData(),static_cast<int>(pending_state_.getSize()));pending_state_.reset();
                    }
                    if(pending_preset_){ZlProcessorAdapter::gamePreset(processor_);pending_preset_=false;}
                    if(restart_after_){restart_after_=false;startCapture();}
                }
            }
            const auto s=engine_.status();const auto t=engine_.telemetry();const auto running=isRunning();
            phones_.setEnabled(!busy()&&!running);outputs_.setEnabled(!busy()&&!running);refresh_.setEnabled(!busy()&&!running);
            start_.setEnabled(!busy()&&!running&&!closing_);stop_.setEnabled(!busy()&&running&&!closing_);preset_.setEnabled(!busy()&&!closing_);
            editor_->setEnabled(!busy()&&s.state!=AB_CONNECTING&&s.state!=AB_STOPPING&&!closing_);
            const auto message=error_.isEmpty()?juce::String(s.message):error_;
            status_.setText(message+"\n"+juce::String::fromUTF8("输入 ")+juce::String(s.input_left_db,1)+" / "+juce::String(s.input_right_db,1)+" dB   "+
                juce::String::fromUTF8("设备输出 ")+juce::String(s.output_left_db,1)+" / "+juce::String(s.output_right_db,1)+" dB   "+
                juce::String::fromUTF8("缓冲 ")+juce::String(s.queued_frames)+" / "+juce::String(s.capacity_frames)+"\n"+
                juce::String::fromUTF8("欠载 ")+juce::String(static_cast<juce::int64>(s.underruns))+juce::String::fromUTF8("  缺失帧 ")+juce::String(static_cast<juce::int64>(s.missing_frames))+
                juce::String::fromUTF8("  软件延迟估计 ")+juce::String(s.host_latency_ms+processor_.getLatencySamples()/48.0f,1)+" ms   DSP "+juce::String(processor_.getLatencySamples())+
                juce::String::fromUTF8(" 帧 | 低延迟：Lookahead 0 / Oversampling Off"),juce::dontSendNotification);
            const auto now=std::chrono::steady_clock::now();
            if(smoke_&&!busy()&&!closing_) {
                static_cast<PluginEditor*>(editor_.get())->bridgeOffscreenRefresh();
                processor_.getCompressController().setMagAnalyzerOn(true);
                for(int block=0;block<5;++block) {
                    for(size_t i=0;i<ui_left_.size();++i){const auto x=.4f*std::sin(static_cast<float>(synthetic_blocks_*1024+static_cast<int>(i))*.04f);ui_left_[i]=x;ui_right_[i]=x*.5f;}
                    if(!adapter_.process(ui_left_.data(),ui_right_.data(),ui_left_.size()))throw std::runtime_error("UI synthetic processing failed");
                    ++synthetic_blocks_;
                }
                // Offscreen paint wakes the original analyzer worker without showing a window.
                (void)editor_->createComponentSnapshot(editor_->getLocalBounds());
            }
            if(capture_begun_ && samples_.is_open()) {
                const auto second=std::chrono::duration_cast<std::chrono::seconds>(now-capture_started_).count();
                if(second!=last_second_){last_second_=second;
                    samples_<<"{\"t\":"<<second<<",\"state\":"<<s.state<<",\"audio\":"<<s.audio_packets<<",\"video\":"<<s.video_packets
                        <<",\"queue\":"<<s.queued_frames<<",\"underruns\":"<<s.underruns<<",\"missing\":"<<s.missing_frames
                        <<",\"dropped\":"<<s.dropped_frames<<",\"max_render_us\":"<<t.max_render_us<<",\"max_packet_gap_us\":"<<t.max_packet_gap_us<<"}\n";samples_.flush();}
            }
            if(!closing_&&capture_seconds_&&capture_begun_&&(now-capture_started_>=std::chrono::seconds(capture_seconds_)||s.state==AB_ERROR))close();
            if(!closing_&&smoke_&&!busy()&&now-started_>=std::chrono::seconds(3)){
                juce::PNGImageFormat png;auto stream=report_.withFileExtension("png").createOutputStream();
                if(stream)png.writeImageToStream(createComponentSnapshot(getLocalBounds()),*stream);
                close();
            }
        }catch(const std::exception& e){error_=e.what();if(closing_&&!busy()&&!isRunning()){finished_(1);return;}closing_=true;if(!busy())stopCapture();}
    }
    PluginProcessor& processor_;ZlProcessorAdapter& adapter_;AudioEngine& engine_;
    juce::String arguments_,saved_output_,error_;std::function<void(int)> finished_;
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
    juce::ComboBox phones_,outputs_;
    juce::TextButton refresh_{juce::String::fromUTF8("刷新")},start_{juce::String::fromUTF8("开始监听")},stop_{juce::String::fromUTF8("停止")},preset_{juce::String::fromUTF8("PUBG 调试预设")};
    juce::TextButton advanced_{juce::String::fromUTF8("高级设置")};
    juce::ToggleButton mute_{juce::String::fromUTF8("静音输出")};juce::Slider volume_;juce::Label status_,volume_label_,volume_hint_;
    enum class Pending{none,refresh,stop};Pending pending_{};std::future<void> task_;
    std::vector<AB_Device> phone_items_,new_phones_;std::vector<AB_OutputDevice> output_items_,new_outputs_;
    bool closing_{},restart_after_{},pending_preset_{},test_{},smoke_{},capture_begun_{};int capture_seconds_{};
    juce::MemoryBlock pending_state_;juce::File report_;std::ofstream samples_;int64_t last_second_{-1};
    std::array<float,1024> ui_left_{},ui_right_{};int synthetic_blocks_{};
    std::chrono::steady_clock::time_point started_{},capture_started_{};
};
class BridgeWindow final : public juce::DocumentWindow {
public:
    BridgeWindow(BridgePanel* panel):DocumentWindow("ZL USB Audio Bridge",juce::Colour(0xff172033),allButtons){
        setUsingNativeTitleBar(true);setContentOwned(panel,true);setResizable(true,false);setResizeLimits(1000,780,2400,1800);
        centreWithSize(getWidth(),getHeight());setVisible(!panel->testMode());
    }
    void closeButtonPressed() override {static_cast<BridgePanel*>(getContentComponent())->close();}
};
}
class BridgeApplication final : public juce::JUCEApplication,private juce::Timer {
public:
    const juce::String getApplicationName() override {return "ZL USB Audio Bridge";}
    const juce::String getApplicationVersion() override {return "0.2.0";}
    void initialise(const juce::String& arguments) override {
        try {
            test_=arguments.contains("--");
            juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
            processor_=std::make_unique<PluginProcessor>();
            juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
            ZlProcessorAdapter::gamePreset(*processor_);
            if(!test_) {
                juce::MemoryBlock state;const auto file=settingsFolder().getChildFile("processor.bin");
                if(file.getSize()<=4*1024*1024 && file.loadFileAsData(state))processor_->restoreBridgeState(state.getData(),static_cast<int>(state.getSize()));
            }
            adapter_=std::make_unique<ZlProcessorAdapter>(*processor_);adapter_->prepare(48000,4096);
            if(arguments.contains("--self-test")) {
                const auto result=runZlSelfTests(*processor_,*adapter_);
                auto tokens=juce::StringArray::fromTokens(arguments,true);const auto index=tokens.indexOf("--result");
                const auto file=index>=0&&index+1<tokens.size()?juce::File::getCurrentWorkingDirectory().getChildFile(tokens[index+1]):
                    juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("self-test.json");
                file.getParentDirectory().createDirectory();file.replaceWithText(juce::JSON::toString(result,true));quit();return;
            }
            if(arguments.contains("--wasapi-smoke")) {
                auto tokens=juce::StringArray::fromTokens(arguments,true);const auto index=tokens.indexOf("--result");
                test_report_=index>=0&&index+1<tokens.size()?juce::File::getCurrentWorkingDirectory().getChildFile(tokens[index+1]):
                    juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("wasapi-test.json");
                test_task_=std::async(std::launch::async,[this]{return runZlWasapiTest(*adapter_);});startTimer(100);return;
            }
            engine_=std::make_unique<audiobridge::AudioEngine>(adapter_.get());
            window_=std::make_unique<BridgeWindow>(new BridgePanel(*processor_,*adapter_,*engine_,arguments,[this](int code){setApplicationReturnValue(code);quit();}));
        }catch(const std::exception& e){
            auto* object=new juce::DynamicObject;object->setProperty("error",e.what());
            try{writeJson(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("startup-error.json"),object);}catch(...){}
            setApplicationReturnValue(1);quit();
        }
    }
    void systemRequestedQuit() override {if(window_)window_->closeButtonPressed();else quit();}
    void shutdown() override {
        stopTimer();if(test_task_.valid())test_task_.wait();
        if(engine_)engine_->stop();
        if(processor_&&!test_) {
            juce::MemoryBlock state;processor_->getStateInformation(state);settingsFolder().createDirectory();
            settingsFolder().getChildFile("processor.bin").replaceWithData(state.getData(),state.getSize());
        }
        window_.reset();engine_.reset();adapter_.reset();processor_.reset();
    }
private:
    void timerCallback() override {
        if(test_task_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        stopTimer();
        try {
            const auto result=test_task_.get();test_report_.getParentDirectory().createDirectory();
            if(!test_report_.replaceWithText(juce::JSON::toString(result,true)))throw std::runtime_error("Cannot save WASAPI result");
        }catch(const std::exception& e){
            setApplicationReturnValue(1);auto* object=new juce::DynamicObject;object->setProperty("error",e.what());
            try{writeJson(test_report_,object);}catch(...){}
        }
        quit();
    }
    std::future<juce::var> test_task_;juce::File test_report_;
    bool test_{};std::unique_ptr<PluginProcessor> processor_;std::unique_ptr<ZlProcessorAdapter> adapter_;
    std::unique_ptr<audiobridge::AudioEngine> engine_;std::unique_ptr<BridgeWindow> window_;
};
juce::JUCEApplicationBase* juce_CreateApplication(){return new BridgeApplication();}
