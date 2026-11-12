#pragma once
#include "PedalBase.h"

// Twin Vise: a model of the Trace Elliot SMX Dual Compressor (shown under our own name), voiced for guitar.
// Built from Michael's scan of drawing SMXDC/1 (18.3.93); full trace in claude/smx-dualcomp-trace.md, small-signal
// reference in Tools/Analysis/smx_dualcomp.py. TL064 op-amps and an SSM2024 quad OTA-type VCA on a split 9 V battery
// (every op-amp swings about +/-3.5 V around the half supply). A TWO-BAND compressor:
//
//   Input   IC1A x5.68 (220k || 33p over 47k) -> INPUT GAIN (B50K, loaded by R12 22k) -> IC1B inverting 100k || 100p:
//           up to +28 dB. A bicolour LED reads this node (green = signal, red = driven hot; ~2 s peak hold).
//   Split   Low band: unity Sallen-Key low-pass (R19 / R20 6k8), high band: unity Sallen-Key high-pass (C7 / C8,
//           R24 10k, R23 22k), then an inverter (the 2nd-order bands sum with opposite polarity).
//           GUITAR VOICING (Michael: "for guitar only"): C5 22n -> 8n2, C6 33n -> 12n (low-pass 868 Hz -> 2.36 kHz),
//           C7 = C8 47n -> 18n (high-pass 228 Hz -> 595 Hz). The bass original splits about an octave and a half lower.
//   Bands   Each band: an inverting op-amp with 220k || 33p feedback AND an OTA in the feedback path (output -> 33k /
//           220R = /151 -> OTA -> current back into the summing node). More control current = more feedback = less
//           gain. Low band: R27 10k in (x22 = +27 dB uncompressed) behind the LOW COMPRESSION drive gang
//           (R21 / R10 / R22 / R9: x0.049 .. x0.316, so fully left the low band is ~unity and uncompressed);
//           high band: R34 47k in (x4.68 = +13.4 dB uncompressed).
//   Side-chains (feedback type: each detects its own band's OUTPUT): /4.3 divider -> half-wave precision rectifier x67.7
//           (2M2 / 33k, clamps at the rail) -> diode -> RC envelope -> follower -> 10k -> OTA control.
//           Low: LOW COMPRESSION gang 2 sets the side-chain level; attack 22 ms (22k / 1u), release ~120 ms (+100k).
//           High: attack ~1 ms (diode + 1k), release ~70 ms (22k + 47k); HIGH COMPRESSION attenuates the control voltage.
//           With the OTA's gain linear in control current the loop settles near 2:1 with a soft knee; the OTA's
//           tanh input adds a trace of colour, the op-amps clip on fast transients the 22 ms low attack lets through.
//   HI COMP footswitch (left, secondary): off shorts the high envelope cap - the high band passes uncompressed.
//   Mix     E.Q. BALANCE (dual B50K, gangs opposite: CW = more high band) -> 33k + 33k -> IC2C x1.42 -> R58 47k ->
//           OUTPUT LEVEL (B50K) -> OUT.
// Model: 2x oversampled. The band stages are solved per sample (Newton on v/220k + Iabc tanh(v / 151 / 2Vt) = -iin),
// envelopes integrated from the drawn RC values. Every knob CW = more. The OTA's transconductance per volt of control
// (SSM2024 data not at hand) is a behavioural constant, kOta.
class TwinVise : public PedalBase
{
public:
    juce::String getId() const override { return "tv"; }
    juce::String getName() const override { return "Twin Vise"; }
    PedalCategory getCategory() const override { return PedalCategory::Compressor; }
    int widthUnits() const override { return 2; }
    juce::String getBasedOn() const override
    {
        return "Trace Elliot SMX Dual Compressor (two-band bass compressor, re-voiced for guitar)";
    }
    // Only the EFFECT footswitch turns the pedal on; HI COMP just switches the high band's compression.
    bool isActive() const override { return primaryOn(); }
    juce::String primarySwitchLabel() const override { return "EFFECT"; }
    juce::String secondarySwitchId() const override { return "tv_hicomp"; }
    juce::String secondarySwitchLabel() const override { return "HI COMP"; }
    int ledMeter() const override { return meterState.load(); }

