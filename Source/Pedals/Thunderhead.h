#pragma once
#include "PedalBase.h"
#include "SlowBurn.h"      // SlowBurn::Mna, the small nodal-analysis solver shared by the circuit models

// Thunderhead: a model of the Diezel VH4 pedal (the VH4 amp's high-gain channel in a box), shown under our own
// name and artwork. Circuit from schematics/Diezel/Diezel-VH4-Pedal.pdf (EAGLE trace, 2017); the pots are
// off-sheet there, values from the PedalPCB "Valhalla" clone parts list. Write-up: claude/vh4-pedal-trace.md;
// small-signal reference: Tools/Analysis/vh4_reference_response.py. Op-amps on +/-12 V (LM2940-12 + ICL7660S).
//
//   INPUT        22n -> 1M || 1M || 100p, IC1A (OPA2134) unity buffer, 1u / 100k.
//   IC1B         OPA2134 non-inverting, 220k || 100p over 10k + 1u: x23 (27 dB), 16 Hz .. 7.2 kHz.
//   INTERSTAGE   22n / 470k, then 2n2 || 680k into 220k (a treble-lift: lows -12 dB, flat above ~450 Hz),
//                220k -> GAIN (A250k, rheostat to ground) at IC2A "+".
//   IC2A         TL072, 1M || 47p over 10k + 2u2: x101 (40 dB), rolled off from 3.4 kHz. Two 1N5232 (5.6 V)
//                zeners back to back across the feedback: soft-knee clipping at ~ +/-6 V (the Valhalla clone
//                uses two zeners; the trace's D4 symbol is ambiguous).
//   DIODE GATE   4n7 -> anti-parallel 1N4148 || 100k -> 470k to ground: small signals are cut ~2 dB and pass
//                through 100k; big ones snap through the diodes. Adds a little "spit" to the decay.
//   IC2B         TL072, 220k || 100p over 10k + 2u2: x23 -> hard rail clipping (~ +/-10.5 V): the VH4's
//                saturated, tight top end.
//   TONE STACK   Marshall-style: 560p -> TREBLE (B250k) lug 3, 39k slope, 22n to BASS (A1M) and 22n to MIDDLE
//                (B25k rheostat); TREBLE wiper -> 1M5 / 100k (-24 dB) into IC3A "+".
//   IC3A         TL072 "power amp" stage, x3.1 (10k || 1n over 4k7 + 10u) with two more ground legs on "-":
//                PRESENCE (C25k): 1u + 110R + pot -> high-mid lift (up to ~ +24 dB above 1 kHz at full).
//                DEEP (C25k): 2u2 + 1k + pot + a gyrator (IC3B follower, 330n / 56k / 47R = 0.87 H) -> a
//                resonant low boost around 115 Hz (up to ~ +10 dB). Full presence can clip this stage too.
//   OUTPUT       10u -> 1k -> VOLUME (A10k). AMP (toggle up, the pedal's "amp input" jack): wiper -> 150k /
//                100k || 2n2 (-8 dB, rolled off from ~1.2 kHz, a dark front-of-amp voice). DIRECT (toggle down,
//                the "power amp" jack): the volume wiper itself, level-matched to AMP at low frequencies.
//
// Processing: the linear input, IC1B and interstage run at the host rate; IC2A (zener feedback, Newton), the
// diode gate (Newton) and IC2B (rail clip) run 4x oversampled; the tone stack, IC3A and the output network are
// small nodal (MNA) circuits at the host rate.
class Thunderhead : public PedalBase
{
public:
    juce::String getId() const override { return "th"; }
    juce::String getName() const override { return "Thunderhead"; }
    PedalCategory getCategory() const override { return PedalCategory::Distortion; }
    juce::String getBasedOn() const override
    {
        return "Diezel VH4 pedal (the VH4 amp's high-gain channel in a box, with Deep and Presence)";
    }

