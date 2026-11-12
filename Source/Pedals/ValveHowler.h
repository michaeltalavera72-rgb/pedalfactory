#pragma once
#include "PedalBase.h"

// Valve Howler: a model of the Ibanez TS-808 Tube Screamer overdrive (schematic drawn by Steve Cerutti),
// shown under our own name and artwork. Signal path, in volts around the 4.5 V reference:
//
//   Input: 0.02u / 510k (~16 Hz) -> 2SC1815 emitter follower -> 1u BP into 10k (~16 Hz)
//   CLIPPING STAGE (JRC4558, non-inverting):
//       ground leg 4.7k + 0.047u  -> gain only above ~720 Hz (the famous mid hump / tight bass)
//       feedback  51k + DRIVE (500k audio) || 51 pF || two anti-parallel 1N914 diodes
//       Ideal op-amp: the ground-leg current i = (Vin - Vc)/4.7k flows through the feedback network,
//       so  i = Vf/Rf + Cf dVf/dt + 2 Is sinh(Vf / n Vt)  (solved per sample, backward Euler + Newton)
//       and Vout = Vin + Vf. The clean signal is always added back -> the soft, "amp-like" clipping.
//   TONE STAGE: 1k into (10k to Vr || 0.22u) = ~723 Hz low-pass, then the 4558 active treble control:
//       TONE (20k, reverse-log feel) between the + and - inputs, wiper to ground via 0.22u + 220R, 1k feedback.
//       Exact transfer function from nodal analysis (see toneCoeffs), re-derived as the knob moves.
//   LEVEL: 1u BP / 1k into B100k (linear, as drawn) -> 2SC1815 output buffer -> 100R / 10u / 10k.
class ValveHowler : public PedalBase
{
public:
    juce::String getId() const override { return "ts"; }
    juce::String getName() const override { return "Valve Howler"; }
    PedalCategory getCategory() const override { return PedalCategory::Overdrive; }
    juce::String getBasedOn() const override { return "Ibanez TS-808 Tube Screamer (late 1970s, JRC4558 op-amp)"; }

    // Tooltip text for each control (the editor adds the control's name on top).
    juce::String describe(const juce::String& id) const override
    {
        if (id == "ts_bypass") return "Footswitch: turns Valve Howler on or off.";
        if (id == "ts_drive") return "Amount of overdrive. Low = clean boost with a mid hump, high = smooth, singing overdrive.";
        if (id == "ts_tone") return "Treble: down = warm and smooth, up = brighter and more cutting.";
        if (id == "ts_level") return "Output level of the pedal. Turn up with low Drive to push an amp or another pedal.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"ts_bypass",1}, "Valve Howler Bypass", false));
        l.add(std::make_unique<P>(juce::ParameterID{"ts_drive",1}, "Drive", 0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"ts_tone",1},  "Tone",  0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"ts_level",1}, "Level", 0.0f, 1.0f, 0.5f));
        knobIds = { "ts_drive", "ts_tone", "ts_level" };   // left to right on the face
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam = s.getRawParameterValue("ts_bypass");
        drive = s.getRawParameterValue("ts_drive");
        tone  = s.getRawParameterValue("ts_tone");
        level = s.getRawParameterValue("ts_level");
    }
    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 4x
        oversampler->initProcessing(spec.maximumBlockSize);
        T = 1.0 / (sr * 4.0);
        gIn = coeff(16.0, sr * 4.0);          // input coupling (0.02u/510k, 1u/10k)
        gDc = coeff(5.0, sr);                 // output coupling
        rfSm.reset(sr * 4.0, 0.03);
        toneSm.reset(sr, 0.03);
        outGain.reset(sr, 0.03);
        toneSm.setCurrentAndTargetValue(tone != nullptr ? tone->load() : 0.5f);
        setTone(toneSm.getCurrentValue());
        reset();
    }
    void reset() override
    {
        for (auto& c : ch) c = {};
        if (oversampler) oversampler->reset();
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        rfSm.setTargetValue((float) (51000.0 + 500000.0 * audioTaper(drive->load())));
        toneSm.setTargetValue(tone->load());
        const float lv = level->load();
        outGain.setTargetValue(levelScale * lv);              // B100k linear, volts -> digital

        // ---- input buffer + clipping stage, 4x oversampled
        juce::dsp::AudioBlock<float> block(buf);
        auto up = oversampler->processSamplesUp(block);
        for (size_t c = 0; c < up.getNumChannels() && c < ch.size(); ++c)
        {
            auto& s = ch[c];
            auto* x = up.getChannelPointer(c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const double rf = rfSm.getNextValue();
                float v = x[i] * 0.5f;                                  // full scale ~ 0.5 V guitar
                v -= lowpass(s.in, v, gIn);
                // ground leg 4.7k + 0.047u (backward Euler): current that the feedback must carry
                const double iG = ((double) v - s.vc) / (4700.0 + T / 47e-9);
                s.vc += T / 47e-9 * iG;
                const double vf = clipFeedback(s.vf, iG, rf);
                x[i] = rail((float) (v + vf));                         // 4558 on 9 V: ~+/-3.8 V swing
            }
        }
        oversampler->processSamplesDown(block);

        // ---- tone stage, level, output coupling at the base rate
        const int n = buf.getNumSamples();
        float gains[32];
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int len = juce::jmin(32, n - i0);
            if (toneSm.isSmoothing()) { toneSm.skip(len); setTone(toneSm.getCurrentValue()); }
            for (int i = 0; i < len; ++i) gains[i] = outGain.getNextValue();
            for (int c = 0; c < buf.getNumChannels() && c < (int) ch.size(); ++c)
            {
                auto& s = ch[(size_t) c];
                auto* x = buf.getWritePointer(c, i0);
                for (int i = 0; i < len; ++i)
                {
                    // biquad, transposed direct form II (double)
                    const double in = x[i];
                    const double y = b0 * in + s.z1;
                    s.z1 = b1 * in - a1 * y + s.z2;
                    s.z2 = b2 * in - a2 * y;
                    float o = rail((float) y) * gains[i];
                    o -= lowpass(s.dc, o, gDc);
                    x[i] = o;
                }
            }
        }
    }

    // Analog tone-stage transfer function H(s) = (n0 + n1 s) / (d0 + d1 s + d2 s^2), exposed for tests.
    // Nodes: A = + input (1k from the clipper, 10k to Vr, 0.22u to Vr), B = - input (= A, ideal op-amp),
    // W = pot wiper (to ground via 0.22u + 220R). Ra = pot A..W, Rb = pot W..B, 1k feedback.
    // Brightest (CW) = wiper at the B end.
    static void toneAnalog(double t, double& n0, double& n1, double& d0, double& d1, double& d2)
    {
        constexpr double R7 = 1000.0, R10k = 10000.0, C5 = 0.22e-6, C6 = 0.22e-6, R220 = 220.0, Rfb = 1000.0, Pot = 20000.0;
        const double ra = 50.0 + Pot * t, rb = 50.0 + Pot * (1.0 - t);   // ~50R end resistance
        const double G1 = 1.0 / R7, Gr = 1.0 / R10k, Ga = 1.0 / ra, Gb = 1.0 / rb;
        const double p0 = Ga + Gb, p1 = (Ga + Gb) * C6 * R220 + C6;     // P(s) = (Ga+Gb)(1 + s C6 R220) + s C6
        n0 = G1 * p0;
        n1 = G1 * (p1 + Rfb * Gb * C6);
        d0 = (G1 + Gr) * p0;
        d1 = (G1 + Gr) * p1 + C5 * p0 + Ga * C6;
        d2 = C5 * p1;
    }