    juce::String describe(const juce::String& id) const override
    {
        if (id == "tv_bypass")  return "Right footswitch: turns Twin Vise on or off.";
        if (id == "tv_hicomp")  return "Left footswitch: compression on the HIGH band on or off. Off, the highs (pick attack, "
                                       "string sparkle) pass uncompressed - and louder - while the LOW band keeps compressing.";
        if (id == "tv_gain")    return "INPUT GAIN: level into both compressors (up to +28 dB). The bicolour LED shows it: green "
                                       "with signal, red when you hit it hard. More gain = more compression in both bands.";
        if (id == "tv_low")     return "LOW COMPRESSION: squeeze on the low band (body and low strings, below about 2 kHz). Fully "
                                       "left the band is clean and uncompressed; turning up drives it harder and makes its "
                                       "detector more sensitive - thicker, flatter, more sustain. Slow, smooth attack.";
        if (id == "tv_high")    return "HIGH COMPRESSION: squeeze on the high band (pick attack and sparkle, above about 600 Hz). "
                                       "Fast attack: tames spiky picking and evens out the top end. Fully left = uncompressed.";
        if (id == "tv_balance") return "E.Q. BALANCE: mix of the two bands. Left = more low band (warm, round), right = more high "
                                       "band (bright, articulate); noon = equal.";
        if (id == "tv_level")   return "OUTPUT LEVEL.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"tv_bypass",1}, "Twin Vise Bypass", false));
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"tv_hicomp",1}, "Twin Vise Hi Comp", true));
        l.add(std::make_unique<P>(juce::ParameterID{"tv_gain",1},    "Input Gain", 0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"tv_low",1},     "Low Comp",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"tv_high",1},    "High Comp",  0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"tv_balance",1}, "Balance",    0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"tv_level",1},   "Level",      0.0f, 1.0f, 0.5f));
        knobIds = { "tv_gain", "tv_low", "tv_high", "tv_balance", "tv_level" };
        detentKnobIds = { "tv_balance" };
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam    = s.getRawParameterValue("tv_bypass");
        secondaryParam = s.getRawParameterValue("tv_hicomp");
        gain    = s.getRawParameterValue("tv_gain");
        low     = s.getRawParameterValue("tv_low");
        high    = s.getRawParameterValue("tv_high");
        balance = s.getRawParameterValue("tv_balance");
        level   = s.getRawParameterValue("tv_level");
    }

    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        fs = sr * 2.0;
        dt = 1.0 / fs;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 2x
        oversampler->initProcessing(spec.maximumBlockSize);
        design();
        for (auto* sm : { &gainSm, &lowSm, &highSm, &balSm, &levelSm }) sm->reset(fs, 0.03);
        hiOffDecay = std::exp(-dt / 0.002);
        meterRel = std::exp(-1.0 / (2.2 * sr));
        reset();
    }
    void reset() override
    {
        if (gain != nullptr)
        {
            gainSm.setCurrentAndTargetValue(gain->load()); lowSm.setCurrentAndTargetValue(low->load());
            highSm.setCurrentAndTargetValue(high->load()); balSm.setCurrentAndTargetValue(balance->load());
            levelSm.setCurrentAndTargetValue(level->load());
        }
        for (auto& c : ch) c = Ch {};
        meterEnv = 0.0; meterState.store(0);
        if (oversampler) oversampler->reset();
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        gainSm.setTargetValue(gain->load()); lowSm.setTargetValue(low->load()); highSm.setTargetValue(high->load());
        balSm.setTargetValue(balance->load()); levelSm.setTargetValue(level->load());

        juce::dsp::AudioBlock<float> block(buf);
        auto up = oversampler->processSamplesUp(block);
        const size_t nCh = juce::jmin(up.getNumChannels(), ch.size());
        const size_t n = up.getNumSamples();
        double peak = 0.0;
        const bool hiOnNow = secondaryOn();
        for (size_t i = 0; i < n; ++i)
        {
            // pots (all scalar: the filters themselves never change)
            const double g = gainSm.getNextValue(), p = lowSm.getNextValue(), h = highSm.getNextValue();
            const double b = balSm.getNextValue(), v = levelSm.getNextValue();
            if (! (juce::exactlyEqual(g, pc.g) && juce::exactlyEqual(p, pc.p) && juce::exactlyEqual(h, pc.h) && juce::exactlyEqual(b, pc.b) && juce::exactlyEqual(v, pc.v)))
            {
                pc.g = g; pc.p = p; pc.h = h; pc.b = b; pc.v = v;
                pc.inGain = inputGain(g); pc.drive = lowDrive(p);
                pc.balLo = loadedPot(1.0 - b, 33e3); pc.balHi = loadedPot(b, 33e3);
                // HIGH COMPRESSION: Thevenin of the pot (h of the envelope, h(1-h) 50k) into R40 10k
                pc.hThev = h / (h * (1.0 - h) * 50e3 + 10e3);
                pc.out = 50e3 * v / 97e3 * (47.0 / 33.0) * levelScale;
            }
            const double inGain = pc.inGain, drive = pc.drive, balLo = pc.balLo, balHi = pc.balHi, hThev = pc.hThev, out = pc.out;
            for (size_t c = 0; c < nCh; ++c)
            {
                auto& s = ch[c];
                float* x = up.getChannelPointer(c);
                // ---- input stages
                double u = (double) x[i] * 0.5;                           // full scale ~ 0.5 V guitar
                u = softRail(ic1a.process(u, s.ic1a), rail);
                u = softRail(lp1(s.ic1b, -inGain * u, gIc1b), rail);      // IC1B (100k || 100p)
                peak = juce::jmax(peak, std::abs(u));
                // ---- low band: LP split, drive network, IC2A + OTA1 in the feedback path
                const double lo = lpSplit.process(u, s.lp) * drive;
                const double iabcL = kOta * juce::jmax(s.envL, 0.0) / 10e3;
                double vl = solveBand(s.vl, lo / 10e3, iabcL);
                s.vl = vl;
                vl = softRail(lp1(s.c9, vl, gC9), rail);
                // ---- high band: HP split, inverter, IC2D + OTA2
                const double hiIn = -hpSplit.process(u, s.hp);
                const double iabcH = kOta * juce::jmax(s.envH, 0.0) * hThev;
                double vh = solveBand(s.vh, hiIn / 47e3, iabcH);
                s.vh = vh;
                vh = softRail(lp1(s.c12, vh, gC12), rail);
                // ---- side-chains (the band outputs, before E.Q. BALANCE)
                envelopeLow(s, p * vl * (10.0 / 43.0));
                const double scH = vh - lp1(s.scHp, vh, gScHp);           // C14 100n into 33k + 10k
                envelopeHigh(s, scH * (10.0 / 43.0), hiOnNow);
                // ---- mixer (inverting), OUTPUT LEVEL
                const double mix = softRail(-(vl * balLo + vh * balHi), rail);
                double o = mix * out;
                o -= lp1(s.dc, o, gDc);
                x[i] = (float) o;
            }
        }
        oversampler->processSamplesDown(block);

        // input-level LED: ~2 s peak hold (C15 10u / R7 220k); green with signal, red when driven hot
        for (int k = 0; k < buf.getNumSamples(); ++k) meterEnv *= meterRel;
        meterEnv = juce::jmax(meterEnv, peak);
        meterState.store(meterEnv > meterRed ? 2 : meterEnv > meterGreen ? 1 : 0);
    }

    // smooth op-amp output limit: linear up to ~rail, then bends over (k = 4) (exposed for tests)
    static double softRail(double v, double r)
    {
        const double q = v / r, q4 = q * q * q * q;
        return v / std::sqrt(std::sqrt(1.0 + q4));
    }
    // test hooks: the envelope (control) voltages of channel 0
    double envLow() const { return ch[0].envL; }
    double envHigh() const { return ch[0].envH; }

