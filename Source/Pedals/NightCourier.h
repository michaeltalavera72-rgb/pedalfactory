#pragma once
#include "PedalBase.h"
#include <vector>

// Night Courier: a model of the EarthQuaker Devices Dispatch Master digital delay + reverb (shown under our own
// name and artwork). From schematics/Earthquake Devices/EQD-Dispatch-Master.pdf:
//
//   Analog side (TL074): input buffer -> the DRY signal always goes straight to the output summer at unity;
//   the wet return comes back through the MIX pot (5k) and is added on top (so MIX never thins the dry signal).
//   Wet side: 2-pole RC filters into and out of a Spin FV-1 DSP running at 3.3 V; its program (EEPROM) reads
//   TIME / REPEATS / REVERB from POT0-POT2. The algorithm itself is firmware, so it is modelled by behaviour:
//
//   - DELAY: 40 ms .. 1 s (exponential knob). The delay time glides when TIME moves (FV-1 pot filtering), so
//     turning it while repeats ring bends their pitch. REPEATS = feedback; just past 3 o'clock it reaches unity
//     and the loop runs away into soft saturation (a self-oscillating wash, like the real one at max).
//     Each repeat is darker (feedback low-pass ~4.5 kHz, high-pass 60 Hz).
//   - REVERB: the dry signal plus the echoes feed a plate-style tank (Dattorro topology, the classic FV-1 plate
//     structure: 4 input diffusers, two cross-coupled decay branches with modulated allpasses and damping).
//     The knob sets the reverb level and lengthens the decay a little.
//   - FV-1 character: ~32 kHz internal rate -> wet path band-limited to ~12 kHz; delay memory stored as
//     short floating-point words (10-bit mantissa) -> a faint grain on long repeats.
//   EXTRAS (Michael's artwork has them; at their defaults the pedal is the original):
//   - MOD (default 0 = off): tape-style wobble of the delay time (sine + slow random drift, up to ~+/-4 ms,
//     0.6..1.8 Hz) and a deeper shimmer in the reverb tank.
//   - TONE (centre detent, noon = original): tilts the wet sound - repeats darken faster or slower (feedback
//     low-pass 2..10 kHz), the reverb damping follows, and a +/-9 dB high shelf at 2.5 kHz on the wet output.
//   - DELAY / REVERB / BOTH 3-way toggle (default BOTH = original): delay only, reverb only (on the dry signal),
//     or the delay feeding the reverb.
//   - TRAILS: switching the pedal off stops new signal entering the delay/reverb but lets the echoes and the
//     reverb ring out (the board keeps calling process() while isActive() reports a tail).
class NightCourier : public PedalBase
{
public:
    juce::String getId() const override { return "nc"; }
    juce::String getName() const override { return "Night Courier"; }
    PedalCategory getCategory() const override { return PedalCategory::Modulation; }
    juce::String getBasedOn() const override { return "EarthQuaker Devices Dispatch Master (digital delay + reverb, Spin FV-1)"; }
    bool isTimeBased() const override { return true; }

