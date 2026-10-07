#include "ZlProcessorAdapter.h"
#include "SelfTests.h"
#include "BridgeLookAndFeel.h"
#include "PluginEditor.hpp"
#include "Engine/AudioEngine.h"
#include "Device/DeviceCatalog.h"
#include "Device/UsbDeviceRepair.h"
#include "Logging.h"
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
        for(auto* c : std::array<juce::Component*,15>{&source_,&receiver_,&phones_,&outputs_,&refresh_,&start_,&stop_,&repair_,&preset_,&mute_,&advanced_,&status_,&output_status_,&phone_label_,&output_label_})addAndMakeVisible(c);
        for(auto* c : std::array<juce::Component*,4>{&volume_,&volume_label_,&volume_hint_,&diagnostics_})addChildComponent(c);
        for(auto* c:chromeControls())c->setLookAndFeel(&chrome_);
        phone_label_.setText(juce::String::fromUTF8("USB 设备"),juce::dontSendNotification);
        output_label_.setText(juce::String::fromUTF8("音频输出"),juce::dontSendNotification);
        phones_.setName(juce::String::fromUTF8("USB 设备"));outputs_.setName(juce::String::fromUTF8("音频输出"));
        source_.addItem("USB",1);source_.addItem("AirPlay",2);source_.setSelectedId(1,juce::dontSendNotification);
        source_.setName(juce::String::fromUTF8("音频来源"));
        receiver_.setText("ZL Audio Bridge",juce::dontSendNotification);
        receiver_.setTooltip(juce::String::fromUTF8("开始监听后，在 iPhone 正在播放面板的 AirPlay 音频列表中选择 ZL Audio Bridge"));
        phones_.setTextWhenNoChoicesAvailable(juce::String::fromUTF8("未检测到 iPhone / iPad"));
        outputs_.setTextWhenNoChoicesAvailable(juce::String::fromUTF8("未检测到音频输出设备"));
        volume_.setRange(0,100,1);volume_.setValue(50,juce::dontSendNotification);
        volume_.setSliderStyle(juce::Slider::LinearHorizontal);volume_.setTextBoxStyle(juce::Slider::TextBoxRight,false,50,24);
        volume_.setTextValueSuffix(" %");volume_.setName(juce::String::fromUTF8("监听音量"));
        volume_label_.setText(juce::String::fromUTF8("监听音量"),juce::dontSendNotification);
        volume_hint_.setText(juce::String::fromUTF8("仅调节本软件输出，不改变 Windows 音量"),juce::dontSendNotification);
        for(auto* label:{&receiver_,&status_,&output_status_,&phone_label_,&output_label_,&volume_label_,&volume_hint_,&diagnostics_})
            label->setFont(BridgeLookAndFeel::controlFont());
        output_status_.setJustificationType(juce::Justification::centredRight);
        diagnostics_.setJustificationType(juce::Justification::centredLeft);
        advanced_.setClickingTogglesState(true);
        advanced_.onClick=[this]{
            const auto open=advanced_.getToggleState();
            advanced_.setButtonText(juce::String::fromUTF8(open?"收起高级设置":"高级设置"));
            volume_.setVisible(open);volume_label_.setVisible(open);volume_hint_.setVisible(open);diagnostics_.setVisible(open);
            resized();repaint();
        };
        mute_.setToggleState(false,juce::dontSendNotification);
        status_.setJustificationType(juce::Justification::centredLeft);
        syncTheme();
        start_.setEnabled(false);stop_.setEnabled(false);repair_.setEnabled(false);
        repair_.setTooltip(juce::String::fromUTF8("修复所选手机的 USB 连接，需要管理员授权。请先关闭其他使用该手机的软件。"));
        repair_.onClick=[this]{repairConnection();};
        phones_.onChange=[this]{repair_notice_.clear();};
        source_.onChange=[this]{refreshDevices();resized();};
        refresh_.onClick=[this]{refreshDevices();};start_.onClick=[this]{startCapture();};stop_.onClick=[this]{stopCapture();};
        preset_.onClick=[this]{pending_preset_=true;restart_after_=isRunning();stopCapture();};
        mute_.onClick=[this]{engine_.volume(gain());};volume_.onValueChange=[this]{engine_.volume(gain());};
        processor_.bridgeStateRequested=[this](juce::MemoryBlock state){
            if(closing_ || busy())return;
            pending_state_=std::move(state);restart_after_=isRunning();stopCapture();
        };
        const auto settings=settingsFolder().getChildFile("devices.json");
        if(settings.existsAsFile()){
            const auto saved=juce::JSON::parse(settings);saved_output_=saved["output"].toString();
            if(saved["source"].toString()=="airplay")source_.setSelectedId(2,juce::dontSendNotification);
        }
        auto tokens=juce::StringArray::fromTokens(arguments_,true);
        if(arguments_.contains("--"))source_.setSelectedId(tokens.contains("--airplay")?2:1,juce::dontSendNotification);
        const auto capture=tokens.indexOf("--capture-muted");
        if(capture>=0) {
            capture_seconds_=capture+1<tokens.size()?tokens[capture+1].getIntValue():30;
            capture_seconds_=juce::jlimit(1,7200,capture_seconds_);test_=true;
        }
        smoke_=tokens.contains("--ui-smoke");test_=test_||smoke_;
        smoke_empty_=smoke_&&tokens.contains("--ui-empty");
        const auto result=tokens.indexOf("--result");
        report_=result>=0 && result+1<tokens.size()?juce::File::getCurrentWorkingDirectory().getChildFile(tokens[result+1].unquoted()):
            juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("validation.json");
        if(capture_seconds_) {
            report_.getParentDirectory().createDirectory();
            samples_.open(std::filesystem::path(report_.withFileExtension("jsonl").getFullPathName().toWideCharPointer()));
        }
        setSize(std::max(1100,editor_->getWidth()),std::max(800,editor_->getHeight()+128));
        started_=std::chrono::steady_clock::now();refreshDevices();startTimer(100);
    }
    ~BridgePanel() override {
        stopTimer();processor_.bridgeStateRequested={};
        if(task_.valid())task_.wait();
        engine_.stop();editor_.reset();
        for(auto* c:chromeControls())c->setLookAndFeel(nullptr);
        if(!test_)try{auto* o=new juce::DynamicObject;o->setProperty("output",saved_output_);o->setProperty("source",isAirPlay()?"airplay":"usb");writeJson(settingsFolder().getChildFile("devices.json"),o);}catch(...){}
    }
    bool testMode() const noexcept{return test_;}
    void close() {closing_=true;if(!busy())stopCapture();}
    void paint(juce::Graphics& g) override {
        g.fillAll(chrome_.background);
        g.setColour(chrome_.foreground.withAlpha(.12f));
        g.drawHorizontalLine(91,16.f,getWidth()-16.f);
        g.drawHorizontalLine(getHeight()-36,16.f,getWidth()-16.f);
        if(advanced_.getToggleState())g.drawHorizontalLine(getHeight()-144,16.f,getWidth()-16.f);
    }
    void resized() override {
        auto bounds=getLocalBounds();auto top=bounds.removeFromTop(92).reduced(16,10);
        auto row=top.removeFromTop(32);refresh_.setBounds(row.removeFromRight(76));row.removeFromRight(8);
        source_.setBounds(row.removeFromLeft(104));row.removeFromLeft(8);
        phone_label_.setText(juce::String::fromUTF8(isAirPlay()?"接收名称":"USB 设备"),juce::dontSendNotification);
        phone_label_.setBounds(row.removeFromLeft(72));phones_.setBounds(row.removeFromLeft(juce::roundToInt(getWidth()*.22f)));
        receiver_.setBounds(phones_.getBounds());receiver_.setVisible(isAirPlay());phones_.setVisible(!isAirPlay());
        row.removeFromLeft(16);output_label_.setBounds(row.removeFromLeft(72));outputs_.setBounds(row);
        top.removeFromTop(8);auto actions=top.removeFromTop(32);
        start_.setBounds(actions.removeFromLeft(116));actions.removeFromLeft(8);stop_.setBounds(actions.removeFromLeft(80));
        actions.removeFromLeft(8);repair_.setBounds(actions.removeFromLeft(112));
        actions.removeFromLeft(16);preset_.setBounds(actions.removeFromLeft(144));actions.removeFromLeft(20);mute_.setBounds(actions.removeFromLeft(128));
        advanced_.setBounds(actions.removeFromRight(128));
        auto footer=bounds.removeFromBottom(36).reduced(16,4);
        output_status_.setBounds(footer.removeFromRight(240));status_.setBounds(footer);
        if(advanced_.getToggleState()) {
            auto settings=bounds.removeFromBottom(108).reduced(16,8);
            auto volume=settings.removeFromTop(32);
            volume_label_.setBounds(volume.removeFromLeft(88));volume_.setBounds(volume.removeFromLeft(260));
            volume.removeFromLeft(16);volume_hint_.setBounds(volume);
            settings.removeFromTop(4);diagnostics_.setBounds(settings);
        }
        // Small desktops can constrain the native window below its requested size.
        // Preserve the editor's minimum aspect ratio instead of clipping its controls.
        const auto scale=std::min(1.f,static_cast<float>(bounds.getHeight())/(bounds.getWidth()*.47f));
        editor_->setBounds(0,0,juce::roundToInt(bounds.getWidth()/scale),juce::roundToInt(bounds.getHeight()/scale));
        editor_->setTransform(juce::AffineTransform::scale(scale).translated(static_cast<float>(bounds.getX()),static_cast<float>(bounds.getY())));
    }