private:
    struct Bq   // bilinear 2nd order section, transposed direct form II
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        struct St { double z1 = 0, z2 = 0; };
        // H(s) = (B2 s^2 + B1 s + B0) / (A2 s^2 + A1 s + A0), prewarped at fw
        void design(double B2, double B1, double B0, double A2, double A1, double A0, double fs, double fw)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * fw;
            const double k = w / std::tan(w / (2.0 * fs)), k2 = k * k;
            const double a0 = A2 * k2 + A1 * k + A0;
            b0 = (B2 * k2 + B1 * k + B0) / a0; b1 = (2.0 * B0 - 2.0 * B2 * k2) / a0; b2 = (B2 * k2 - B1 * k + B0) / a0;
            a1 = (2.0 * A0 - 2.0 * A2 * k2) / a0; a2 = (A2 * k2 - A1 * k + A0) / a0;
        }
        double process(double x, St& s) const
        {
            const double y = b0 * x + s.z1; s.z1 = b1 * x - a1 * y + s.z2; s.z2 = b2 * x - a2 * y; return y;
        }
    };
    struct Ch
    {
        Bq::St ic1a, lp, hp;
        double ic1b = 0, c9 = 0, c12 = 0, scHp = 0, dc = 0;
        double vl = 0, vh = 0, envL = 0, envH = 0;
    };
    std::array<Ch, 2> ch;
    Bq ic1a, lpSplit, hpSplit;
    double sr = 48000.0, fs = 96000.0, dt = 1.0 / 96000.0;
    double gIc1b = 0, gC9 = 0, gC12 = 0, gScHp = 0, gDc = 0;
    juce::SmoothedValue<double> gainSm, lowSm, highSm, balSm, levelSm;
    double hiOffDecay = 0.98;
    struct { double g = -1, p = -1, h = -1, b = -1, v = -1, inGain = 0, drive = 0, balLo = 0, balHi = 0, hThev = 0, out = 0; } pc;
    std::atomic<float> *gain{}, *low{}, *high{}, *balance{}, *level{};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    double meterEnv = 0.0, meterRel = 0.999;
    std::atomic<int> meterState { 0 };

    static constexpr double rail = 3.5;                 // TL064 swing on the split 9 V battery
    static constexpr double otaA = 151.0 * 2.0 * 0.02585;   // OTA input attenuator (33k / 220R) x 2 Vt: tanh scale in volts
    // SSM2024 transconductance per (Ic / 2 Vt) - its data isn't at hand; 4 gives ~2:1 at noon in the playing range and up to
    // ~29 dB of gain reduction (1 = a light ~1.4:1). Tuned by ear.
    static constexpr double kOta = 4.0;
    static constexpr double vDiode = 0.55;
    static constexpr double scRail = 3.5;               // rectifier op-amp swing
    static constexpr double meterGreen = 0.10, meterRed = 2.2;   // volts at the IC1B output
    static constexpr double levelScale = 4.8;           // volts -> digital; calibrated: same loudness as the Valve Howler at noon (-18 dBFS 440 Hz sine in)

    void design()
    {
        // IC1A: 1 + Rf / (Rg (1 + s Rf Cf)), Rf 220k, Cf 33p, Rg 47k
        { const double Rf = 220e3, Cf = 33e-12, Rg = 47e3;
          ic1a.design(0.0, Rg * Rf * Cf, Rg + Rf, 0.0, Rg * Rf * Cf, Rg, fs, 1000.0); }
        // guitar-voiced split filters (see the header)
        { const double R = 6.8e3, C5 = 8.2e-9, C6 = 12e-9;
          lpSplit.design(0.0, 0.0, 1.0, R * R * C5 * C6, 2.0 * R * C5, 1.0, fs, 2360.0); }
        { const double R24 = 10e3, R23 = 22e3, C = 18e-9;
          const double A2 = R24 * R23 * C * C;
          hpSplit.design(A2, 0.0, 0.0, A2, R24 * 2.0 * C, 1.0, fs, 595.0); }
        gIc1b = coeff(1.0 / (2.0 * juce::MathConstants<double>::pi * 100e3 * 100e-12), fs);
        gC9 = gC12 = coeff(1.0 / (2.0 * juce::MathConstants<double>::pi * 220e3 * 33e-12), fs);
        gScHp = coeff(1.0 / (2.0 * juce::MathConstants<double>::pi * 43e3 * 100e-9), fs);
        gDc = coeff(5.0, fs);
    }

    // INPUT GAIN pot (wiper fraction g from 0 V) loaded by R12 22k into IC1B's virtual earth, times 100k
    static double inputGain(double g)
    {
        const double r1 = juce::jmax((1.0 - g) * 50e3, 1.0), r2 = juce::jmax(g * 50e3, 1.0);
        return (r2 / (r1 + r2)) * 100e3 / (22e3 + r1 * r2 / (r1 + r2));
    }
    // LOW COMPRESSION drive gang: voltage at node X per volt out of the low-pass (Tools/Analysis/smx_dualcomp.py low_drive)
    static double lowDrive(double p)
    {
        auto par = [](double a, double b) { return a * b / (a + b); };
        const double up = juce::jmax((1.0 - p) * 50e3, 1.0), lo = p * 50e3, zx = 5e3;
        const double rax = par(220e3, up);
        const double za = par(rax + zx, lo + 10e3);
        return za / (10e3 + za) * zx / (rax + zx);
    }
    // a 50k pot at fraction x, wiper loaded by rl
    static double loadedPot(double x, double rl)
    {
        const double r1 = juce::jmax((1.0 - x) * 50e3, 1.0), r2 = juce::jmax(x * 50e3, 1.0);
        const double r2l = r2 * rl / (r2 + rl);
        return r2l / (r1 + r2l);
    }

    // band stage: v / 220k + Iabc tanh(v / otaA) = -iin  (monotonic in v: Newton from the previous sample)
    static double solveBand(double v, double iin, double iabc)
    {
        constexpr double gf = 1.0 / 220e3;
        for (int it = 0; it < 8; ++it)
        {
            const double t = std::tanh(v / otaA);
            const double F = v * gf + iabc * t + iin;
            const double dF = gf + iabc * (1.0 - t * t) / otaA;
            const double dv = F / dF;
            v -= juce::jlimit(-2.0, 2.0, dv);
            if (std::abs(dv) < 1e-5) break;
        }
        return v;
    }
    // precision half-wave rectifier x67.7 (2M2 / 33k), clamped at the op-amp swing
    static double rectify(double x) { return juce::jmin(juce::jmax(x, 0.0) * 67.7, scRail); }

    // low envelope: D10 -> (R52 100k to 0 V) -> R54 22k -> C20 1u
    void envelopeLow(Ch& s, double x) const
    {
        const double vr = juce::jmax(rectify(x) - vDiode, s.envL * (100.0 / 122.0));
        s.envL += dt * (vr - s.envL) / (22e3 * 1e-6);
    }
    // high envelope: D6 -> node D (R43 47k to 0 V) -> (D5 + R48 1k) || R49 22k -> C19 1u; HI COMP off shorts C19
    void envelopeHigh(Ch& s, double x, bool hiOn) const
    {
        if (! hiOn) { s.envH *= hiOffDecay; return; }       // switch closed: C19 shorted (discharges over ~2 ms)
        const double vd = juce::jmax(rectify(x) - vDiode, s.envH * (47.0 / 69.0));
        const double i = (vd - s.envH) / 22e3 + juce::jmax(vd - vDiode - s.envH, 0.0) / 1e3;
        s.envH = juce::jmax(s.envH + dt * i / 1e-6, 0.0);
    }

    static double coeff(double fc, double f) { const double g = std::tan(juce::MathConstants<double>::pi * juce::jmin(fc, f * 0.45) / f); return g / (1.0 + g); }
    static double lp1(double& s, double in, double g) { const double v = (in - s) * g; const double y = v + s; s = y + v; return y; }
};