    // Tooltip text for each control (the editor adds the control's name on top).
    juce::String describe(const juce::String& id) const override
    {
        if (id == "th_bypass") return "Footswitch: turns Thunderhead on or off (red LED = on).";
        if (id == "th_gain") return "How hard the two clipping stages are driven. Low = crunchy, noon = full high-gain saturation, above noon = more compression and sustain. Fully down is almost silent, like the real pot.";
        if (id == "th_deep") return "Resonant low-end boost around 115 Hz in the last stage (up to about +10 dB). Turn up for a bigger, heavier bottom end; down for a tighter sound.";
        if (id == "th_volume") return "Output level of the pedal.";
        if (id == "th_bass") return "Low frequencies in the Marshall-style tone stack.";
        if (id == "th_mid") return "Mid frequencies in the tone stack. Down = scooped, up = punchier and more forward.";
        if (id == "th_treble") return "High frequencies in the tone stack. The bass and middle controls interact with it.";
        if (id == "th_presence") return "Upper-mid / treble lift in the last stage (up to about +24 dB above 1 kHz). Adds bite and cut; near full it also adds a little extra grit.";
        if (id == "th_amp") return "Output voicing. AMP (up): the pedal's darker output for the front of a guitar amp (rolled off above about 1.2 kHz). DIRECT (down): the brighter, unfiltered power-amp output, better into an amp sim, FRFR or a power amp. Same level.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"th_bypass",1}, "Thunderhead Bypass", false));
        l.add(std::make_unique<P>(juce::ParameterID{"th_gain",1},     "Gain",     0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"th_deep",1},     "Deep",     0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"th_volume",1},   "Volume",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"th_bass",1},     "Bass",     0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"th_mid",1},      "Middle",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"th_treble",1},   "Treble",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"th_presence",1}, "Presence", 0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"th_amp",1}, "Amp Output", true,
            juce::AudioParameterBoolAttributes().withStringFromValueFunction([](bool b, int) { return juce::String(b ? "Amp" : "Direct"); })));
        // Top row (big knobs: GAIN / DEEP / VOLUME), then the bottom row (small knobs: the EQ, low to high).
        knobIds = { "th_gain", "th_deep", "th_volume", "th_bass", "th_mid", "th_treble", "th_presence" };
        toggleId = "th_amp"; toggleLabel = "AMP / DIRECT";
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam = s.getRawParameterValue("th_bypass");
        gain     = s.getRawParameterValue("th_gain");
        deep     = s.getRawParameterValue("th_deep");
        volume   = s.getRawParameterValue("th_volume");
        bass     = s.getRawParameterValue("th_bass");
        mid      = s.getRawParameterValue("th_mid");
        treble   = s.getRawParameterValue("th_treble");
        presence = s.getRawParameterValue("th_presence");
        ampOut   = s.getRawParameterValue("th_amp");
    }

    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 4x
        oversampler->initProcessing(spec.maximumBlockSize);
        h = 1.0 / (sr * 4.0);
        gDc = coeff(5.0, sr);
        for (auto* sm : allSmoothers()) sm->reset(sr, 0.03);
        if (gain != nullptr) { targets(); snap(); }
        rebuild(true);
        reset();
    }
    void reset() override
    {
        for (auto& c : ch)
        {
            for (auto* m : { &c.in, &c.inter, &c.stack, &c.amp3, &c.out }) m->clearState();
            c.ic1b.clear(); c.ic2b.clear();
            c.vc9 = c.vf = c.vc11 = c.u = 0.0; c.dc = 0.0f;
        }
        if (oversampler) oversampler->reset();
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        targets();
        const int n = buf.getNumSamples();
        const int nc = juce::jmin(buf.getNumChannels(), (int) ch.size());
        const bool amp = ampOut->load() >= 0.5f;
        juce::dsp::AudioBlock<float> whole(buf);

        // The whole pedal runs in chunks of <= 32 samples, so every stage (also the tone stack, DEEP / PRESENCE and
        // VOLUME after the clipping) follows a moving knob every 32 samples, whatever the host's buffer size.
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int len = juce::jmin(32, n - i0);
            advanceSmoothers(len);
            rebuild(false);

            // ---- host rate: input, IC1B, interstage + GAIN (linear; IC1B's swing limit only bites on > 0.48 V peaks)
            for (int c = 0; c < nc; ++c)
            {
                auto& s = ch[(size_t) c];
                auto* x = buf.getWritePointer(c);
                for (int i = i0; i < i0 + len; ++i)
                {
                    s.in.step((double) x[i] * 0.5);                      // full scale ~ 0.5 V guitar
                    double d;
                    const double v1 = softRail(s.ic1b.process(s.in.node(inOut)), opaRail, d);
                    s.inter.step(v1);
                    x[i] = (float) s.inter.node(intP);                   // IC2A "+"
                }
            }

            // ---- 4x: IC2A with the zener feedback, diode gate, IC2B
            auto sub = whole.getSubBlock((size_t) i0, (size_t) len);
            auto up = oversampler->processSamplesUp(sub);
            for (size_t c = 0; c < up.getNumChannels() && c < ch.size(); ++c)
            {
                auto& s = ch[c];
                auto* x = up.getChannelPointer(c);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const double vo = ic2a(s, (double) x[i]);
                    const double y = diodeGate(s, vo);
                    double d;
                    x[i] = (float) softRail(s.ic2b.process(y), tlRail, d);
                }
            }
            oversampler->processSamplesDown(sub);

            // ---- host rate: tone stack, IC3A (Deep / Presence), output network
            for (int c = 0; c < nc; ++c)
            {
                auto& s = ch[(size_t) c];
                auto* x = buf.getWritePointer(c);
                for (int i = i0; i < i0 + len; ++i)
                {
                    s.stack.step((double) x[i]);
                    s.amp3.step(s.stack.node(stOut));
                    double d;
                    s.out.step(softRail(s.amp3.node(a3Out), tlRail, d));
                    const double v = amp ? s.out.node(outAmp) : s.out.node(outWiper) * directMatch;
                    float o = (float) (v * levelScale);
                    o -= lowpass(s.dc, o, gDc);
                    x[i] = o;
                }
            }
        }
    }

    // ---- tapers (exposed for tests)
    static double audioTaper(double x) { return (std::pow(10.0, 2.0 * x) - 1.0) / 99.0; }   // ~9 % at noon

    // Zener pair (1N5232 + 1N5232 back to back): current and slope. Soft knee ~ +/-5.9 V at 100 uA.
    static double zener(double v, double& g)
    {
        const double a = juce::jlimit(-60.0, 60.0, (v - zKnee) / zSlope), b = juce::jlimit(-60.0, 60.0, (-v - zKnee) / zSlope);
        const double ea = std::exp(a), eb = std::exp(b);
        g = zI0 / zSlope * (ea + eb);
        return zI0 * (ea - eb);
    }

