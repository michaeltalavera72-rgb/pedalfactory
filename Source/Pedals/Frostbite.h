#pragma once
#include "PedalBase.h"
#include "SlowBurn.h"      // SlowBurn::Mna, the small nodal-analysis solver shared by the circuit models

// Frostbite: a model of the Artec Cool Drive CDV-1 distortion (shown under our own name and artwork).
// Built from schematics "Artec - Cool Drive.pdf" (rev 1.1, a third-party drawing). One TL074 on a 9 V single supply
// (bias 4.5 V, so every stage swings about +/-3 V), signal path in volts:
//
//   IC1A  non-inverting preamp: 22k || 47p over 1k8 + C13 100n -> x1 in the bass rising to x13 (+22 dB) above ~900 Hz
//         (zero 67 Hz, pole 884 Hz). The factory MOD (C13 -> 4u7) makes it a flat x13: "more bass".
//   IC1D  PRE-SHAPE: an inverting multi-feedback network (C16 100n in, R26/R35/R27, C17/C18/C19, R28-R34, 50k lin pot whose
//         wiper is the virtual-earth input). Pot towards lug 1: a mid hump at ~800 Hz with the lows cut (100 Hz 12 dB
//         below the peak) and ~20 dB more drive - tight, focused picking edge. Towards lug 2: flat and quieter - loose
//         and full. Solved exactly with SlowBurn::Mna.
//   IC1C  GAIN stage: non-inverting, R1 2M || C11 47p over C7 1u + R8 1k8 + GAIN lug1-wiper (100k lin, wiper grounded):
//         x21 (26 dB) ... x1112 (61 dB), the 47p rolls the top off from ~1.7 kHz (plus the TL074's 3 MHz GBW).
//         Clips at the op-amp swing first (hard), then:
//   Clip node: C2 1u -> R2 10k -> node K with C3 10n and D1/D2 1N4148 to ground (soft). The other half of the GAIN pot
//         (wiper - lug 2 + R7 10k) also loads node K, so low GAIN settings drive the diodes less (-6 dB at minimum).
//         The factory MOD adds a second diode in series with each (clips ~2x higher: "more level", more open).
//   IC1B  POST-SHAPE: C4 1u -> a second multi-feedback network (R15/R16/R17, C8/C9/C10, R18-R36, 50k lin pot). Towards
//         lug 1: wide and bright (flat to ~6 kHz); towards lug 2: a warm 400-800 Hz hump, treble -25 dB at 10 kHz.
//         Node K, C4 and the whole IC1B network are one Mna (the post stage loads the clip node), the diode pair is
//         solved per sample by Newton on node K's Thevenin equivalent (4x oversampled).
//   R25 2k2 -> C12 1u -> LEVEL 50k lin -> OUT.
// Knob directions (the drawing's pot pin numbers don't say): every knob gives "more" clockwise - GAIN more gain,
// PRE more mid focus/edge, POST brighter/wider (the "screaming metal" end), LEVEL louder.
// Verified against Tools/Analysis/cooldrive.py (exact AC nodal analysis of the drawn circuit).
class Frostbite : public PedalBase
{
public:
    juce::String getId() const override { return "fb"; }
    juce::String getName() const override { return "Frostbite"; }
    PedalCategory getCategory() const override { return PedalCategory::Distortion; }
    juce::String getBasedOn() const override { return "Artec Cool Drive CDV-1 (op-amp distortion with PRE and POST shape controls)"; }

