#pragma once
#include "PedalBase.h"

// Ramhorn Fuzz: a "spirit" model of the classic Ram's Head-era Big Muff Pi circuit
// (shown in the UI under our own name). Not a full circuit simulation. It keeps the things that make the pedal what it is:
//   1. A clean-ish input boost (Q1)
//   2. SUSTAIN sets how hard the signal hits two cascaded clipping stages (Q2, Q3).
//      Each stage has huge gain at low frequencies that falls off above ~720 Hz
//      (470k feedback resistor + 470pF), then diodes clip it softly.
//   3. A passive TONE control that blends a lowpass (~408 Hz) and a highpass (~1 kHz)
//      with a mid scoop in between (39k / 10nF and 3.9nF / 39k).
//   4. A recovery stage (Q4) that brings the level back, then VOLUME.
// Component values come from the schematic. Gains/levels are tuned by ear, not solved.
class RamhornFuzz : public PedalBase
{
public:
    juce::String getId() const override { return "bm"; }
    juce::String getName() const override { return "Ramhorn Fuzz"; }
    PedalCategory getCategory() const override { return PedalCategory::Fuzz; }
    juce::String getBasedOn() const override { return "Electro-Harmonix Big Muff Pi (Ram's Head era, early 1970s)"; }

    // Tooltip text for each control (the editor adds the control's name on top).
    juce::String describe(const juce::String& id) const override
    {
        if (id == "bm_bypass") return "Footswitch: turns Ramhorn Fuzz on or off.";
        if (id == "bm_volume") return "Output level of the pedal.";
        if (id == "bm_tone") return "Blends a bass-heavy and a treble-heavy filter with a mid scoop between them. Down = dark and fat, up = bright and cutting.";
        if (id == "bm_sustain") return "Amount of fuzz and sustain: how hard the two clipping stages are driven.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bm_bypass",1}, "Ramhorn Bypass", false));
        l.add(std::make_unique<P>(juce::ParameterID{"bm_sustain",1}, "Sustain", 0.0f, 1.0f, 0.6f));
        l.add(std::make_unique<P>(juce::ParameterID{"bm_tone",1},    "Tone",    0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"bm_volume",1},  "Volume",  0.0f, 1.0f, 0.5f));
        knobIds = { "bm_volume", "bm_tone", "bm_sustain" };   // left to right on the face
        // ids keep the "bm" prefix so saved boards/projects still load
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam = s.getRawParameterValue("bm_bypass");
        sustain = s.getRawParameterValue("bm_sustain");
        tone    = s.getRawParameterValue("bm_tone");
        volume  = s.getRawParameterValue("bm_volume");
    }
    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 4x
        oversampler->initProcessing(spec.maximumBlockSize);
        const double fs = spec.sampleRate * 4.0;
        // fc = 1 / (2 pi R C) for each part of the circuit
        gStage = coeff(720.0, fs);    // 470k // 470pF in the feedback of Q1/Q2/Q3
        gToneLp = coeff(408.0, fs);   // 39k + 10nF
        gToneHp = coeff(1046.0, fs);  // 3.9nF + 39k
        gDc = coeff(20.0, fs);        // input/coupling capacitors
        // knobs glide over 30 ms (no zipper noise when turned or automated)
        susSm.reset(fs, 0.03); toneSm.reset(fs, 0.03); volSm.reset(fs, 0.03);
        if (sustain != nullptr)
        {
            susSm.setCurrentAndTargetValue(sustainGain(sustain->load()));
            toneSm.setCurrentAndTargetValue(tone->load());
            volSm.setCurrentAndTargetValue(volumeGain(volume->load()));
        }
        reset();
    }
    void reset() override { for (auto& c : ch) c = {}; if (oversampler) oversampler->reset(); }

    void process(juce::AudioBuffer<float>& buf) override
    {
        // Sustain is a log pot placed between Q1 and Q2: it scales the signal that hits the clippers.
        // Q2+Q3 together have ~70 dB of gain, so the pot needs a wide range (~-66 dB at minimum)
        // for the low end to clip only lightly; at -40 dB both stages stayed saturated.
        susSm.setTargetValue(sustainGain(sustain->load()));
        toneSm.setTargetValue(tone->load());
        volSm.setTargetValue(volumeGain(volume->load()));

        juce::dsp::AudioBlock<float> block(buf);
        auto up = oversampler->processSamplesUp(block);
        for (size_t c = 0; c < up.getNumChannels() && c < ch.size(); ++c)
        {
            auto& s = ch[c];
            auto* x = up.getChannelPointer(c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float susGain = susSm.getNextValue(), t = toneSm.getNextValue(), vol = volSm.getNextValue();
                float v = x[i] * 0.5f;                       // treat full scale as ~0.5 V guitar signal
                v -= lowpass(s.dc, v, gDc);                  // remove DC / sub-bass (coupling caps)

                v = lowpass(s.q1, v, gStage) * 12.0f;        // Q1 boost, ~21 dB
                v = 2.5f * std::tanh(v / 2.5f);              // limited by the 9 V rail

                v *= susGain;                                // SUSTAIN

                v = clipStage(s.q2, v);                      // Q2 clipper
                v = clipStage(s.q3, v);                      // Q3 clipper

                const float lp = lowpass(s.tLp, v, gToneLp); // TONE: lowpass leg
                const float hp = v - lowpass(s.tHp, v, gToneHp); // highpass leg
                v = ((1.0f - t) * lp + t * hp) * 0.5f;       // blend, passive loss

                v = 3.0f * std::tanh(3.7f * v / 3.0f);       // Q4 recovery stage
                x[i] = v * 0.4f * vol;                       // VOLUME
            }
        }
        oversampler->processSamplesDown(block);
    }

private:
    struct Ch { float dc = 0, q1 = 0, q2 = 0, q3 = 0, tLp = 0, tHp = 0; };
    std::array<Ch, 2> ch;
    float gStage = 0, gToneLp = 0, gToneHp = 0, gDc = 0;

    static float coeff(double fc, double fs) { auto g = std::tan(juce::MathConstants<double>::pi * fc / fs); return (float) (g / (1.0 + g)); }
    // one-pole lowpass (TPT form); s is the filter memory
    static float lowpass(float& s, float in, float g) { float v = (in - s) * g; float y = v + s; s = y + v; return y; }

    // Gain ~57 (470k / 8k2) that rolls off above ~720 Hz, then diode-style soft clip around +/-0.6 V
    float clipStage(float& state, float in) const
    {
        const float v = lowpass(state, in, gStage) * 57.0f;
        return 0.6f * std::tanh(v / 0.6f);
    }

    std::atomic<float> *sustain{}, *tone{}, *volume{};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> susSm { 1.0f };
    juce::SmoothedValue<float> toneSm, volSm;
    static float sustainGain(float sus) { return juce::Decibels::decibelsToGain(-66.0f * std::pow(1.0f - sus, 1.5f)); }
    static float volumeGain(float v) { return 2.0f * v * v; }   // audio-taper feel
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
};