    juce::String describe(const juce::String& id) const override
    {
        if (id == "nc_bypass")  return "Footswitch: turns Night Courier on or off. Switching off lets the echoes and the reverb ring out.";
        if (id == "nc_time")    return "Delay time, 40 ms (left) to 1 second (right). Turning it while echoes ring bends their pitch.";
        if (id == "nc_repeats") return "Number of echoes: left = a single echo, noon = several, past 3 o'clock = endless, building into "
                                       "a self-oscillating wash.";
        if (id == "nc_mix")     return "Level of the echoes and reverb added to your dry signal (the dry signal always stays at full level).";
        if (id == "nc_reverb")  return "Amount of plate-style reverb on the dry signal and the echoes. Left = no reverb.";
        if (id == "nc_mod")     return "Tape-style wobble on the echoes and a deeper shimmer in the reverb. Fully left = off (the original pedal).";
        if (id == "nc_tone")    return "Tone of the echoes and reverb: left = darker, right = brighter. Noon = the original voicing (double-click).";
        if (id == "nc_mode")    return "Lever up = DELAY only, centre = REVERB only, down = BOTH (the delay feeding the reverb, like the original).";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"nc_bypass",1}, "Night Courier Bypass", false));
        l.add(std::make_unique<P>(juce::ParameterID{"nc_time",1},    "Time",    0.0f, 1.0f, 0.55f));
        l.add(std::make_unique<P>(juce::ParameterID{"nc_repeats",1}, "Repeats", 0.0f, 1.0f, 0.40f));
        l.add(std::make_unique<P>(juce::ParameterID{"nc_mix",1},     "Mix",     0.0f, 1.0f, 0.40f));
        l.add(std::make_unique<P>(juce::ParameterID{"nc_reverb",1},  "Reverb",  0.0f, 1.0f, 0.40f));
        l.add(std::make_unique<P>(juce::ParameterID{"nc_mod",1},     "Mod",     0.0f, 1.0f, 0.0f));
        l.add(std::make_unique<P>(juce::ParameterID{"nc_tone",1},    "Tone",    0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"nc_mode",1}, "Mode",
                                                           juce::StringArray { "Delay", "Reverb", "Both" }, 2));
        knobIds = { "nc_time", "nc_repeats", "nc_mix", "nc_reverb", "nc_mod", "nc_tone" };   // top row, then MOD / TONE
        detentKnobIds = { "nc_tone" };
        toggleId = "nc_mode"; toggleLabel = "Delay / Reverb / Both"; togglePositions = 3;    // 0 = up, 1 = centre, 2 = down
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam = s.getRawParameterValue("nc_bypass");
        time = s.getRawParameterValue("nc_time");
        repeats = s.getRawParameterValue("nc_repeats");
        mix = s.getRawParameterValue("nc_mix");
        reverb = s.getRawParameterValue("nc_reverb");
        mod = s.getRawParameterValue("nc_mod");
        tone = s.getRawParameterValue("nc_tone");
        mode = s.getRawParameterValue("nc_mode");
    }

    // On while switched on, and afterwards until the echoes / reverb have died away (trails).
    bool isActive() const override { return primaryOn() || tail.load(std::memory_order_relaxed); }

    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        echo.setSize((int) std::ceil(sr * (maxTime + 0.05)) + 8);
        const double k = sr / 29761.0;                         // Dattorro's lengths are given at 29.761 kHz
        for (int i = 0; i < 4; ++i) inAp[(size_t) i].setSize(len(inApLen[i], k));
        tankApA.setSize(len(672 + 32, k)); tankApB.setSize(len(908 + 32, k));
        tankD1A.setSize(len(4453, k)); tankAp2A.setSize(len(1800, k)); tankD2A.setSize(len(3720, k));
        tankD1B.setSize(len(4217, k)); tankAp2B.setSize(len(2656, k)); tankD2B.setSize(len(3163, k));
        kScale = k;
        { const int t[14] = { 266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228, 2673, 2111, 335, 121 }; for (int i = 0; i < 14; ++i) tp[i] = len(t[i], k); }
        dl[0] = len(4453, k); dl[1] = len(3720, k); dl[2] = len(4217, k); dl[3] = len(3163, k);
        lfoC = std::cos(juce::MathConstants<double>::twoPi * 0.9 / sr); lfoS = std::sin(juce::MathConstants<double>::twoPi * 0.9 / sr); lfoX = 1.0; lfoY = 0.0;
        bandIn.setCoefficients(juce::IIRCoefficients::makeLowPass(sr, juce::jmin(12000.0, 0.45 * sr), 0.54));
        bandIn2.setCoefficients(juce::IIRCoefficients::makeLowPass(sr, juce::jmin(12000.0, 0.45 * sr), 1.31));
        bandOut.setCoefficients(juce::IIRCoefficients::makeLowPass(sr, juce::jmin(12000.0, 0.45 * sr), 0.71));
        gFbHp = coeff(60.0); gDc = coeff(10.0);
        lastTone = -1.0f; setTone(tone != nullptr ? tone->load() : 0.5f);
        timeSm = targetDelaySamples();
        mixSm.reset(sr, 0.05); revSm.reset(sr, 0.05); fbSm.reset(sr, 0.05); inGate.reset(sr, 0.01);
        modSm.reset(sr, 0.10); echoOn.reset(sr, 0.03); revOn.reset(sr, 0.03);
        modSm.setCurrentAndTargetValue(mod != nullptr ? mod->load() : 0.0f);
        { const int m = modeIndex(); echoOn.setCurrentAndTargetValue(m != 1 ? 1.0f : 0.0f); revOn.setCurrentAndTargetValue(m != 0 ? 1.0f : 0.0f); }
        mixSm.setCurrentAndTargetValue(mix != nullptr ? mix->load() : 0.4f);
        revSm.setCurrentAndTargetValue(reverb != nullptr ? reverb->load() : 0.4f);
        fbSm.setCurrentAndTargetValue(feedbackFor(repeats != nullptr ? repeats->load() : 0.4f));
        inGate.setCurrentAndTargetValue(1.0f);
        reset();
    }
    void clearTank()
    {
        for (auto& a : inAp) a.clear();
        for (Line* d : { (Line*) &tankApA, (Line*) &tankApB, (Line*) &tankD1A, (Line*) &tankAp2A, (Line*) &tankD2A, (Line*) &tankD1B, (Line*) &tankAp2B, (Line*) &tankD2B }) d->clear();
        dampA = dampB = crossA = crossB = 0.0f;
    }
    void reset() override
    {
        echo.clear();
        clearTank(); tankEmpty = true;
        fbLp = fbHp = dc = 0.0f; lfo = 0.0; lfoX = 1.0; lfoY = 0.0; modPhase = 0.0; drift = driftTarget = 0.0f; driftCount = 0;
        shelf.reset();
        bandIn.reset(); bandIn2.reset(); bandOut.reset();
        quietSamples = 0;
        tail.store(false);
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        const bool on = primaryOn();
        mixSm.setTargetValue(mix->load());
        revSm.setTargetValue(reverb->load());
        // Bypassed: the trails must always die away, so the loop gain is held below 1 (REPEATS past ~3 o'clock would
        // otherwise keep self-oscillating - and keep this pedal running - with no way to stop it from the footswitch).
        fbSm.setTargetValue(on ? feedbackFor(repeats->load()) : juce::jmin(feedbackFor(repeats->load()), trailFeedback));
        inGate.setTargetValue(on ? 1.0f : 0.0f);
        modSm.setTargetValue(mod->load());
        { const int m = modeIndex(); echoOn.setTargetValue(m != 1 ? 1.0f : 0.0f); revOn.setTargetValue(m != 0 ? 1.0f : 0.0f); }
        setTone(tone->load());
        const double target = targetDelaySamples();
        const double gTime = 1.0 - std::exp(-1.0 / (0.12 * sr));          // ~120 ms glide (FV-1 pot filter)
        const float exc = (float) (12.0 * kScale);                           // tank allpass modulation depth (samples)
        // Reverb not heard (DELAY-only mode, or REVERB fully left) and its fade finished: don't run the tank.
        // It is emptied once, so it starts clean when the reverb comes back.
        const bool tankOff = (revOn.getTargetValue() <= 0.0f && ! revOn.isSmoothing())
                          || (revSm.getTargetValue() <= 0.0f && ! revSm.isSmoothing());
        if (tankOff && ! tankEmpty) { clearTank(); tankEmpty = true; }
        else if (! tankOff) tankEmpty = false;

        auto* x = buf.getWritePointer(0);
        const int n = buf.getNumSamples();
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float dry = x[i];
            const float g = inGate.getNextValue();
            const float fb = fbSm.getNextValue();
            const float rv = revSm.getNextValue();
            const float wetGain = mixSm.getNextValue();
            const float md = modSm.getNextValue(), eOn = echoOn.getNextValue(), rOn = revOn.getNextValue();

            // ---- into the FV-1: input filters, ADC headroom (~2x the guitar's peak level)
            float in = (float) bandIn2.processSingleSampleRaw(bandIn.processSingleSampleRaw(dry * g));
            in = softClip(in, 2.0f);

            // ---- delay with gliding time and filtered, saturating feedback
            timeSm += (target - timeSm) * gTime;
            // MOD: sine + slow random drift on the read position (tape-style wobble), off at 0
            double wob = 0.0;
            if (md > 0.0005f)
            {
                modPhase += juce::MathConstants<double>::twoPi * (0.6 + 1.2 * md) / sr;
                if (modPhase > juce::MathConstants<double>::twoPi) modPhase -= juce::MathConstants<double>::twoPi;
                if (--driftCount <= 0) { driftTarget = rng.nextFloat() * 2.0f - 1.0f; driftCount = (int) (0.35 * sr); }
                drift += (driftTarget - drift) * gDrift;
                const double depth = sr * 0.001 * (0.15 + 3.6 * std::pow((double) md, 1.5));
                wob = depth * (0.7 * std::sin(modPhase) + 0.3 * drift);
            }
            const float e = echo.readCubic(juce::jmax(2.0, timeSm + wob));
            float loop = e;
            fbLp += (loop - fbLp) * gFbLp; loop = fbLp;                   // darker each repeat
            fbHp += (loop - fbHp) * gFbHp; loop -= fbHp;                  // and a little thinner in the lows
            echo.write(fv1Word(softClip(in + fb * loop, 1.2f)));            // FV-1 saturates at full scale

            // ---- plate reverb on dry + echoes
            float revOut = 0.0f;
            if (! tankOff)
            {
                const float decay = 0.55f + 0.25f * rv;                       // RT60 ~1.5 s (low) .. ~5 s (max)
                float r = 0.5f * (in + e * eOn);
                for (int a = 0; a < 4; ++a) r = inAp[(size_t) a].allpass(r, inApG[a]);
                { const double nx = lfoX * lfoC - lfoY * lfoS, ny = lfoX * lfoS + lfoY * lfoC;      // rotate (cos, sin) by the LFO step
                  const double r2 = 1.5 - 0.5 * (nx * nx + ny * ny); lfoX = nx * r2; lfoY = ny * r2; }   // keep it on the unit circle
                const float ex = exc * (1.0f + 2.0f * md);
                const float mA = ex * (float) lfoY, mB = ex * (float) lfoX;
                // branch A
                float a1 = tankApA.allpassMod(r + crossB * decay, 0.70f, 672.0f * (float) kScale + mA);
                float a2 = tankD1A.delayed(a1, dl[0]);
                dampA += (a2 - dampA) * gDamp;
                float a3 = tankAp2A.allpass(dampA * decay, 0.50f);
                crossA = tankD2A.delayed(a3, dl[1]);
                // branch B
                float b1 = tankApB.allpassMod(r + crossA * decay, 0.70f, 908.0f * (float) kScale + mB);
                float b2 = tankD1B.delayed(b1, dl[2]);
                dampB += (b2 - dampB) * gDamp;
                float b3 = tankAp2B.allpass(dampB * decay, 0.50f);
                crossB = tankD2B.delayed(b3, dl[3]);
                // output taps (Dattorro's left + right, summed to mono)
                revOut = 0.30f * (tankD1B.tap(tp[0]) + tankD1B.tap(tp[1]) - tankAp2B.tap(tp[2])
                                              + tankD2B.tap(tp[3]) - tankD1A.tap(tp[4]) - tankAp2A.tap(tp[5])
                                              - tankD2A.tap(tp[6])
                                              + tankD1A.tap(tp[7]) + tankD1A.tap(tp[8]) - tankAp2A.tap(tp[9])
                                              + tankD2A.tap(tp[10]) - tankD1B.tap(tp[11]) - tankAp2B.tap(tp[12])
                                              - tankD2B.tap(tp[13]));
            }

            // ---- out of the FV-1: echoes + reverb, DAC filter, then MIX adds it to the full dry signal
            if (! juce::exactlyEqual(rv, lastRv)) { lastRv = rv; revLevel = 2.0f * std::pow(rv, 1.3f); }
            float wet = eOn * e + rOn * revLevel * revOut;
            wet = (float) bandOut.processSingleSampleRaw(wet);
            wet = shelf.processSingleSampleRaw(wet);                         // TONE tilt
            dc += (wet - dc) * gDc; wet -= dc;
            const float w = wetGain * wet;
            peak = juce::jmax(peak, std::abs(w), std::abs(e));
            x[i] = dry + w;
        }

        // trails: keep running while bypassed until the tail has decayed below -100 dBFS for half a second
        if (on) { quietSamples = 0; tail.store(true); }
        else
        {
            quietSamples = peak < 1.0e-5f ? quietSamples + n : 0;
            if (quietSamples > (int) (0.5 * sr)) { reset(); tail.store(false); }
        }
    }

