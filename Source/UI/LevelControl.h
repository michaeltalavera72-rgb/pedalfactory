#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

// Horizontal level meter: -60..+6 dBFS, green/amber/red, peak hold, latching clip light (click to reset).
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    explicit LevelMeter(std::function<float()> takePeakFn) : takePeak(std::move(takePeakFn)) { startTimerHz(30); }

    void paint(juce::Graphics& g) override
    {
        using namespace juce;
        auto r = getLocalBounds().toFloat();
        auto clipR = r.removeFromRight(r.getHeight() + 2.0f).withTrimmedLeft(2.0f);
        g.setColour(Colour(0xff0d0d0d)); g.fillRoundedRectangle(r, 2.0f);

        const float x0 = r.getX() + 1.0f, w = r.getWidth() - 2.0f;
        auto xFor = [&](float db) { return x0 + w * jlimit(0.0f, 1.0f, (db - minDb) / (maxDb - minDb)); };
        const float lvl = xFor(Decibels::gainToDecibels(level, minDb - 1.0f));
        ColourGradient grad(Colour(0xff2fb34a), x0, 0, Colour(0xffe0262a), x0 + w, 0, false);
        grad.addColour((-12.0 - minDb) / (maxDb - minDb), Colour(0xff3fcf55));
        grad.addColour((-6.0 - minDb) / (maxDb - minDb), Colour(0xffe8c22a));
        grad.addColour((0.0 - minDb) / (maxDb - minDb), Colour(0xffe8752a));
        g.setGradientFill(grad);
        g.fillRect(Rectangle<float>(x0, r.getY() + 1.0f, lvl - x0, r.getHeight() - 2.0f));

        // peak hold line and 0 dB tick
        g.setColour(Colours::white.withAlpha(0.85f));
        g.fillRect(Rectangle<float>(xFor(Decibels::gainToDecibels(hold, minDb - 1.0f)) - 1.0f, r.getY() + 1.0f, 2.0f, r.getHeight() - 2.0f));
        g.setColour(Colours::white.withAlpha(0.35f));
        g.fillRect(Rectangle<float>(xFor(0.0f), r.getY(), 1.0f, r.getHeight()));

        g.setColour(clipped ? Colour(0xffff2a1a) : Colour(0xff3a1210));
        g.fillEllipse(clipR.reduced(1.0f));
    }

    void mouseDown(const juce::MouseEvent&) override { clipped = false; repaint(); }

private:
    static constexpr float minDb = -60.0f, maxDb = 6.0f;
    static constexpr float offScale = 0.0005f;                     // -66 dBFS
    std::function<float()> takePeak;
    float level = 0.0f, hold = 0.0f;
    int holdTicks = 0;
    bool clipped = false;

    void timerCallback() override
    {
        const float oldLevel = level, oldHold = hold; const bool oldClipped = clipped;
        const float p = takePeak();
        level = juce::jmax(p, level * 0.80f);                     // ~20 dB/s fall
        if (p >= hold) { hold = p; holdTicks = 45; }               // 1.5 s peak hold
        else if (--holdTicks <= 0) hold *= 0.85f;
        if (level < offScale) level = 0.0f;                        // below the meter's range: settle (no endless decay)
        if (hold < offScale) hold = 0.0f;
        if (p >= 1.0f) clipped = true;                             // 0 dBFS or more: latch until clicked
        if (! juce::exactlyEqual(level, oldLevel) || ! juce::exactlyEqual(hold, oldHold) || clipped != oldClipped)
            repaint();                                             // silence = nothing to redraw
    }
};

// Label + dB slider + meter, bound to a board parameter.
class LevelControl : public juce::Component
{
public:
    LevelControl(const juce::String& title, juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                 std::function<float()> takePeak)
        : meter(std::move(takePeak))
    {
        label.setText(title, juce::dontSendNotification);
        label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
        label.setJustificationType(juce::Justification::centredLeft);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 18);
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffe8a33a));
        slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff8a5a1a));
        slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2e3236));
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setDoubleClickReturnValue(true, 0.0);                 // double-click = 0 dB
        slider.setTooltip(title + " level (double-click for 0 dB)");
        addAndMakeVisible(label); addAndMakeVisible(slider); addAndMakeVisible(meter);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, paramId, slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop(r.getHeight() - 9);
        label.setBounds(top.removeFromLeft(54));
        slider.setBounds(top);
        meter.setBounds(r.withTrimmedLeft(54).withTrimmedRight(66).withHeight(7));
    }

private:
    juce::Label label;
    juce::Slider slider;
    LevelMeter meter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};
