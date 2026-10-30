#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

// Used only by the standalone controls, never by the embedded ZL editor.
class BridgeLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    juce::Colour background, foreground, surface, outline;

    bool update(juce::Colour bg, juce::Colour text) {
        bg=bg.withAlpha(1.f);text=text.withAlpha(1.f);
        if(background==bg && foreground==text)return false;
        background=bg;foreground=text;
        surface=bg.interpolatedWith(text,.055f);
        outline=bg.interpolatedWith(text,.2f);
        setColour(juce::ComboBox::backgroundColourId,surface);
        setColour(juce::ComboBox::textColourId,text);
        setColour(juce::ComboBox::arrowColourId,text);
        setColour(juce::ComboBox::outlineColourId,outline);
        setColour(juce::ComboBox::focusedOutlineColourId,text.withAlpha(.7f));
        setColour(juce::PopupMenu::backgroundColourId,surface);
        setColour(juce::PopupMenu::textColourId,text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId,bg.interpolatedWith(text,.16f));
        setColour(juce::PopupMenu::highlightedTextColourId,text);
        setColour(juce::PopupMenu::headerTextColourId,text);
        setColour(juce::TextButton::buttonColourId,surface);
        setColour(juce::TextButton::buttonOnColourId,bg.interpolatedWith(text,.14f));
        setColour(juce::TextButton::textColourOffId,text);
        setColour(juce::TextButton::textColourOnId,text);
        setColour(juce::ToggleButton::textColourId,text);
        setColour(juce::ToggleButton::tickColourId,text);
        setColour(juce::ToggleButton::tickDisabledColourId,text.withAlpha(.5f));
        setColour(juce::Label::textColourId,text);
        setColour(juce::TooltipWindow::backgroundColourId,surface);
        setColour(juce::TooltipWindow::textColourId,text);
        setColour(juce::TooltipWindow::outlineColourId,outline);
        setColour(juce::Slider::backgroundColourId,outline);
        setColour(juce::Slider::trackColourId,text.withAlpha(.6f));
        setColour(juce::Slider::thumbColourId,text);
        setColour(juce::Slider::textBoxTextColourId,text);
        setColour(juce::Slider::textBoxBackgroundColourId,surface);
        setColour(juce::Slider::textBoxOutlineColourId,outline);
        setColour(juce::Slider::textBoxHighlightColourId,bg.interpolatedWith(text,.25f));
        return true;
    }

    static juce::Font controlFont() {return juce::Font(juce::FontOptions("Microsoft YaHei UI",16.f,juce::Font::plain));}
    juce::Font getComboBoxFont(juce::ComboBox&) override {return controlFont();}
    juce::Font getPopupMenuFont() override {return controlFont();}
    juce::Font getTextButtonFont(juce::TextButton&,int) override {return controlFont();}

    void drawButtonBackground(juce::Graphics& g,juce::Button& button,const juce::Colour& colour,
                              bool hover,bool down) override {
        auto bounds=button.getLocalBounds().toFloat().reduced(.5f);
        auto fill=colour;
        if(!button.isEnabled())fill=surface;
        else if(down || hover)fill=fill.interpolatedWith(colour==foreground?background:foreground,down?.18f:.08f);
        g.setColour(fill);g.fillRoundedRectangle(bounds,6.f);
        g.setColour(button.hasKeyboardFocus(true)?foreground.withAlpha(.7f):outline);
        g.drawRoundedRectangle(bounds,6.f,1.f);
    }
    void drawButtonText(juce::Graphics& g,juce::TextButton& button,bool,bool) override {
        g.setFont(controlFont());
        g.setColour(button.isEnabled()?button.findColour(button.getToggleState()?juce::TextButton::textColourOnId:juce::TextButton::textColourOffId):foreground.withAlpha(.45f));
        g.drawFittedText(button.getButtonText(),button.getLocalBounds().reduced(8,0),juce::Justification::centred,1);
    }
    void drawComboBox(juce::Graphics& g,int width,int height,bool,int,int,int,int,juce::ComboBox& box) override {
        const auto bounds=juce::Rectangle<float>(0,0,static_cast<float>(width),static_cast<float>(height)).reduced(.5f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));g.fillRoundedRectangle(bounds,6.f);
        g.setColour(box.findColour(box.hasKeyboardFocus(true)?juce::ComboBox::focusedOutlineColourId:juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds,6.f,1.f);
        juce::Path arrow;const auto x=static_cast<float>(width)-19.f,y=height*.5f;
        arrow.startNewSubPath(x-4.f,y-2.f);arrow.lineTo(x,y+2.f);arrow.lineTo(x+4.f,y-2.f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(box.isEnabled()?.9f:.55f));
        g.strokePath(arrow,juce::PathStrokeType(1.5f));
    }
    void positionComboBoxText(juce::ComboBox& box,juce::Label& label) override {
        label.setBounds(10,1,box.getWidth()-38,box.getHeight()-2);label.setFont(controlFont());
    }
    void drawComboBoxTextWhenNothingSelected(juce::Graphics& g,juce::ComboBox& box,juce::Label& label) override {
        // JUCE's default uses the LookAndFeel colour at 50% alpha, ignoring the box colour.
        g.setColour(box.findColour(juce::ComboBox::textColourId).withMultipliedAlpha(.78f));
        g.setFont(controlFont());g.drawFittedText(box.getTextWhenNothingSelected(),label.getBounds(),juce::Justification::centredLeft,1);
    }
    void drawLabel(juce::Graphics& g,juce::Label& label) override {
        if(auto* box=dynamic_cast<juce::ComboBox*>(label.getParentComponent())) {
            g.setColour(box->findColour(juce::ComboBox::textColourId).withMultipliedAlpha(box->isEnabled()?1.f:.7f));
            g.setFont(controlFont());g.drawFittedText(label.getText(),label.getLocalBounds(),juce::Justification::centredLeft,1);
        } else juce::LookAndFeel_V4::drawLabel(g,label);
    }
    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& button,bool hover,bool down) override {
        const auto y=(button.getHeight()-18.f)*.5f;
        drawTickBox(g,button,1.f,y,18.f,18.f,button.getToggleState(),button.isEnabled(),hover,down);
        g.setColour(foreground.withMultipliedAlpha(button.isEnabled()?1.f:.5f));g.setFont(controlFont());
        g.drawFittedText(button.getButtonText(),button.getLocalBounds().withTrimmedLeft(27),juce::Justification::centredLeft,1);
    }
};
