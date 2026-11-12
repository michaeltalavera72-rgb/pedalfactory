#pragma once
#include "PedalBase.h"

// Buzzsaw: a model of the 1983 Boss HM-2 "Heavy Metal" circuit (shown under our own name).
// Built from the HM-2 service notes schematic. Signal path, in volts, oversampled 4x:
//
//   Q1 buffer -> C6/R19 high-pass (~154 Hz)
//   Q6  shunt-feedback NPN stage, gain ~21 (R16 470k / R19 22k), C10 roll-off ~3.4 kHz.
//       DISTORTION (VR4 250k) is not in the feedback loop: it loads the collector through
//       R49 150R + C31 10uF. Low settings cut mids/highs but leave the bass at full gain
//       (a first-order shelf), so low DIST sounds thick and bassy.
//   C11/R22 high-pass -> Q7 PNP stage, gain ~21 (R29 470k / R22 22k), C14 roll-off ~3.4 kHz
//   Op-amp 1b: x5.7 above ~72 Hz (R20 220k / R42 47k + C22), with D3 one way and D4+D5 the
//       other in its feedback -> asymmetric soft clipping, C9 roll-off ~7 kHz.
//   R23 -> D6/D7 anti-parallel IN SERIES (dead zone: small signals are blocked, the HM-2 gate /
//       sputter on decaying notes) -> R30 -> D8/D9 to ground (hard clip) + C16.
//   Op-amp 3a "COLOR MIX": two gyrator band EQs. LOW 87 Hz, HIGH = two resonators at 958 Hz and
//       1.28 kHz on one pot. Up to about +/-21 dB. The peaking filters below were fitted to a
//       nodal analysis of the real network (matches the service-note spec: 90 Hz / 950 Hz /
//       1.3 kHz, +21 dB, flat at centre) - see Tools/Analysis/hm2_colormix.py.
//   LEVEL (VR1 10k audio taper) -> output buffer.
class Buzzsaw : public PedalBase
{
public:
    juce::String getId() const override { return "hm"; }
    juce::String getName() const override { return "Buzzsaw"; }
    PedalCategory getCategory() const override { return PedalCategory::Distortion; }
    juce::String getBasedOn() const override { return "Boss HM-2 Heavy Metal (1983, made in Japan)"; }