private:
    // power-of-two circular buffer with integer, cubic and modulated reads
    struct Line
    {
        std::vector<float> b; int mask = 0, w = 0;
        void setSize(int n) { int s = 1; while (s < n + 4) s <<= 1; b.assign((size_t) s, 0.0f); mask = s - 1; w = 0; }
        void clear() { std::fill(b.begin(), b.end(), 0.0f); w = 0; }
        void write(float v) { b[(size_t) w] = v; w = (w + 1) & mask; }
        float at(int d) const { return b[(size_t) ((w - d) & mask)]; }
        float tap(int d) const { return at(d); }
        float readCubic(double d) const
        {
            const int i = (int) d; const float f = (float) (d - i);
            // Hermite between y0 (delay i) and y1 (delay i + 1); callers keep d >= 2
            const float ym = at(i - 1), y0 = at(i), y1 = at(i + 1), y2 = at(i + 2);
            const float c1 = 0.5f * (y1 - ym), c2 = ym - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym) + 1.5f * (y0 - y1);
            return ((c3 * f + c2) * f + c1) * f + y0;
        }
        float delayed(float in, int d) { const float o = at(d); write(in); return o; }
        float allpass(float in, float g)                     // Schroeder allpass, delay = length
        {
            const float d = at(length);
            const float v = in - g * d; write(v); return d + g * v;
        }
        float allpassMod(float in, float g, float d)
        {
            const int i = (int) d; const float f = d - (float) i;
            const float o = at(i) * (1.0f - f) + at(i + 1) * f;
            const float v = in - g * o; write(v); return o + g * v;
        }
        int length = 1;
    };
    struct Ap : Line { void setSize(int n) { Line::setSize(n); length = n; } };

    Line echo;
    std::array<Ap, 4> inAp;
    Line tankApA, tankApB, tankD1A, tankD1B, tankD2A, tankD2B;
    Ap tankAp2A, tankAp2B;
    static constexpr int inApLen[4] = { 142, 107, 379, 277 };
    static constexpr float inApG[4] = { 0.75f, 0.75f, 0.625f, 0.625f };

    double sr = 48000.0, kScale = 1.0, timeSm = 1000.0, lfo = 0.0;
    float fbLp = 0, fbHp = 0, dampA = 0, dampB = 0, crossA = 0, crossB = 0, dc = 0;
    float gFbLp = 0, gFbHp = 0, gDamp = 0, gDc = 0;
    juce::IIRFilter bandIn, bandIn2, bandOut, shelf;
    juce::SmoothedValue<float> mixSm, revSm, fbSm, inGate, modSm, echoOn, revOn;
    double modPhase = 0.0;
    float drift = 0, driftTarget = 0, gDrift = 0, lastTone = -1.0f;
    int driftCount = 0;
    int tp[14] {}, dl[4] {};
    double lfoC = 1, lfoS = 0, lfoX = 1, lfoY = 0;
    float lastRv = -1.0f, revLevel = 0.0f;
    juce::Random rng { 7 };
    int quietSamples = 0;
    std::atomic<bool> tail { false };
    std::atomic<float> *time{}, *repeats{}, *mix{}, *reverb{}, *mod{}, *tone{}, *mode{};

    int modeIndex() const { return mode != nullptr ? juce::jlimit(0, 2, (int) std::lround(mode->load())) : 2; }
    // TONE: noon = the original voicing (feedback LPF 4.5 kHz, damping 6 kHz, flat shelf)
    void setTone(float t)
    {
        if (std::abs(t - lastTone) < 1.0e-4f) return;
        lastTone = t;
        const double u = (double) t - 0.5;
        gFbLp = coeff(4500.0 * std::pow(2.0, 2.4 * u));
        gDamp = coeff(6000.0 * std::pow(2.0, 2.0 * u));
        gDrift = coeff(1.5);
        shelf.setCoefficients(juce::IIRCoefficients::makeHighShelf(sr, 2500.0, 0.6, juce::Decibels::decibelsToGain((float) (18.0 * u))));
    }

    static constexpr double minTime = 0.040, maxTime = 1.0;
    static constexpr float trailFeedback = 0.90f;                          // highest loop gain while bypassed (trails)
    bool tankEmpty = true;

    static int len(int samples, double k) { return juce::jmax(1, (int) std::lround(samples * k)); }
    int tap(int samples) const { return len(samples, kScale); }
    float coeff(double fc) const { return (float) (1.0 - std::exp(-juce::MathConstants<double>::twoPi * fc / sr)); }
    double targetDelaySamples() const
    {
        const double t = time != nullptr ? (double) time->load() : 0.55;
        return sr * minTime * std::pow(maxTime / minTime, t);                // 40 ms .. 1 s, ~230 ms at noon
    }
    // REPEATS knob -> loop gain: 0 = one echo, noon ~0.55, unity just past 3 o'clock, 1.06 at max (runaway)
    static float feedbackFor(float k) { return 1.06f * std::pow(k, 0.85f); }
    static float softClip(float v, float lim) { return lim * std::tanh(v / lim); }
    // FV-1 delay memory word: short floating point (10-bit mantissa) -> faint grain on quiet, long repeats
    static float fv1Word(float v)
    {
        if (std::abs(v) < 1.0e-7f) return 0.0f;
        int e; const float m = std::frexp(v, &e);
        return std::ldexp(std::round(m * 1024.0f) / 1024.0f, e);
    }
};