private:
    std::array<juce::Component*,20> chromeControls() {
        return {&source_,&receiver_,&phones_,&outputs_,&refresh_,&start_,&stop_,&repair_,&preset_,&mute_,&advanced_,&status_,&output_status_,&phone_label_,&output_label_,&volume_,&volume_label_,&volume_hint_,&diagnostics_,&tooltips_};
    }
    void syncTheme() {
        const auto colour=[this](const juce::String& name) {
            const auto channel=[&](const char* suffix){return static_cast<juce::uint8>(processor_.state_.getRawParameterValue(name+suffix)->load());};
            return juce::Colour(channel("_r"),channel("_g"),channel("_b"));
        };
        if(!chrome_.update(colour("background"),colour("text")))return;
        start_.setColour(juce::TextButton::buttonColourId,chrome_.foreground);
        start_.setColour(juce::TextButton::textColourOffId,chrome_.background);
        for(auto* label:{&phone_label_,&output_label_,&volume_hint_,&diagnostics_,&output_status_})
            label->setColour(juce::Label::textColourId,chrome_.foreground.withAlpha(.78f));
        for(auto* c:chromeControls())c->sendLookAndFeelChange();
        repaint();
    }
    bool hasSelection() const {
        const auto phone=phones_.getSelectedItemIndex(),output=outputs_.getSelectedItemIndex();
        return (isAirPlay() || (phone>=0 && static_cast<size_t>(phone)<phone_items_.size())) && output>=0 && static_cast<size_t>(output)<output_items_.size();
    }
    bool isAirPlay() const {return source_.getSelectedId()==2;}
    bool hasPhone() const {const auto index=phones_.getSelectedItemIndex();return !isAirPlay()&&index>=0&&static_cast<size_t>(index)<phone_items_.size();}
    bool busy() const{return task_.valid();}
    bool isRunning() const {const auto s=engine_.status().state;return s==AB_CONNECTING||s==AB_READY||s==AB_STREAMING||s==AB_STOPPING;}
    float gain() const{return (test_||mute_.getToggleState())?0.f:static_cast<float>(volume_.getValue()/100.0);}
    void refreshDevices() {
        if(busy()||isRunning())return;
        error_.clear();
        repair_notice_.clear();
        phones_.setTextWhenNothingSelected(juce::String::fromUTF8("正在查找 USB 设备…"));
        outputs_.setTextWhenNothingSelected(juce::String::fromUTF8("正在查找音频输出…"));
        refresh_.setButtonText(juce::String::fromUTF8("刷新中…"));start_.setEnabled(false);
        phones_.setEnabled(false);outputs_.setEnabled(false);refresh_.setEnabled(false);
        pending_=Pending::refresh;
        task_=std::async(std::launch::async,[this,airplay=isAirPlay()]{new_phones_=airplay?std::vector<AB_Device>{}:list_phones();new_outputs_=list_outputs();});
    }
    void repairConnection() {
        if(busy()||closing_||isRunning()||!hasPhone())return;
        const auto serial=narrow(phone_items_[static_cast<size_t>(phones_.getSelectedItemIndex())].id);
        error_.clear();repair_notice_.clear();editor_->setEnabled(false);pending_=Pending::repair;
        task_=std::async(std::launch::async,[this,serial]{
            engine_.stop(); // Join all failed-session cleanup before restarting its node.
            repair_usb_device_connection(serial);
            new_phones_=list_phones();new_outputs_=list_outputs();
        });
    }
    void startCapture() {
        if(busy()||closing_||isRunning())return;
        const auto phone=phones_.getSelectedItemIndex(),output=outputs_.getSelectedItemIndex();
        if(!hasSelection()){error_=juce::String::fromUTF8(isAirPlay()?"请选择耳机或音频输出设备":"请选择已连接的手机和输出设备");return;}
        AB_Config c{};c.struct_size=sizeof(c);c.version=AB_VERSION;c.volume=gain();
        if(!isAirPlay())copy_text(c.device_id,phone_items_[static_cast<size_t>(phone)].id);
        copy_text(c.output_id,output_items_[static_cast<size_t>(output)].id);
        saved_output_=juce::String(c.output_id);error_.clear();repair_notice_.clear();editor_->setEnabled(false);
        try{
            if(isAirPlay())engine_.startAirPlay(c,std::filesystem::path(juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                .getSiblingFile("Wireless").getChildFile("AudioBridge.AirPlayHost.exe").getFullPathName().toWideCharPointer()));
            else engine_.start(c);
            capture_started_=std::chrono::steady_clock::now();capture_begun_=true;
        }
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
        o->setProperty("source",isAirPlay()?"airplay":"usb");o->setProperty("sample_rate",static_cast<int>(s.sample_rate));
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
        if(smoke_){o->setProperty("start_enabled",smoke_start_enabled_);o->setProperty("repair_enabled",smoke_repair_enabled_);o->setProperty("phone_placeholder",phones_.getTextWhenNothingSelected());o->setProperty("output_placeholder",outputs_.getTextWhenNothingSelected());}
        writeJson(report_,o);
    }
    void timerCallback() override {
        try {
            if(busy() && task_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                auto completed=pending_;pending_=Pending::none;
                bool succeeded=true;
                try{task_.get();}catch(const std::exception& e){
                    succeeded=false;error_=juce::String::fromUTF8(e.what());closing_=closing_||test_;
                    if(completed==Pending::repair)iPhoneMirror::logging::write(std::string("usb_repair failed error=")+e.what());
                }
                if(completed==Pending::repair&&succeeded)repair_notice_=juce::String::fromUTF8("USB 连接已恢复，请点击“开始监听”");
                if(completed==Pending::refresh || (completed==Pending::repair&&succeeded)) {
                    juce::String selected_phone;
                    const auto previous_phone=phones_.getSelectedItemIndex(),previous_output=outputs_.getSelectedItemIndex();
                    if(previous_phone>=0&&static_cast<size_t>(previous_phone)<phone_items_.size())selected_phone=juce::String(phone_items_[static_cast<size_t>(previous_phone)].id);
                    if(previous_output>=0&&static_cast<size_t>(previous_output)<output_items_.size())saved_output_=juce::String(output_items_[static_cast<size_t>(previous_output)].id);
                    if(smoke_empty_){new_phones_.clear();new_outputs_.clear();}
                    phone_items_=std::move(new_phones_);output_items_=std::move(new_outputs_);phones_.clear();outputs_.clear();
                    int selected_phone_id=phone_items_.empty()?0:1;
                    for(size_t i=0;i<phone_items_.size();++i){phones_.addItem(juce::String(phone_items_[i].name),static_cast<int>(i+1));if(selected_phone==juce::String(phone_items_[i].id))selected_phone_id=static_cast<int>(i+1);}
                    int selected=1;
                    for(size_t i=0;i<output_items_.size();++i){outputs_.addItem(juce::String(output_items_[i].name),static_cast<int>(i+1));if(output_items_[i].is_default)selected=static_cast<int>(i+1);}
                    for(size_t i=0;i<output_items_.size();++i)if(saved_output_==juce::String(output_items_[i].id))selected=static_cast<int>(i+1);
                    phones_.setSelectedId(selected_phone_id,juce::dontSendNotification);outputs_.setSelectedId(output_items_.empty()?0:selected);
                    phones_.setTextWhenNothingSelected(juce::String::fromUTF8(phone_items_.empty()?"未检测到 iPhone / iPad":"请选择 iPhone / iPad"));
                    outputs_.setTextWhenNothingSelected(juce::String::fromUTF8(output_items_.empty()?"未检测到音频输出设备":"请选择音频输出设备"));
                    refresh_.setButtonText(juce::String::fromUTF8("刷新"));
                    if(capture_seconds_&&!closing_){startCapture();if(!capture_begun_)closing_=true;}
                    if(closing_)stopCapture();
                } else if(completed==Pending::repair&&closing_) {stopCapture();}
                else if(completed==Pending::stop) {
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
            syncTheme();
            const auto s=engine_.status();const auto t=engine_.telemetry();const auto running=isRunning();
            source_.setEnabled(!busy()&&!running&&!closing_);
            phones_.setEnabled(!busy()&&!running&&!closing_&&!phone_items_.empty());outputs_.setEnabled(!busy()&&!running&&!closing_&&!output_items_.empty());refresh_.setEnabled(!busy()&&!running&&!closing_);
            start_.setEnabled(!busy()&&!running&&!closing_&&hasSelection());stop_.setEnabled(!busy()&&running&&!closing_);preset_.setEnabled(!busy()&&!closing_);
            repair_.setEnabled(!busy()&&!running&&!closing_&&hasPhone());
            editor_->setEnabled(!busy()&&s.state!=AB_CONNECTING&&s.state!=AB_STOPPING&&!closing_);
            juce::String message;
            if(!error_.isEmpty())message=error_;
            else if(pending_==Pending::repair)message=juce::String::fromUTF8("正在修复 USB 连接，请完成管理员授权并稍候…");
            else if(!repair_notice_.isEmpty())message=repair_notice_;
            else if(s.state==AB_ERROR)message=juce::String(s.message);
            else if(pending_==Pending::refresh)message=juce::String::fromUTF8("正在刷新设备…");
            else if(closing_||s.state==AB_STOPPING||pending_==Pending::stop)message=juce::String::fromUTF8(isAirPlay()?"正在停止 AirPlay，请稍候…":"正在停止并恢复 USB，请稍候…");
            else if(s.state==AB_CONNECTING)message=juce::String::fromUTF8(isAirPlay()?"正在启动 AirPlay…":"正在连接 USB 设备…");
            else if(s.state==AB_STREAMING)message=juce::String::fromUTF8(s.source_idle?"已连接，等待音频…":"正在监听");
            else if(s.state==AB_READY)message=isAirPlay()?juce::String(s.message):juce::String::fromUTF8("已连接，等待音频…");
            else if(!isAirPlay()&&phone_items_.empty())message=juce::String::fromUTF8("请连接并解锁 iPhone / iPad，然后点击刷新");
            else if(output_items_.empty())message=juce::String::fromUTF8("请连接或启用音频输出设备，然后点击刷新");
            else if(isAirPlay())message=juce::String::fromUTF8("点击开始监听，再从 iPhone 的 AirPlay 音频列表选择 ZL Audio Bridge");
            else message=juce::String::fromUTF8("设备已就绪，点击“开始监听”");
            status_.setText(message,juce::dontSendNotification);status_.setTooltip(message);
            output_status_.setText(juce::String::fromUTF8((test_||mute_.getToggleState())?"输出已静音":"输出已开启")+juce::String::fromUTF8(" · 音量 ")+juce::String(volume_.getValue(),0)+"%",juce::dontSendNotification);
            diagnostics_.setText(juce::String::fromUTF8("输入 ")+juce::String(s.input_left_db,1)+" / "+juce::String(s.input_right_db,1)+" dB   "+
                juce::String::fromUTF8("设备输出 ")+juce::String(s.output_left_db,1)+" / "+juce::String(s.output_right_db,1)+" dB   "+
                juce::String::fromUTF8("缓冲 ")+juce::String(s.queued_frames)+" / "+juce::String(s.capacity_frames)+"\n"+
                juce::String::fromUTF8("欠载 ")+juce::String(static_cast<juce::int64>(s.underruns))+juce::String::fromUTF8("  缺失帧 ")+juce::String(static_cast<juce::int64>(s.missing_frames))+
                juce::String::fromUTF8("  软件延迟估计 ")+juce::String(s.host_latency_ms+processor_.getLatencySamples()*1000.0f/(s.sample_rate?s.sample_rate:48000),1)+" ms   DSP "+juce::String(processor_.getLatencySamples())+
                juce::String::fromUTF8(" 帧\n")+(s.sample_rate?juce::String(s.sample_rate/1000.0,1):juce::String("--"))+
                juce::String::fromUTF8(" kHz / PCM16 / 立体声 · WASAPI 共享模式 · Lookahead 0 / Oversampling Off"),juce::dontSendNotification);
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
                smoke_start_enabled_=start_.isEnabled();
                smoke_repair_enabled_=repair_.isEnabled();
                if(!hasPhone()&&smoke_repair_enabled_)throw std::runtime_error("Repair must be disabled without a selected phone");
                if(!hasSelection()&&smoke_start_enabled_)throw std::runtime_error("Start must be disabled without a device and output");
                const auto snapshot=[this](const juce::File& file){
                    if(!file.getParentDirectory().createDirectory().wasOk())throw std::runtime_error("Cannot create UI snapshot directory");
                    juce::PNGImageFormat png;auto stream=file.createOutputStream();
                    if(!stream||!png.writeImageToStream(createComponentSnapshot(getLocalBounds()),*stream))throw std::runtime_error("Cannot save UI snapshot");
                };
                snapshot(report_.withFileExtension("png"));
                advanced_.setToggleState(true,juce::dontSendNotification);advanced_.onClick();
                snapshot(report_.getSiblingFile(report_.getFileNameWithoutExtension()+"-advanced.png"));
                const auto original_size=getBounds();setSize(1000,780);
                snapshot(report_.getSiblingFile(report_.getFileNameWithoutExtension()+"-minimum.png"));
                setSize(original_size.getWidth(),original_size.getHeight());
                advanced_.setToggleState(false,juce::dontSendNotification);advanced_.onClick();
                close();
            }
        }catch(const std::exception& e){error_=e.what();if(closing_&&!busy()&&!isRunning()){finished_(1);return;}closing_=true;if(!busy())stopCapture();}
    }
    PluginProcessor& processor_;ZlProcessorAdapter& adapter_;AudioEngine& engine_;
    juce::String arguments_,saved_output_,error_,repair_notice_;std::function<void(int)> finished_;
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
    BridgeLookAndFeel chrome_;
    juce::ComboBox source_,phones_,outputs_;juce::Label receiver_;
    juce::TextButton refresh_{juce::String::fromUTF8("刷新")},start_{juce::String::fromUTF8("开始监听")},stop_{juce::String::fromUTF8("停止")},preset_{juce::String::fromUTF8("PUBG 调试预设")};
    juce::TextButton advanced_{juce::String::fromUTF8("高级设置")};
    juce::ToggleButton mute_{juce::String::fromUTF8("静音输出")};juce::Slider volume_;juce::Label status_,volume_label_,volume_hint_,diagnostics_,output_status_,phone_label_,output_label_;
    juce::TooltipWindow tooltips_{this,700};
    juce::TextButton repair_{juce::String::fromUTF8("修复 USB")};
    enum class Pending{none,refresh,stop,repair};Pending pending_{};std::future<void> task_;
    std::vector<AB_Device> phone_items_,new_phones_;std::vector<AB_OutputDevice> output_items_,new_outputs_;
    bool closing_{},restart_after_{},pending_preset_{},test_{},smoke_{},smoke_empty_{},smoke_start_enabled_{},smoke_repair_enabled_{},capture_begun_{};int capture_seconds_{};
    juce::MemoryBlock pending_state_;juce::File report_;std::ofstream samples_;int64_t last_second_{-1};
    std::array<float,1024> ui_left_{},ui_right_{};int synthetic_blocks_{};
    std::chrono::steady_clock::time_point started_{},capture_started_{};
};
class BridgeWindow final : public juce::DocumentWindow {
public:
    BridgeWindow(BridgePanel* panel):DocumentWindow("ZL USB Audio Bridge",juce::Colours::black,allButtons){
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
                const auto file=index>=0&&index+1<tokens.size()?juce::File::getCurrentWorkingDirectory().getChildFile(tokens[index+1].unquoted()):
                    juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("self-test.json");
                file.getParentDirectory().createDirectory();file.replaceWithText(juce::JSON::toString(result,true));quit();return;
            }
            if(arguments.contains("--wasapi-smoke")) {
                auto tokens=juce::StringArray::fromTokens(arguments,true);const auto index=tokens.indexOf("--result");
                test_report_=index>=0&&index+1<tokens.size()?juce::File::getCurrentWorkingDirectory().getChildFile(tokens[index+1].unquoted()):
                    juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("wasapi-test.json");
                const auto rateIndex=tokens.indexOf("--wasapi-rate");
                const auto rate=rateIndex>=0&&rateIndex+1<tokens.size()?static_cast<uint32_t>(tokens[rateIndex+1].getIntValue()):48000u;
                test_task_=std::async(std::launch::async,[this,rate]{return runZlWasapiTest(*adapter_,rate);});startTimer(100);return;
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