    juce::String describe(const juce::String& id) const override
    {
        if (id == "fb_bypass") return "Footswitch: turns Frostbite on or off.";
        if (id == "fb_gain")   return "Amount of distortion: from a gritty crunch fully left to a thick, saturated high-gain "
                                      "sound fully right. Most of the change happens in the right half.";
        if (id == "fb_pre")    return "Shapes the guitar before it is distorted. Left: flat and loose, with full lows and a "
                                      "softer attack. Right: a tight, focused mid hump with the lows trimmed - more edge and "
                                      "bite on the pick attack, and more drive.";
        if (id == "fb_post")   return "Shapes the distortion afterwards. Left: a warm, smooth midrange with rounded highs. "
                                      "Right: wider and brighter, with more lows and highs - the screaming metal end.";
        if (id == "fb_level")  return "Output level.";
        if (id == "fb_mod")    return "The two factory mods printed on the original schematic. MOD: more bass going into the "
                                      "gain stage and doubled clipping diodes - louder, fuller and more open. STOCK: the "
                                      "circuit as shipped.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"fb_bypass",1}, "Frostbite Bypass", false));
        l.add(std::make_unique<P>(juce::ParameterID{"fb_gain",1},  "Gain",       0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"fb_pre",1},   "Pre Shape",  0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"fb_post",1},  "Post Shape", 0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"fb_level",1}, "Level",      0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"fb_mod",1}, "Mod", false,
            juce::AudioParameterBoolAttributes().withStringFromValueFunction([](bool b, int) { return juce::String(b ? "Mod" : "Stock"); })));
        knobIds = { "fb_gain", "fb_pre", "fb_post", "fb_level" };
        toggleId = "fb_mod"; toggleLabel = "MOD / STOCK";
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam = s.getRawParameterValue("fb_bypass");
        gain  = s.getRawParameterValue("fb_gain");
        pre   = s.getRawParameterValue("fb_pre");
        post  = s.getRawParameterValue("fb_post");
        level = s.getRawParameterValue("fb_level");
        mod   = s.getRawParameterValue("fb_mod");
    }

    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 4x
        oversampler->initProcessing(spec.maximumBlockSize);
        gDc = coeff(5.0, sr);
        for (auto* sm : { &gainSm, &preSm, &postSm, &levelSm }) sm->reset(sr, 0.03);
        if (gain != nullptr) { targets(); snap(); }
        modSm.reset(sr, 0.02);
        modSm.setCurrentAndTargetValue(mod != nullptr && mod->load() >= 0.5f ? 1.0 : 0.0);
        for (auto& c : ch) buildCircuits(c);
        rebuild(true);
        reset();
    }
    void reset() override
    {
        for (auto& c : ch)
        {
            c.preNet.clearState(); c.postNet.clearState();
            c.ic1a.clear(); c.ic1aMod.clear(); c.ic1c.clear();
            c.bw = 0.0; c.vk = 0.0; c.inHp = 0.0; c.dc = 0.0f;
        }
        if (oversampler) oversampler->reset();
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        targets();
        modSm.setTargetValue(mod->load() >= 0.5f ? 1.0 : 0.0);
        const int n = buf.getNumSamples();
        const int nc = juce::jmin(buf.getNumChannels(), (int) ch.size());
        juce::dsp::AudioBlock<float> whole(buf);

        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int len = juce::jmin(32, n - i0);
            const float g0 = (float) levelSm.getCurrentValue();
            const double m0 = modSm.getCurrentValue();
            modSm.skip(len);
            const double m1 = modSm.getCurrentValue();
            advanceSmoothers(len);
            rebuild(false);
            const float g1 = (float) levelSm.getCurrentValue();

            // ---- host rate: input coupling, IC1A, PRE-SHAPE (linear apart from the swing limits)
            for (int c = 0; c < nc; ++c)
            {
                auto& s = ch[(size_t) c];
                auto* x = buf.getWritePointer(c);
                for (int i = i0; i < i0 + len; ++i)
                {
                    double v = (double) x[i] * 0.5;                          // full scale ~ 0.5 V guitar
                    v -= lowpassD(s.inHp, v, gIn);                           // C 100n into 1M (1.6 Hz)
                    // MOD (C13 100n -> 4u7): both versions run, crossfaded over 20 ms when the toggle moves
                    const double mm = m0 + (m1 - m0) * (double) (i - i0 + 1) / (double) len;
                    const double a = softRail((1.0 - mm) * s.ic1a.process(v) + mm * s.ic1aMod.process(v), tlRail);
                    s.preNet.step(a);
                    x[i] = (float) softRail(s.preNet.node(preOut), tlRail); // IC1D out (C20 / R14 / C1 -> IC1C "+")
                }
            }

            // ---- 4x: GAIN stage, clip node + POST-SHAPE
            auto sub = whole.getSubBlock((size_t) i0, (size_t) len);
            auto up = oversampler->processSamplesUp(sub);
            for (size_t c = 0; c < up.getNumChannels() && c < ch.size(); ++c)
            {
                auto& s = ch[c];
                auto* x = up.getChannelPointer(c);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    double u = s.ic1c.process((double) x[i]);
                    u = lowpassD(s.bw, u, gBw);                              // TL074 gain-bandwidth at this gain
                    u = softRail(u, tlRail);
                    stepPost(s, u, 1.0 + m1);     // MOD: two 1N4148s in series per direction
                    x[i] = (float) softRail(s.postNet.node(postOut), tlRail);
                }
            }
            oversampler->processSamplesDown(sub);

            // ---- LEVEL (50k lin; R25 2k2 in series), output scaling, DC guard
            for (int c = 0; c < nc; ++c)
            {
                auto& s = ch[(size_t) c];
                auto* x = buf.getWritePointer(c);
                for (int i = i0; i < i0 + len; ++i)
                {
                    const float t = (float) (i - i0 + 1) / (float) len;
                    const float lv = g0 + (g1 - g0) * t;
                    float o = x[i] * lv * (float) (50.0 / 52.2) * levelScale;
                    o -= lowpass(s.dc, o, gDc);
                    x[i] = o;
                }
            }
        }
    }

    // smooth op-amp output limit: linear up to ~rail, then bends over (k = 4) (exposed for tests)
    static double softRail(double v, double rail)
    {
        const double r = v / rail, r4 = r * r * r * r;
        return v / std::sqrt(std::sqrt(1.0 + r4));
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
        Mna preNet, postNet;
        OpStage ic1a, ic1aMod, ic1c;
        double inHp = 0, bw = 0, vk = 0;
        float dc = 0;
    };
    std::array<Ch, 2> ch;
    double sr = 48000.0, gIn = 0, gBw = 0;
    float gDc = 0;
    juce::SmoothedValue<double> gainSm, preSm, postSm, levelSm, modSm;
    std::atomic<float> *gain{}, *pre{}, *post{}, *level{}, *mod{};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    double last[3] { -1, -1, -1 };

    static constexpr double tlRail = 3.0;                              // TL074 swing on 9 V around the 4.5 V bias
    static constexpr double IsSi = 2.52e-9, nVtSi = 1.752 * 0.02585;  // 1N4148
    static constexpr double gbw = 3.0e6;                               // TL074 gain-bandwidth
    static constexpr float levelScale = 1.32f;    // volts -> digital; calibrated: same loudness as the Valve Howler at noon (-18 dBFS sine in)

    // element / node indices (set in buildCircuits)
    int preOut = 0, preR1 = 0, preR2 = 0;
    int postOut = 0, postK = 0, postR1 = 0, postR2 = 0, postRl = 0;

    void targets()
    {
        gainSm.setTargetValue(gain->load()); preSm.setTargetValue(pre->load());
        postSm.setTargetValue(post->load()); levelSm.setTargetValue(level->load());
    }
    void snap() { for (auto* sm : { &gainSm, &preSm, &postSm, &levelSm }) sm->setCurrentAndTargetValue(sm->getTargetValue()); }
    void advanceSmoothers(int len) { for (auto* sm : { &gainSm, &preSm, &postSm, &levelSm }) sm->skip(len); }

    void buildCircuits(Ch& c)
    {
        gIn = coeffD(1.6, sr);
        c.ic1a.design(sr, 22e3, 47e-12, 1.8e3, 100e-9);       // stock C13
        c.ic1aMod.design(sr, 22e3, 47e-12, 1.8e3, 4.7e-6);    // MOD C13
        // PRE-SHAPE (host rate): 1 = IC1A out, A = C16/R26/R35/R27, B, C, D, E, F, G = IC1D out, H, W = wiper (virtual earth)
        auto& p = c.preNet; p = {}; p.T = 1.0 / sr;
        { const int A = p.addNode(), B = p.addNode(), C = p.addNode(), D = p.addNode(), E = p.addNode(),
                    F = p.addNode(), G = p.addNode(), H = p.addNode(), W = p.addNode();
          p.C(1, A, 100e-9);
          p.R(A, F, 100e3); p.R(A, B, 4.7e3); p.R(A, H, 100e3);                  // R26, R35, R27
          p.R(B, E, 47e3); p.C(B, C, 22e-9); p.R(B, C, 100e3);                   // R30, C17, R28
          p.R(C, D, 47e3); p.R(C, G, 22e3);                                     // R34, R31
          p.C(F, E, 2.2e-9); p.R(F, G, 100e3);                                  // C18, R32
          p.R(E, G, 1.8e3);                                                     // R29
          p.C(H, D, 4.7e-9); p.R(H, G, 2.2e3);                                  // C19, R33
          preR1 = p.R(D, W, 25e3); preR2 = p.R(W, E, 25e3);                     // PRE-SHAPE 50k lin: lug 1 = D, lug 2 = E
          p.opamp(0, W, G);
          preOut = G; }
        // POST (4x): 1 = IC1C out, S2 = C2/R2, K = clip node, Q = C4 out, T, U, V, X, Y, Z, O = IC1B out, W = wiper
        auto& q = c.postNet; q = {}; q.T = 1.0 / (sr * 4.0);
        { const int S2 = q.addNode(), K = q.addNode(), Q = q.addNode(), T = q.addNode(), U = q.addNode(), V = q.addNode(),
                    X = q.addNode(), Y = q.addNode(), Z = q.addNode(), O = q.addNode(), W = q.addNode();
          q.C(1, S2, 1e-6); q.R(S2, K, 10e3); q.C(K, 0, 10e-9);                 // C2, R2, C3
          postRl = q.R(K, 0, 60e3);                                             // R7 10k + GAIN wiper-lug 2 (wiper grounded)
          q.C(K, Q, 1e-6);                                                      // C4
          q.R(Q, T, 10e3); q.R(Q, X, 4.7e3); q.R(Q, Z, 100e3);                  // R16, R15, R17
          q.R(T, O, 100e3); q.C(T, U, 2.2e-9);                                  // R22, C9
          q.R(U, X, 47e3); q.R(U, O, 27e3);                                     // R20, R36
          q.R(V, O, 22e3); q.C(V, X, 22e-9); q.R(V, X, 100e3); q.R(V, Y, 47e3); // R21, C8, R18, R24
          q.C(Y, Z, 4.7e-9); q.R(Z, O, 2.2e3);                                  // C10, R23
          postR1 = q.R(U, W, 25e3); postR2 = q.R(W, Y, 25e3);                   // POST_SHAPE 50k lin: lug 1 = U, lug 2 = Y
          q.opamp(0, W, O);
          postOut = O; postK = K; }
    }

    void rebuild(bool force)
    {
        const double v[3] { gainSm.getCurrentValue(), preSm.getCurrentValue(), postSm.getCurrentValue() };
        bool changed[3]; bool any = force;
        for (int k = 0; k < 3; ++k) { changed[k] = force || v[k] != last[k]; any |= changed[k]; last[k] = v[k]; }
        if (! any) return;
        // GAIN 100k lin, CW = more gain: lug 1 (R8 side) - wiper = ra shrinks clockwise
        const double ra = 100e3 * (1.0 - v[0]) + 1.0, rb = 100e3 - ra + 2.0;
        const double gDc0 = 1.0 + 2e6 / (1.8e3 + ra);                            // mid-band noise gain
        gBw = coeffD(juce::jmin(gbw / gDc0, sr * 4.0 * 0.45), sr * 4.0);
        const double ap = 1.0 - v[1], aq = 1.0 - v[2];                           // CW = towards lug 1
        for (auto& c : ch)
        {
            if (changed[0])
            {
                c.ic1c.design(sr * 4.0, 2e6, 47e-12, 1.8e3 + ra, 1e-6);
                c.postNet.res[(size_t) postRl].v = 10e3 + rb;
            }
            if (changed[1])
            {
                c.preNet.res[(size_t) preR1].v = 50e3 * ap + 1.0;
                c.preNet.res[(size_t) preR2].v = 50e3 * (1.0 - ap) + 1.0;
                c.preNet.build();
            }
            if (changed[2])
            {
                c.postNet.res[(size_t) postR1].v = 50e3 * aq + 1.0;
                c.postNet.res[(size_t) postR2].v = 50e3 * (1.0 - aq) + 1.0;
            }
            if (changed[0] || changed[2]) c.postNet.build();
        }
    }

    // One step of the post network with the 1N4148 pair (nSeries in series per direction: 1 stock, 2 MOD) from node K to ground:
    // linear solution x0, then Newton on vK = vK0 - Rkk * Id(vK), then superpose the diode current.
    void stepPost(Ch& s, double vin, double nSeries)
    {
        auto& m = s.postNet;
        const int N = m.size();
        std::array<double, Mna::maxN> rhs {};
        for (auto& c : m.caps)
        {
            const double ieq = c.g * c.v + c.i;
            c.i = ieq;
            if (c.a > 0) rhs[(size_t) c.a - 1] += ieq;
            if (c.b > 0) rhs[(size_t) c.b - 1] -= ieq;
        }
        rhs[(size_t) m.nodes] = vin;
        for (int i = 0; i < N; ++i)
        {
            double acc = 0.0;
            for (int j = 0; j < N; ++j) acc += m.inv[(size_t) i][(size_t) j] * rhs[(size_t) j];
            m.x[(size_t) i] = acc;
        }
        const size_t k = (size_t) postK - 1;
        const double v0 = m.x[k], rkk = m.inv[k][k];
        const double nvt = nVtSi * nSeries;
        double v = s.vk, id = 0.0;
        for (int it = 0; it < 40; ++it)
        {
            const double a = juce::jlimit(-60.0, 60.0, v / nvt);
            const double ea = std::exp(a), eb = 1.0 / ea;
            id = IsSi * (ea - eb);
            const double gd = IsSi / nvt * (ea + eb);
            const double F = v - v0 + rkk * id;
            const double dv = F / (1.0 + rkk * gd);
            v -= juce::jlimit(-0.2, 0.2, dv);
            if (std::abs(dv) < 1e-5) break;
        }
        { const double a = juce::jlimit(-60.0, 60.0, v / nvt); id = IsSi * (std::exp(a) - std::exp(-a)); }
        s.vk = v;
        for (int i = 0; i < N; ++i) m.x[(size_t) i] -= m.inv[(size_t) i][k] * id;
        for (auto& c : m.caps)
        {
            const double ieq = c.i;
            c.v = m.node(c.a) - m.node(c.b);
            c.i = c.g * c.v - ieq;
        }
    }

    static double coeffD(double fc, double fs) { const double g = std::tan(juce::MathConstants<double>::pi * fc / fs); return g / (1.0 + g); }
    static double lowpassD(double& s, double in, double g) { const double v = (in - s) * g; const double y = v + s; s = y + v; return y; }
    static float coeff(double fc, double fs) { return (float) coeffD(fc, fs); }
    static float lowpass(float& s, float in, float g) { float v = (in - s) * g; float y = v + s; s = y + v; return y; }
};