    // Tooltip text for each control (the editor adds the control's name on top).
    juce::String describe(const juce::String& id) const override
    {
        if (id == "hm_bypass") return "Footswitch: turns Buzzsaw on or off.";
        if (id == "hm_level") return "Output level of the pedal.";
        if (id == "hm_low") return "COLOR MIX low band around 90 Hz, about +/-21 dB. Noon = flat. Up = huge, chainsaw low end.";
        if (id == "hm_high") return "COLOR MIX high band around 1 kHz, about +/-21 dB. Noon = flat. Up = the classic buzzsaw snarl.";
        if (id == "hm_dist") return "Amount of distortion. Low settings stay thick and bassy; high = full HM-2 saturation.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"hm_bypass",1}, "Buzzsaw Bypass", false));
        l.add(std::make_unique<P>(juce::ParameterID{"hm_level",1}, "Level", 0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"hm_low",1},   "Low",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"hm_high",1},  "High",  0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"hm_dist",1},  "Dist",  0.0f, 1.0f, 0.6f));
        knobIds = { "hm_level", "hm_low", "hm_high", "hm_dist" };     // left to right on the face
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam = s.getRawParameterValue("hm_bypass");
        level = s.getRawParameterValue("hm_level");
        low   = s.getRawParameterValue("hm_low");
        high  = s.getRawParameterValue("hm_high");
        dist  = s.getRawParameterValue("hm_dist");
    }
    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 4x
        oversampler->initProcessing(spec.maximumBlockSize);
        const double fs = sr * 4.0;
        gIn   = coeff(154.0, fs);     // C6 0.047u / R19 22k
        gQ6Lp = coeff(3390.0, fs);    // R16 470k / C10 100p
        gMid  = coeff(154.0, fs);     // C11 0.047u / R22 22k
        gQ7Lp = coeff(3390.0, fs);    // R29 470k / C14 100p
        g1bHp = coeff(72.0, fs);      // C22 0.047u / R42 47k
        g1bLp = coeff(7230.0, fs);    // R20 220k / C9 100p
        gClipLp = coeff(10000.0, fs); // R23/R30 10k + C16 1n (diodes conducting)
        gDc   = coeff(8.0, sr);       // output coupling
        lastDist = -1.0f; lastLow = lastHigh = -1.0f;
        outGain.reset(sr, 0.03);                                // LEVEL glides over 30 ms
        outGain.setCurrentAndTargetValue(level != nullptr ? levelGain(level->load()) : 0.0f);
        reset();
    }
    void reset() override
    {
        for (auto& c : ch) c = {};
        for (auto& b : eq) b.reset();
        if (oversampler) oversampler->reset();
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        updateDist(dist->load());
        updateEq(low->load(), high->load());
        outGain.setTargetValue(levelGain(level->load()));

        juce::dsp::AudioBlock<float> block(buf);
        auto up = oversampler->processSamplesUp(block);
        for (size_t c = 0; c < up.getNumChannels() && c < ch.size(); ++c)
        {
            auto& s = ch[c];
            auto* x = up.getChannelPointer(c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                float v = x[i] * 0.5f;                              // full scale ~ 0.5 V guitar
                v -= lowpass(s.in, v, gIn);                         // C6/R19

                // Q6: gain 21 at low frequencies, 21*Rl/(Rl+R17) above the C31 corner
                const float lp = lowpass(s.dist, v, gDistLp);
                v = 21.0f * (shelfHf * v + (1.0f - shelfHf) * lp);
                v = lowpass(s.q6, v, gQ6Lp);
                v = transistor(v, 3.9f, 3.4f);                      // collector swing limits (9 V rail)

                v -= lowpass(s.mid, v, gMid);                       // C11/R22
                v = lowpass(s.q7, 21.0f * v, gQ7Lp);
                v = transistor(v, 3.6f, 4.0f);                      // PNP stage clips the other way round

                // op-amp 1b: unity at DC, x5.7 above 72 Hz, feedback diodes: D3 (0.6 V) / D4+D5 (1.2 V)
                const float hp = v - lowpass(s.hp1b, v, g1bHp);
                float fb = 4.68f * hp;
                fb = fb > 0.0f ? diode(fb, 0.62f) : -diode(-fb, 1.20f);
                fb = lowpass(s.lp1b, fb, g1bLp);
                v = juce::jlimit(-4.2f, 4.2f, v + fb);             // op-amp rails at 9 V

                // D6/D7 in series: dead zone; D8/D9 to ground: hard clip; C16 smoothing
                v = deadZone(v, 0.55f);
                v = 0.62f * hardKnee(v / 0.62f);
                v = lowpass(s.clip, v, gClipLp);

                x[i] = v;
            }
        }
        oversampler->processSamplesDown(block);

        // COLOR MIX + LEVEL at the base rate (linear, after the clipping)
        for (int c = 0; c < buf.getNumChannels() && c < (int) ch.size(); ++c)
        {
            auto* x = buf.getWritePointer(c);
            auto& s = ch[(size_t) c];
            auto g = outGain;                                   // same ramp on every channel
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                float v = x[i];
                v = eq[(size_t) c * 3 + 0].processSingleSampleRaw(v);
                v = eq[(size_t) c * 3 + 1].processSingleSampleRaw(v);
                v = eq[(size_t) c * 3 + 2].processSingleSampleRaw(v);
                v -= lowpass(s.dc, v, gDc);
                x[i] = v * g.getNextValue();
            }
        }
        outGain.skip(buf.getNumSamples());
    }

