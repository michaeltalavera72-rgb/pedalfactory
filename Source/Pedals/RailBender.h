#pragma once
#include "PedalBase.h"

// Rail Bender: a model of the T-Rex Hobo Drive (schematic V1.2.3, 2011), shown under our own name.
// Two independently switched sections, like the original's two footswitches:
//
//   BOOST (right switch, "rb_boost_on"): op-amp U2-B, gain 1 + POT3(47k B)/R15(10k) = 0 .. +15 dB,
//       C13 4u7 -> flat above ~3 Hz. Off = JFET Q4 shorts the feedback -> unity. Sits BEFORE the drive,
//       so it also pushes the drive harder. Works with the drive switched off (buffered path).
//   DRIVE (left switch = main bypass):
//       U6-A buffer -> C1 22n / R2 470k (~15 Hz) ->
//       U1-A  gain 1 + (R4 240k || (R31 3k3 + PREAMP 470k log)) / (R3 2k2 + C2 470n):
//             x2.5 .. x73, only above ~154 Hz (tight bass); 33p roll-off; TL072 rails on +9 / -6 V
//       R5 10k -> D31/D32 AA119 germanium diodes to ground || C4 15n (handwritten value on the
//             drawing): soft, low-threshold clipping, warm top end. Solved per sample (diode ODE).
//       U1-B  gain 1 + R8 150k / (R7 39k + C5 100n) = x4.85 above ~41 Hz
//       TONE  R9 13k into (P2 22k log + C7 100n) to ground: treble cut, fully CW = brightest
//       LEVEL P5 47k linear -> output buffer.
class RailBender : public PedalBase
{
public:
    juce::String getId() const override { return "rb"; }
    juce::String getName() const override { return "Rail Bender"; }
    PedalCategory getCategory() const override { return PedalCategory::Overdrive; }
    juce::String getBasedOn() const override { return "T-Rex Hobo Drive (schematic V1.2.3, 2011)"; }
    juce::String secondarySwitchId() const override { return "rb_boost_on"; }
    juce::String secondarySwitchLabel() const override { return "BOOST"; }

