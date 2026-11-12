#pragma once
#include "PedalBase.h"

// Shared Drive/Tone/Level pedal. Subclasses only supply id, name and the clipping curve.
class DriveBase : public PedalBase
{
public:
    juce::String describe(const juce::String& id) const override
    {
        if (id.endsWith("_bypass")) return "Footswitch: turns the pedal on or off.";
        if (id.endsWith("_drive"))  return "Amount of distortion.";
        if (id.endsWith("_tone"))   return "Treble: down = darker, up = brighter.";
        if (id.endsWith("_level"))  return "Output level of the pedal.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        auto id = getId();
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id + "_bypass",1}, getName() + " Bypass", false));
        l.add(std::make_unique<P>(juce::ParameterID{id + "_drive",1}, "Drive", 0.0f, 1.0f, 0.4f));
        l.add(std::make_unique<P>(juce::ParameterID{id + "_tone",1},  "Tone",  0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{id + "_level",1}, "Level", 0.0f, 1.0f, 0.5f));
        knobIds = { id + "_drive", id + "_tone", id + "_level" };
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        auto id = getId();
        bypassParam = s.getRawParameterValue(id + "_bypass");
        drive = s.getRawParameterValue(id + "_drive");
        tone  = s.getRawParameterValue(id + "_tone");
        level = s.getRawParameterValue(id + "_level");
    }
    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
        oversampler->initProcessing(spec.maximumBlockSize);
        post.prepare(spec);
        post.setType(juce::dsp::FirstOrderTPTFilterType::lowpass);
        // DRIVE and LEVEL glide over 30 ms (no zipper noise)
        driveSm.reset(spec.sampleRate * 2.0, 0.03); levelSm.reset(spec.sampleRate, 0.03);
        if (drive != nullptr) { driveSm.setCurrentAndTargetValue(driveGain(drive->load())); levelSm.setCurrentAndTargetValue(levelGain(level->load())); }
    }
    void reset() override { post.reset(); if (oversampler) oversampler->reset(); }

    void process(juce::AudioBuffer<float>& buf) override
    {
        driveSm.setTargetValue(driveGain(drive->load()));
        levelSm.setTargetValue(levelGain(level->load()));
        post.setCutoffFrequency(juce::jmap(tone->load(), 0.0f, 1.0f, 1200.0f, 9000.0f));
        juce::dsp::AudioBlock<float> block(buf);
        auto up = oversampler->processSamplesUp(block);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* x = up.getChannelPointer(c);
            auto g = driveSm;                                    // same ramp on every channel
            for (size_t i = 0; i < up.getNumSamples(); ++i) x[i] = shape(x[i] * g.getNextValue()) * 0.5f;
        }
        driveSm.skip((int) up.getNumSamples());
        oversampler->processSamplesDown(block);
        post.process(juce::dsp::ProcessContextReplacing<float>(block));
        for (int c = 0; c < buf.getNumChannels(); ++c)
        {
            auto* x = buf.getWritePointer(c);
            auto g = levelSm;
            for (int i = 0; i < buf.getNumSamples(); ++i) x[i] *= g.getNextValue();
        }
        levelSm.skip(buf.getNumSamples());
    }
protected:
    virtual float shape(float x) const = 0;   // the clipping curve
private:
    std::atomic<float> *drive{}, *tone{}, *level{};
    juce::SmoothedValue<float> driveSm, levelSm;
    static float driveGain(float d) { return 1.0f + 80.0f * d * d; }
    static float levelGain(float l) { return juce::Decibels::decibelsToGain(juce::jmap(l, 0.0f, 1.0f, -24.0f, 6.0f)); }
    juce::dsp::FirstOrderTPTFilter<float> post;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
};

struct Overdrive  : DriveBase { juce::String getId() const override { return "od"; } juce::String getName() const override { return "Overdrive"; }
    PedalCategory getCategory() const override { return PedalCategory::Overdrive; }
    juce::String getBasedOn() const override { return "Generic placeholder - not based on a specific pedal yet"; }
    float shape(float x) const override { return std::tanh(x); } };
struct Distortion : DriveBase { juce::String getId() const override { return "ds"; } juce::String getName() const override { return "Distortion"; }
    PedalCategory getCategory() const override { return PedalCategory::Distortion; }
    juce::String getBasedOn() const override { return "Generic placeholder - not based on a specific pedal yet"; }
    float shape(float x) const override { return juce::jlimit(-1.0f, 1.0f, x); } };
struct Fuzz       : DriveBase { juce::String getId() const override { return "fz"; } juce::String getName() const override { return "Fuzz"; }
    PedalCategory getCategory() const override { return PedalCategory::Fuzz; }
    juce::String getBasedOn() const override { return "Generic placeholder - not based on a specific pedal yet"; }
    float shape(float x) const override { return x > 0 ? std::tanh(x * 2.0f) : std::tanh(x * 0.6f) * 1.3f; } };