private:
    struct Ch { float in = 0, dist = 0, q6 = 0, mid = 0, q7 = 0, hp1b = 0, lp1b = 0, clip = 0, dc = 0; };
    std::array<Ch, 2> ch;
    std::array<juce::IIRFilter, 6> eq;    // [channel * 3 + band]: LOW, HIGH 958 Hz, HIGH 1.28 kHz
    double sr = 48000.0;
    float gIn = 0, gQ6Lp = 0, gMid = 0, gQ7Lp = 0, g1bHp = 0, g1bLp = 0, gClipLp = 0, gDc = 0;
    float gDistLp = 0, shelfHf = 1;
    float lastDist = -1.0f, lastLow = -1.0f, lastHigh = -1.0f;
    juce::SmoothedValue<float> outGain;
    static float levelGain(float lv) { return 0.55f * lv * lv; }   // 10k "A" (audio) taper; ~-22 dBFS flat / ~-5 dBFS chainsaw at noon
    std::atomic<float> *level{}, *low{}, *high{}, *dist{};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    static float coeff(double fc, double fs) { auto g = std::tan(juce::MathConstants<double>::pi * fc / fs); return (float) (g / (1.0 + g)); }
    static float lowpass(float& s, float in, float g) { float v = (in - s) * g; float y = v + s; s = y + v; return y; }

    // soft transistor saturation with different limits for the two directions
    static float transistor(float v, float posLim, float negLim)
    {
        return v >= 0.0f ? posLim * std::tanh(v / posLim) : negLim * std::tanh(v / negLim);
    }
    // silicon diode(s) in a feedback loop: almost linear below the knee, then flattens hard
    static float diode(float v, float knee)
    {
        const float t = v / knee;
        const float t2 = t * t; return knee * t / std::sqrt(std::sqrt(1.0f + t2 * t2));
    }
    static float hardKnee(float t) { const float t2 = t * t; return t / std::cbrt(std::sqrt(1.0f + t2 * t2 * t2)); }
    // two anti-parallel diodes in series with the signal: nothing passes until |v| ~ 0.55 V
    static float deadZone(float v, float vt)
    {
        const float a = std::abs(v);
        const float out = a - vt * std::tanh(a / vt);
        return v >= 0.0f ? out : -out;
    }

    void updateDist(float d)
    {
        if (juce::exactlyEqual(d, lastDist)) return;
        lastDist = d;
        // VR4 250k (D taper ~ logarithmic) in series with R49 150R, AC-coupled by C31 10uF
        const double rPot = 250000.0 * (std::pow(1000.0, (double) d) - 1.0) / 999.0;
        const double rl = 150.0 + rPot, r17 = 10000.0, c31 = 10e-6;
        shelfHf = (float) (rl / (rl + r17));
        gDistLp = coeff(1.0 / (juce::MathConstants<double>::twoPi * (rl + r17) * c31), sr * 4.0);
    }

    // Fitted to the nodal analysis of the HM-2 COLOR MIX network; index = |pot - centre| * 2 * 10
    static constexpr float Lf0[11] = { 70.864f, 70.864f, 71.145f, 71.553f, 72.127f, 72.900f, 73.918f, 75.261f, 77.076f, 79.635f, 83.496f };
    static constexpr float Lg [11] = { 0.000f, 0.619f, 1.260f, 1.952f, 2.730f, 3.644f, 4.771f, 6.251f, 8.371f, 11.888f, 20.379f };
    static constexpr float LQ [11] = { 0.333f, 0.333f, 0.338f, 0.349f, 0.365f, 0.390f, 0.424f, 0.477f, 0.562f, 0.726f, 1.269f };
    static constexpr float Hg [11] = { 0.000f, 0.331f, 0.670f, 1.037f, 1.452f, 1.943f, 2.554f, 3.367f, 4.548f, 6.542f, 11.544f };
    static constexpr float HQ [11] = { 0.198f, 0.198f, 0.181f, 0.180f, 0.185f, 0.195f, 0.212f, 0.239f, 0.282f, 0.364f, 0.630f };

    static float lookup(const float* t, float u)
    {
        const float p = juce::jlimit(0.0f, 10.0f, u * 10.0f);
        const int i = juce::jmin(9, (int) p);
        return t[i] + (t[i + 1] - t[i]) * (p - (float) i);
    }
    // Knob -> pot position. The original "G" taper isn't documented; a gentle S-curve spreads the
    // boost over the knob travel (a linear pot does almost nothing until the last quarter).
    static float potFromKnob(float knob)
    {
        const float x = juce::jlimit(-1.0f, 1.0f, 2.0f * knob - 1.0f);
        return 0.5f + 0.5f * (x < 0 ? -1.0f : 1.0f) * std::pow(std::abs(x), 0.6f);
    }

    void updateEq(float lowKnob, float highKnob)
    {
        if (juce::exactlyEqual(lowKnob, lastLow) && juce::exactlyEqual(highKnob, lastHigh)) return;
        lastLow = lowKnob; lastHigh = highKnob;
        const float kL = potFromKnob(lowKnob), kH = potFromKnob(highKnob);
        const float uL = std::abs(kL - 0.5f) * 2.0f, uH = std::abs(kH - 0.5f) * 2.0f;
        const float sL = kL >= 0.5f ? 1.0f : -1.0f, sH = kH >= 0.5f ? 1.0f : -1.0f;
        const auto cl  = juce::IIRCoefficients::makePeakFilter(sr, lookup(Lf0, uL), lookup(LQ, uL),
                                                              juce::Decibels::decibelsToGain(sL * lookup(Lg, uL)));
        const auto ch1 = juce::IIRCoefficients::makePeakFilter(sr, 958.0, lookup(HQ, uH),
                                                              juce::Decibels::decibelsToGain(sH * lookup(Hg, uH)));
        const auto ch2 = juce::IIRCoefficients::makePeakFilter(sr, 1278.0, lookup(HQ, uH),
                                                              juce::Decibels::decibelsToGain(sH * lookup(Hg, uH)));
        for (size_t c = 0; c < 2; ++c)
        {
            eq[c * 3 + 0].setCoefficients(cl);
            eq[c * 3 + 1].setCoefficients(ch1);
            eq[c * 3 + 2].setCoefficients(ch2);
        }
    }
};