    // Tooltip text for each control (the editor adds the control's name on top).
    juce::String describe(const juce::String& id) const override
    {
        if (id == "rb_bypass") return "Left footswitch: turns the DRIVE section on or off.";
        if (id == "rb_boost_on") return "Right footswitch: turns the BOOST on or off. The boost sits before the drive and also works with the drive off.";
        if (id == "rb_drive") return "Amount of germanium-diode overdrive.";
        if (id == "rb_tone") return "Treble: down = darker, up = brighter.";
        if (id == "rb_level") return "Output level of the drive section.";
        if (id == "rb_boost") return "Clean boost amount, 0 to +15 dB (when the BOOST footswitch is on). Also pushes the drive harder.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"rb_bypass",1}, "Rail Bender Drive Bypass", false));
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"rb_boost_on",1}, "Rail Bender Boost On", false));
        l.add(std::make_unique<P>(juce::ParameterID{"rb_drive",1}, "Drive", 0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"rb_tone",1},  "Tone",  0.0f, 1.0f, 0.75f));
        l.add(std::make_unique<P>(juce::ParameterID{"rb_level",1}, "Level", 0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"rb_boost",1}, "Boost", 0.0f, 1.0f, 0.5f));
        knobIds = { "rb_drive", "rb_tone", "rb_level", "rb_boost" };   // left to right on the face
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam    = s.getRawParameterValue("rb_bypass");
        secondaryParam = s.getRawParameterValue("rb_boost_on");
        drive = s.getRawParameterValue("rb_drive");
        tone  = s.getRawParameterValue("rb_tone");
        level = s.getRawParameterValue("rb_level");
        boost = s.getRawParameterValue("rb_boost");
    }
    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 4x
        oversampler->initProcessing(spec.maximumBlockSize);
        const double fs = sr * 4.0;
        T = 1.0 / fs;
        gIn   = coeff(15.0, fs);       // C1 22n into R1 10k + R2 470k
        gU1aHp = coeff(154.0, fs);     // R3 2k2 / C2 470n
        gU1aLp = coeff(20000.0, fs);   // 33p across ~160k feedback (plus TL072 limits)
        gU1bHp = coeff(40.8, fs);      // R7 39k / C5 100n
        gDc   = coeff(8.0, sr);
        driveGain.reset(sr * 4.0, 0.03); boostGain.reset(sr * 4.0, 0.03);
        driveMix.reset(sr * 4.0, 0.01);  boostMix.reset(sr * 4.0, 0.01);
        outGain.reset(sr * 4.0, 0.03);
        lastTone = -1.0f;
        reset();
    }
    void reset() override
    {
        for (auto& c : ch) c = {};
        if (oversampler) oversampler->reset();
    }
    // Back on after both footswitches were off: no half-finished switch fades from last time.
    void activated() override
    {
        reset();
        driveMix.setCurrentAndTargetValue(primaryOn() ? 1.0f : 0.0f);
        boostMix.setCurrentAndTargetValue(secondaryOn() ? 1.0f : 0.0f);
        driveIdle = false;
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        const bool driveOn = primaryOn(), boostOn = secondaryOn();
        // knob -> component values
        const double rPre = 470000.0 * audioTaper(drive->load());
        const double rf = 1.0 / (1.0 / 240000.0 + 1.0 / (3300.0 + rPre));
        driveGain.setTargetValue((float) (rf / 2200.0));                       // the "+1" is added below
        boostGain.setTargetValue(47000.0f * boost->load() / 10000.0f);
        driveMix.setTargetValue(driveOn ? 1.0f : 0.0f);                        // smooth switching, no clicks
        boostMix.setTargetValue(boostOn ? 1.0f : 0.0f);
        updateTone(tone->load());
        const float lv = level->load();
        outGain.setTargetValue(levelScale * lv);                                // P5 47k linear (x2 = volts -> digital)

        // DRIVE switched off and its fade finished (BOOST only): don't run the drive path at all.
        const bool idleNow = ! driveOn && ! driveMix.isSmoothing() && driveMix.getCurrentValue() <= 0.0f;
        if (driveIdle && ! idleNow)
        {
            // drive coming back: clean drive filters / diode state, knobs where they are now (the 10 ms fade covers the start)
            for (auto& c : ch) { c.in = c.u1aHp = c.u1aLp = c.u1bHp = c.tone = 0.0f; c.vc = 0.0; }
            driveGain.setCurrentAndTargetValue(driveGain.getTargetValue());
            outGain.setCurrentAndTargetValue(outGain.getTargetValue());
        }
        driveIdle = idleNow;

        juce::dsp::AudioBlock<float> block(buf);
        auto up = oversampler->processSamplesUp(block);
        for (size_t c = 0; c < up.getNumChannels() && c < ch.size(); ++c)
        {
            auto& s = ch[c];
            auto* x = up.getChannelPointer(c);
            if (idleNow)
            {
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const float dry = x[i] * 0.5f;
                    x[i] = rail(dry + boostMix.getNextValue() * boostGain.getNextValue() * dry) * 2.0f;
                }
                continue;
            }
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float bg = boostGain.getNextValue(), dg = driveGain.getNextValue();
                const float bm = boostMix.getNextValue(), dm = driveMix.getNextValue();
                const float dry = x[i] * 0.5f;                                   // full scale ~ 0.5 V guitar

                // BOOST: unity when off; 1 + R/10k when on. Op-amp rails +9 / -6 V
                float v = dry + bm * bg * dry;
                v = rail(v);

                // DRIVE
                float d = v - lowpass(s.in, v, gIn);
                d = d + dg * (d - lowpass(s.u1aHp, d, gU1aHp));                  // gain only above ~154 Hz
                d = lowpass(s.u1aLp, d, gU1aLp);
                d = rail(d);
                d = (float) germaniumClipper(s.vc, d);                            // R5 + AA119 pair || C4
                d = d + 3.846f * (d - lowpass(s.u1bHp, d, gU1bHp));              // U1-B: x4.85 above 41 Hz
                d = rail(d);
                d = d - toneK * (d - lowpass(s.tone, d, gTone));                  // treble cut (see updateTone)

                // LEVEL acts on the drive path; with the drive switched off the (boosted) buffered
                // signal passes at unity. Volts back to digital: x2.
                const float lg = outGain.getNextValue();
                x[i] = dm * d * lg + (1.0f - dm) * v * 2.0f;
            }
        }
        oversampler->processSamplesDown(block);

        for (int c = 0; c < buf.getNumChannels() && c < (int) ch.size(); ++c)
        {
            auto* x = buf.getWritePointer(c);
            auto& s = ch[(size_t) c];
            for (int i = 0; i < buf.getNumSamples(); ++i)
                x[i] -= lowpass(s.dc, x[i], gDc);                                 // output coupling
        }
    }