private:
    struct Ch { float in = 0, dc = 0; double vc = 0, vf = 0, z1 = 0, z2 = 0; };
    std::array<Ch, 2> ch;
    double sr = 48000.0, T = 1.0 / 192000.0;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float gIn = 0, gDc = 0;
    juce::SmoothedValue<float> rfSm, toneSm, outGain;
    std::atomic<float> *drive{}, *tone{}, *level{};
    static constexpr float levelScale = 1.1f;    // volts -> digital; calibrated: ~-20 dBFS at noon like the other pedals
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    static float coeff(double fc, double fs) { auto g = std::tan(juce::MathConstants<double>::pi * fc / fs); return (float) (g / (1.0 + g)); }
    static float lowpass(float& s, float in, float g) { float v = (in - s) * g; float y = v + s; s = y + v; return y; }
    static double audioTaper(float x) { return (std::pow(10.0, 2.0 * (double) x) - 1.0) / 99.0; }
    static float rail(float v) { return 3.8f * std::tanh(v / 3.8f); }

    // Feedback network of the clipping stage: Rf || 51 pF || 1N914 pair, driven by current iG.
    // iG = Vf/Rf + Cf (Vf - Vf_prev)/T + 2 Is sinh(Vf / nVt)
    double clipFeedback(double& vf, double iG, double rf) const
    {
        constexpr double Cf = 51e-12, Is = 2.52e-9, nVt = 1.752 * 0.02585;
        const double cT = Cf / T, vPrev = vf;
        double x = vf;
        for (int it = 0; it < 6; ++it)
        {
            const double a = juce::jlimit(-40.0, 40.0, x / nVt);
            const double e = std::exp(a), ei = 1.0 / e;
            const double f  = x / rf + cT * (x - vPrev) + Is * (e - ei) - iG;
            const double fp = 1.0 / rf + cT + Is / nVt * (e + ei);
            const double step = f / fp;
            x -= juce::jlimit(-0.2, 0.2, step);                 // damped Newton: stays stable on big jumps
            if (std::abs(step) < 1e-5) break;
        }
        vf = x;
        return x;
    }

    void setTone(float t)
    {
        double n0, n1, d0, d1, d2;
        // The treble lift only happens when the wiper is close to the "-" end, so with a linear taper
        // nearly all the action is in the last few degrees. A reverse-log (C) taper spreads it evenly.
        toneAnalog(1.0 - audioTaper(1.0f - t), n0, n1, d0, d1, d2);
        const double K = 2.0 * sr;                                     // bilinear transform
        const double A0 = d0 + d1 * K + d2 * K * K;
        b0 = (n0 + n1 * K) / A0;
        b1 = (2.0 * n0) / A0;
        b2 = (n0 - n1 * K) / A0;
        a1 = (2.0 * d0 - 2.0 * d2 * K * K) / A0;
        a2 = (d0 - d1 * K + d2 * K * K) / A0;
    }
};