private:
    // non-inverting op-amp stage, Zf = Rf || Cf, Zg = Rg + Cg (bilinear)
    struct OpStage
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void design(double fs, double Rf, double Cf, double Rg, double Cg)
        {
            // H(s) = 1 + Rf Cg s / ((1 + s Rf Cf)(1 + s Rg Cg))
            const double A2 = Rf * Cf * Rg * Cg, A1 = Rf * Cf + Rg * Cg, B2 = A2, B1 = A1 + Rf * Cg;
            const double k = 2.0 * fs, k2 = k * k;
            const double a0 = A2 * k2 + A1 * k + 1.0;
            b0 = (B2 * k2 + B1 * k + 1.0) / a0; b1 = (2.0 - 2.0 * B2 * k2) / a0; b2 = (B2 * k2 - B1 * k + 1.0) / a0;
            a1 = (2.0 - 2.0 * A2 * k2) / a0; a2 = (A2 * k2 - A1 * k + 1.0) / a0;
        }
        double process(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void clear() { z1 = z2 = 0.0; }
    };
    using Mna = SlowBurn::Mna;

    struct Ch
    {
        Mna in, inter, stack, amp3, out;
        OpStage ic1b, ic2b;
        double vc9 = 0, vf = 0;          // IC2A: ground-leg cap, feedback voltage (out - in)
        double vc11 = 0, u = 0;          // diode gate: coupling cap, diode voltage
        float dc = 0;
    };
    std::array<Ch, 2> ch;
    double sr = 48000.0, h = 1.0 / 192000.0;
    float gDc = 0;
    juce::SmoothedValue<double> gainSm, deepSm, volSm, bassSm, midSm, trebSm, presSm;
    std::atomic<float> *gain{}, *deep{}, *volume{}, *bass{}, *mid{}, *treble{}, *presence{}, *ampOut{};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    double last[7] { -1, -1, -1, -1, -1, -1, -1 };

    static constexpr double opaRail = 11.0, tlRail = 10.5;          // OPA2134 / TL072 swing on +/-12 V
    static constexpr double zKnee = 5.9, zSlope = 0.10, zI0 = 100e-6;
    static constexpr double IsSi = 2.52e-9, nVtSi = 1.752 * 0.02585;  // 1N4148
    static constexpr double directMatch = 100.0 / 250.0;             // DIRECT matched to AMP's low-frequency level
    static constexpr float levelScale = 1.32f;    // volts -> digital; calibrated: same loudness as the Valve Howler at noon (-18 dBFS sine in)

    // node / element indices (set in buildCircuits)
    int inOut = 0, intP = 0, intGain = 0;
    int stOut = 0, stTt = 0, stTb = 0, stBass = 0, stMid = 0;
    int a3Out = 0, a3Deep = 0, a3Pres = 0;
    int outWiper = 0, outAmp = 0, outVa = 0, outVb = 0;

    std::array<juce::SmoothedValue<double>*, 7> allSmoothers() { return { &gainSm, &deepSm, &volSm, &bassSm, &midSm, &trebSm, &presSm }; }
    void targets()
    {
        gainSm.setTargetValue(gain->load()); deepSm.setTargetValue(deep->load()); volSm.setTargetValue(volume->load());
        bassSm.setTargetValue(bass->load()); midSm.setTargetValue(mid->load()); trebSm.setTargetValue(treble->load());
        presSm.setTargetValue(presence->load());
    }
    void snap() { for (auto* sm : allSmoothers()) sm->setCurrentAndTargetValue(sm->getTargetValue()); }
    void advanceSmoothers(int len) { for (auto* sm : allSmoothers()) sm->skip(len); }

    void buildCircuits(Ch& c)
    {
        const double T = 1.0 / sr;
        // input: 1 = guitar, 2 = IC1A "+", 3 = IC1A out, 4 = IC1B "+"
        auto& a = c.in; a = {}; a.T = T;
        { const int n2 = a.addNode(), n3 = a.addNode(), n4 = a.addNode();
          a.C(1, n2, 22e-9); a.R(n2, 0, 500e3); a.C(n2, 0, 100e-12);
          a.opamp(n2, n3, n3);
          a.C(n3, n4, 1e-6); a.R(n4, 0, 100e3);
          inOut = n4; }
        // interstage: 1 = IC1B out, 2 = C7/R8, 3 = R10, 4 = IC2A "+"
        auto& b = c.inter; b = {}; b.T = T;
        { const int n2 = b.addNode(), n3 = b.addNode(), n4 = b.addNode();
          b.C(1, n2, 22e-9); b.R(n2, 0, 470e3); b.C(n2, n3, 2.2e-9); b.R(n2, n3, 680e3); b.R(n3, 0, 220e3);
          b.R(n3, n4, 220e3); intGain = b.R(n4, 0, 25e3);
          intP = n4; }
        // tone stack: 1 = IC2B out, 2 = treble lug 3, 3 = treble wiper, 4 = BASS3, 5 = slope node, 6 = MID3, 7 = IC3A "+"
        auto& s = c.stack; s = {}; s.T = T;
        { const int t3 = s.addNode(), tw = s.addNode(), b3 = s.addNode(), sl = s.addNode(), m3 = s.addNode(), p = s.addNode();
          s.C(1, t3, 560e-12); stTt = s.R(t3, tw, 125e3); stTb = s.R(tw, b3, 125e3);
          s.R(1, sl, 39e3); s.C(sl, b3, 22e-9); s.C(sl, m3, 22e-9);
          stBass = s.R(b3, m3, 100e3); stMid = s.R(m3, 0, 12.5e3);
          s.R(tw, p, 1.5e6); s.R(p, 0, 100e3);
          stOut = p; }
        // IC3A + gyrator: 1 = "+", 2 = "-", 3 = out, 4 = R32/C20, 5 = C18/R28, 6 = R28/DEEP, 7 = DEEP1,
        // 8 = IC3B out, 9 = IC3B "+", 10 = C17/R26, 11 = R26/PRESENCE
        auto& q = c.amp3; q = {}; q.T = T;
        { const int nn = q.addNode(), o = q.addNode(), g1 = q.addNode(), d1 = q.addNode(), d2 = q.addNode(), dp = q.addNode(),
                    go = q.addNode(), gp = q.addNode(), p1 = q.addNode(), p2 = q.addNode();
          q.opamp(1, nn, o);
          q.R(nn, o, 10e3); q.C(nn, o, 1e-9);
          q.R(nn, g1, 4.7e3); q.C(g1, 0, 10e-6);
          q.C(nn, d1, 2.2e-6); q.R(d1, d2, 1e3); a3Deep = q.R(d2, dp, 2.3e3);
          q.R(dp, go, 47.0); q.C(dp, gp, 330e-9); q.R(gp, 0, 56e3); q.opamp(gp, go, go);
          q.C(nn, p1, 1e-6); q.R(p1, p2, 110.0); a3Pres = q.R(p2, 0, 2.3e3);
          a3Out = o; }
        // output: 1 = IC3A out, 2 = C22/R34, 3 = VOLUME lug 3, 4 = wiper, 5 = AMP out
        auto& u = c.out; u = {}; u.T = T;
        { const int n2 = u.addNode(), n3 = u.addNode(), w = u.addNode(), o = u.addNode();
          u.C(1, n2, 10e-6); u.R(n2, n3, 1e3); outVa = u.R(n3, w, 9e3); outVb = u.R(w, 0, 1e3);
          u.R(w, o, 150e3); u.R(o, 0, 100e3); u.C(o, 0, 2.2e-9);
          outWiper = w; outAmp = o; }
        c.ic1b.design(sr, 220e3, 100e-12, 10e3, 1e-6);
        c.ic2b.design(sr * 4.0, 220e3, 100e-12, 10e3, 2.2e-6);
    }

    void rebuild(bool force)
    {
        if (force) for (auto& c : ch) buildCircuits(c);
        const double v[7] { gainSm.getCurrentValue(), deepSm.getCurrentValue(), volSm.getCurrentValue(), bassSm.getCurrentValue(),
                            midSm.getCurrentValue(), trebSm.getCurrentValue(), presSm.getCurrentValue() };
        bool changed[7]; bool any = force;
        for (int k = 0; k < 7; ++k) { changed[k] = force || v[k] != last[k]; any |= changed[k]; last[k] = v[k]; }
        if (! any) return;
        const double g = v[0], dp = v[1], vol = v[2], b = v[3], m = v[4], t = v[5], p = v[6];
        for (auto& c : ch)
        {
            if (force) c.in.build();
            if (changed[0]) { c.inter.res[(size_t) intGain].v = 250e3 * audioTaper(g) + 50.0; c.inter.build(); }    // GAIN A250k
            if (changed[3] || changed[4] || changed[5])
            {
                c.stack.res[(size_t) stTt].v = 250e3 * (1.0 - t) + 50.0;                 // TREBLE B250k
                c.stack.res[(size_t) stTb].v = 250e3 * t + 50.0;
                c.stack.res[(size_t) stBass].v = 1.0e6 * audioTaper(b) + 50.0;           // BASS A1M, CW = more bass
                c.stack.res[(size_t) stMid].v = 25e3 * m + 10.0;                         // MIDDLE B25k, CW = more mids
                c.stack.build();
            }
            if (changed[1] || changed[6])
            {
                c.amp3.res[(size_t) a3Deep].v = 25e3 * audioTaper(1.0 - dp) + 50.0;     // DEEP C25k, CW = 0 ohm
                c.amp3.res[(size_t) a3Pres].v = 25e3 * audioTaper(1.0 - p) + 50.0;      // PRESENCE C25k, CW = 0 ohm
                c.amp3.build();
            }
            if (changed[2])
            {
                const double vb = 10e3 * audioTaper(vol);                                // VOLUME A10k
                c.out.res[(size_t) outVa].v = 10e3 - vb + 1.0;
                c.out.res[(size_t) outVb].v = vb + 1.0;
                c.out.build();
            }
        }
    }

    // IC2A: non-inverting x101, feedback 1M || 47p || zener pair, ground leg 10k + 2u2 (backward Euler, 4x).
    double ic2a(Ch& s, double vp) const
    {
        constexpr double Rg = 10e3, Cg = 2.2e-6, Rf = 1e6, Cf = 47e-12;
        const double ig = (vp - s.vc9) / (Rg + h / Cg);
        s.vc9 += h / Cg * ig;
        const double cf = Cf / h;
        double v = s.vf;
        for (int it = 0; it < 30; ++it)
        {
            double gz;
            const double iz = zener(v, gz);
            const double F = v / Rf + cf * (v - s.vf) + iz - ig;
            const double dv = F / (1.0 / Rf + cf + gz);
            v -= juce::jlimit(-0.5, 0.5, dv);
            if (std::abs(dv) < 1e-5) break;
        }
        s.vf = v;
        double d;
        return softRail(vp + v, tlRail, d);
    }

    // C11 4n7 -> (1N4148 pair || 100k) -> node Y, 470k to ground (backward Euler, 4x). Returns Y.
    double diodeGate(Ch& s, double vo) const
    {
        constexpr double C11 = 4.7e-9, G15 = 1.0 / 100e3, G16 = 1.0 / 470e3;
        const double gc = C11 / h, k = gc / (gc + G16);
        double u = s.u;
        for (int it = 0; it < 30; ++it)
        {
            const double a = juce::jlimit(-60.0, 60.0, u / nVtSi);
            const double ea = std::exp(a), eb = 1.0 / ea;
            const double id = IsSi * (ea - eb), gd = IsSi / nVtSi * (ea + eb);
            const double y = k * (vo - u - s.vc11);
            const double F = u * G15 + id - y * G16;
            const double du = F / (G15 + gd + k * G16);
            u -= juce::jlimit(-0.3, 0.3, du);
            if (std::abs(du) < 1e-5) break;
        }
        const double y = k * (vo - u - s.vc11);
        s.vc11 = vo - (y + u);
        s.u = u;
        return y;
    }

    static float coeff(double fc, double fs) { auto g = std::tan(juce::MathConstants<double>::pi * fc / fs); return (float) (g / (1.0 + g)); }
    static float lowpass(float& s, float in, float g) { float v = (in - s) * g; float y = v + s; s = y + v; return y; }
    // smooth op-amp output limit: linear up to ~rail, then bends over (k = 4)
    static double softRail(double v, double rail, double& d)
    {
        const double r = v / rail, r4 = r * r * r * r, den = std::sqrt(std::sqrt(1.0 + r4));
        d = 1.0 / (den * (1.0 + r4));
        return v / den;
    }
};