private:
    struct Ch { float in = 0, u1aHp = 0, u1aLp = 0, u1bHp = 0, tone = 0, dc = 0; double vc = 0; };
    std::array<Ch, 2> ch;
    double sr = 48000.0, T = 1.0 / 192000.0;
    float gIn = 0, gU1aHp = 0, gU1aLp = 0, gU1bHp = 0, gDc = 0, gTone = 0, toneK = 0, lastTone = -1;
    bool driveIdle = false;
    juce::SmoothedValue<float> driveGain, boostGain, driveMix, boostMix, outGain;
    std::atomic<float> *drive{}, *tone{}, *level{}, *boost{};
    static constexpr float levelScale = 0.5f;   // calibrated: ~-21 dBFS at noon like the other pedals
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    static float coeff(double fc, double fs) { auto g = std::tan(juce::MathConstants<double>::pi * fc / fs); return (float) (g / (1.0 + g)); }
    static float lowpass(float& s, float in, float g) { float v = (in - s) * g; float y = v + s; s = y + v; return y; }
    static double audioTaper(float x) { return (std::pow(10.0, 2.0 * (double) x) - 1.0) / 99.0; }   // "A"/log pot

    // TL072 output swing on +9 V / -6 V rails, soft at the edges
    static float rail(float v)
    {
        return v >= 0.0f ? 7.4f * std::tanh(v / 7.4f) : 4.6f * std::tanh(v / 4.6f);
    }

    // R5 10k feeding two anti-parallel AA119 germanium diodes and C4 15n to ground.
    // C dv/dt = (u - v)/R - 2 Is sinh(v / nVt), backward Euler + Newton.
    double germaniumClipper(double& v, float u) const
    {
        constexpr double R = 10000.0, C = 15e-9, Is = 1.3e-7, nVt = 1.3 * 0.02585;
        const double cT = C / T, vPrev = v;
        double x = v;
        for (int it = 0; it < 4; ++it)
        {
            const double a = juce::jlimit(-30.0, 30.0, x / nVt);
            const double e = std::exp(a), ei = 1.0 / e;
            const double f  = cT * (x - vPrev) - ((double) u - x) / R + Is * (e - ei);
            const double fp = cT + 1.0 / R + Is / nVt * (e + ei);
            const double step = f / fp;
            x -= step;
            if (std::abs(step) < 1e-5) break;
        }
        v = x;
        return x;
    }

    // Tone: H = (Rt + 1/sC)/(R9 + Rt + 1/sC) = 1 - R9/(R9+Rt) * HP(fc), fc = 1/(2 pi (R9+Rt) C7)
    void updateTone(float t)
    {
        if (juce::exactlyEqual(t, lastTone)) return;
        lastTone = t;
        const double rt = 22000.0 * audioTaper(t), r9 = 13000.0, c7 = 100e-9;
        toneK = (float) (r9 / (r9 + rt));
        // implemented as d - K*(d - LP(d)) = d - K*HP(d); LP corner = HP corner
        gTone = coeff(1.0 / (juce::MathConstants<double>::twoPi * (r9 + rt) * c7), sr * 4.0);
    }
};
