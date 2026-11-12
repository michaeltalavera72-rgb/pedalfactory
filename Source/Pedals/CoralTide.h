#pragma once
#include "PedalBase.h"

// Coral Tide: two Bearfoot / BJFE circuits in one double-wide enclosure, shown under our own name
// (claude/bjfe-dual-plan.md; schematics "Bearfoot _ BJFE - Sea Blue Q.pdf" and "... - Baby Pink Boost.pdf").
//
//   Face (Michael's artwork): BOOST / LEVEL knobs left, ORDER toggle centre, LOW / HIGH right; footswitches
//   BOOST (left) and EQ (right).
//   EQ section (right footswitch = main "ct_bypass"): the Sea Blue EQ.
//       Q1 JFET, common source: IN -> 14k7 -> 22n -> gate (1M). Drain 14k7 + 1n to V+, source 14k7 || (22u + 1k):
//       ~+12 dB. C8 470n into a passive network:
//         treble: (4n7 || 47k) -> TREBLE 50kB -> 22n -> gnd, wiper = output node
//         bass:   5k6 -> BASS 50kA (22n across the upper half, 220n across the lower half) -> 1k -> gnd,
//                 wiper -> 5k6 -> output node
//       Q2 JFET: gate = output node (1M). Drain 14k7 + 1n, source 14k7 || 1u (+20 dB above ~120 Hz, tight bass).
//       1u -> VOLUME 50kB, fixed here at the setting that gives unity with LOW / HIGH at noon (Michael's choice (b):
//       the pedal's single LEVEL knob is a master output level instead, so the EQ footswitch doesn't jump +9 dB).
//   BOOST section (left footswitch "ct_boost_on"): the Baby Pink Booster.
//       2N5952 common source: IN -> 47n -> 14k7 -> gate (2M). Drain 14k7 (100p drain-source), source 12k to gnd and
//       22u -> 1k -> BOOST 50kC; the footswitch grounds the BOOST wiper. Switched off, the source sees 1k + 50k + 150k:
//       the stage stays in the signal path as a ~unity JFET buffer (the real pedal's buffered bypass).
//   ORDER mini toggle ("ct_order", up = B > EQ = BOOST first, down = EQ > B = default).
//   LEVEL ("ct_level"): master output of the whole pedal after both sections, x0 .. x2.8 (unity at 0.357, max +9 dB).
//
// Model: each JFET stage is one linear network (resistors, trapezoidal capacitors, V+ and the input as fixed
// voltages) with the JFET as a nonlinear current source between drain and source (square law + triode region),
// solved per sample with a one-unknown safeguarded Newton. Bias points match the traced voltages
// (Sea Blue Q1 6.78 / 1.86 V, Q2 6.60 / 2.02 V on ~8.64 V). 2x oversampling (the JFETs only soft-clip).
// Small-signal response checked against Tools/Analysis/bjfe_dual.py.
class CoralTide : public PedalBase
{
public:
    juce::String getId() const override { return "ct"; }
    juce::String getName() const override { return "Coral Tide"; }
    PedalCategory getCategory() const override { return PedalCategory::BoostEq; }
    int widthUnits() const override { return 2; }
    juce::String getBasedOn() const override
    {
        return "Bearfoot / BJFE Sea Blue EQ and Baby Pink Booster (two pedals combined in one double-wide enclosure)";
    }
    juce::String primarySwitchLabel() const override { return "EQ"; }
    juce::String secondarySwitchId() const override { return "ct_boost_on"; }
    juce::String secondarySwitchLabel() const override { return "BOOST"; }

    juce::String describe(const juce::String& id) const override
    {
        if (id == "ct_bypass")   return "Right footswitch (blue LED): turns the EQ section on or off.";
        if (id == "ct_boost_on") return "Left footswitch (red LED): turns the BOOST on or off. With the boost off its JFET stage stays in the path as a "
                                        "near-transparent buffer (as on the original pedal).";
        if (id == "ct_treble")   return "Treble (HIGH): noon is about flat; down cuts the highs (up to about -19 dB at 10 kHz), up adds sparkle (about +8 dB).";
        if (id == "ct_bass")     return "Bass (LOW): a broad low shelf peaking around 60-100 Hz, about -7 dB fully down to +8 dB fully up.";
        if (id == "ct_level")    return "Master output level of the whole pedal (after the EQ and the boost). Unity at the default "
                                        "(a little before 11 o'clock), up to about +9 dB fully up.";
        if (id == "ct_boost")    return "Clean, full-range boost, about +1 dB fully down to +14 dB fully up (when the BOOST footswitch is on). "
                                        "Hit hard, the JFET starts to round off the peaks.";
        if (id == "ct_order")    return "Order of the two sections. Down (EQ > B) = EQ then BOOST: the boost lifts your EQ'd tone, for solos. "
                                        "Up (B > EQ) = BOOST then EQ: the boost drives the EQ's two JFET stages for a touch of grit.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"ct_bypass",1}, "Coral Tide EQ Bypass", false));
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"ct_boost_on",1}, "Coral Tide Boost On", false));
        l.add(std::make_unique<P>(juce::ParameterID{"ct_boost",1},  "Boost",  0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"ct_level",1},  "Level",  0.0f, 1.0f, defaultLevel));
        l.add(std::make_unique<P>(juce::ParameterID{"ct_bass",1},   "Low",    0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"ct_treble",1}, "High",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"ct_order",1}, "Coral Tide Order (Boost first)", false));
        knobIds = { "ct_boost", "ct_level", "ct_bass", "ct_treble" };      // left to right as on the artwork
        toggleId = "ct_order"; toggleLabel = "ORDER";
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam    = s.getRawParameterValue("ct_bypass");
        secondaryParam = s.getRawParameterValue("ct_boost_on");
        treble = s.getRawParameterValue("ct_treble");
        bass   = s.getRawParameterValue("ct_bass");
        level  = s.getRawParameterValue("ct_level");
        boost  = s.getRawParameterValue("ct_boost");
        order  = s.getRawParameterValue("ct_order");
    }

    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 2x
        oversampler->initProcessing(spec.maximumBlockSize);
        T = 1.0 / (sr * 2.0);
        const double fs = sr * 2.0;
        eqMix.reset(fs, 0.012);    boostMix.reset(fs, 0.020);
        volGain.reset(fs, 0.03);   orderGain.reset(fs, 0.005);
        for (auto& c : ch) buildChannel(c);
        reset();
    }
    void reset() override
    {
        eqMix.setCurrentAndTargetValue(primaryOn() ? 1.0f : 0.0f);
        boostMix.setCurrentAndTargetValue(secondaryOn() ? 1.0f : 0.0f);
        volGain.setCurrentAndTargetValue(levelGain(level != nullptr ? level->load() : defaultLevel));
        orderGain.setCurrentAndTargetValue(1.0f);
        boostFirst = order != nullptr && order->load() >= 0.5f;
        eqIdle = false;
        for (auto& c : ch)
            if (! c.a.res.empty()) { updatePots(c, true); initBias(c); }
        if (oversampler) oversampler->reset();
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        eqMix.setTargetValue(primaryOn() ? 1.0f : 0.0f);
        boostMix.setTargetValue(secondaryOn() ? 1.0f : 0.0f);
        volGain.setTargetValue(levelGain(level->load()));
        const bool wantBoostFirst = order->load() >= 0.5f;

        juce::dsp::AudioBlock<float> block(buf);
        auto up = oversampler->processSamplesUp(block);
        const size_t nCh = juce::jmin(up.getNumChannels(), ch.size());
        const size_t n = up.getNumSamples();
        // EQ switched off and its fade finished (BOOST only): don't run the two Sea Blue stages.
        // When the EQ comes back they restart from their operating point (the 12 ms fade covers the start).
        const bool eqIdleNow = ! primaryOn() && ! eqMix.isSmoothing() && eqMix.getCurrentValue() <= 0.0f;
        if (eqIdle && ! eqIdleNow)
            for (auto& c : ch) if (! c.a.res.empty()) { c.a.solveBias(); c.b.solveBias(); }
        eqIdle = eqIdleNow;
        // the smoothed values are shared by the channels: advance them once per sample
        for (size_t i = 0; i < n; ++i)
        {
            const float em = eqMix.getNextValue(), bm = boostMix.getNextValue(), vg = volGain.getNextValue();
            // ORDER change: fade out (5 ms), swap, fade back in - no click from the re-ordered states
            if (wantBoostFirst != boostFirst && ! orderGain.isSmoothing() && orderGain.getTargetValue() > 0.5f)
                orderGain.setTargetValue(0.0f);
            const float og = orderGain.getNextValue();
            if (orderGain.getTargetValue() < 0.5f && ! orderGain.isSmoothing())
            {
                boostFirst = wantBoostFirst;
                orderGain.setTargetValue(1.0f);
                for (auto& c : ch) c.a.beSteps = c.b.beSteps = c.p.beSteps = 2;   // inputs re-routed: damp the jump
            }
            for (size_t c = 0; c < nCh; ++c)
            {
                auto& s = ch[c];
                if ((s.counter++ & 31) == 0) updatePots(s, false, bm);
                float* x = up.getChannelPointer(c);
                const double in = (double) x[i] * 0.5;                    // full scale ~ 0.5 V guitar
                double v;
                if (eqIdleNow) v = pinkStage(s, in);
                else if (boostFirst)
                {
                    v = pinkStage(s, in);
                    v = em * eqSection(s, v) + (1.0f - em) * v;
                }
                else
                {
                    v = em * eqSection(s, in) + (1.0f - em) * in;
                    v = pinkStage(s, v);
                }
                x[i] = (float) (v * 2.0 * og * vg);                       // LEVEL; volts back to digital
            }
        }
        oversampler->processSamplesDown(block);
    }

    // ---- exposed for tests -------------------------------------------------------------------------------------
    // Linear network + one JFET. Node 0 = ground, 1 = V+, 2 = input (fixed voltages); 3.. = unknowns.
    struct JfetNet
    {
        static constexpr int maxN = 16, nFixed = 3;
        struct Res { int a, b; double r; };
        struct Cap { int a, b; double c, v = 0, i = 0; };
        std::vector<Res> res;
        std::vector<Cap> caps;
        int nodes = nFixed;
        int gate = -1, drain = -1, source = -1;
        double idss = 3e-3, vp = -2.0, vdd = 8.64, T = 1.0 / 96000.0, id = 0.0;
        std::array<double, maxN> x {};                                   // node voltages (all nodes)
        // Two system matrices: [0] trapezoidal (normal steps), [1] backward Euler. The trapezoidal rule rings at
        // Nyquist after an abrupt change (pot or footswitch rebuild, ORDER swap) on stiff RC pairs (e.g. the 100p
        // drain-source cap, pots at their ends); a couple of backward-Euler steps afterwards damp that.
        struct Mat
        {
            std::array<std::array<double, maxN>, maxN> ginv {};           // inverse of the unknown-node block
            std::array<std::array<double, nFixed>, maxN> gfix {};         // unknown rows x fixed columns
            std::array<double, maxN> z {};                                // dV per amp of drain current
        };
        std::array<Mat, 2> mat;
        int beSteps = 0;

        int addNode() { return nodes++; }
        int R(int a, int b, double r) { res.push_back({ a, b, r }); return (int) res.size() - 1; }
        void C(int a, int b, double c) { caps.push_back({ a, b, c }); }

        void build(bool dc)
        {
            for (int k = 0; k < (dc ? 1 : 2); ++k) buildMat(mat[(size_t) k], dc ? 0.0 : (k == 0 ? 2.0 : 1.0));
            beSteps = dc ? 0 : 2;
        }
        // capScale: companion conductance = capScale * C / T (2 = trapezoidal, 1 = backward Euler, 0 = DC: caps open)
        void buildMat(Mat& mt, double capScale)
        {
            const int N = nodes, m = N - nFixed;
            std::array<std::array<double, maxN>, maxN> g {};
            auto stamp = [&](int a, int b, double y)
            {
                g[(size_t) a][(size_t) a] += y; g[(size_t) b][(size_t) b] += y;
                g[(size_t) a][(size_t) b] -= y; g[(size_t) b][(size_t) a] -= y;
            };
            for (auto& r : res) stamp(r.a, r.b, 1.0 / r.r);
            if (capScale > 0.0) for (auto& c : caps) stamp(c.a, c.b, capScale * c.c / T);
            std::array<std::array<double, 2 * maxN>, maxN> a {};
            for (int i = 0; i < m; ++i)
            {
                for (int j = 0; j < m; ++j) a[(size_t) i][(size_t) j] = g[(size_t) (i + nFixed)][(size_t) (j + nFixed)];
                a[(size_t) i][(size_t) (m + i)] = 1.0;
                for (int k = 0; k < nFixed; ++k) mt.gfix[(size_t) i][(size_t) k] = g[(size_t) (i + nFixed)][(size_t) k];
            }
            for (int col = 0; col < m; ++col)                               // Gauss-Jordan
            {
                int piv = col;
                for (int r = col + 1; r < m; ++r) if (std::abs(a[(size_t) r][(size_t) col]) > std::abs(a[(size_t) piv][(size_t) col])) piv = r;
                std::swap(a[(size_t) col], a[(size_t) piv]);
                const double d = 1.0 / a[(size_t) col][(size_t) col];
                for (int j = 0; j < 2 * m; ++j) a[(size_t) col][(size_t) j] *= d;
                for (int r = 0; r < m; ++r)
                    if (r != col && a[(size_t) r][(size_t) col] != 0.0)
                    {
                        const double f = a[(size_t) r][(size_t) col];
                        for (int j = 0; j < 2 * m; ++j) a[(size_t) r][(size_t) j] -= f * a[(size_t) col][(size_t) j];
                    }
            }
            for (int i = 0; i < m; ++i) for (int j = 0; j < m; ++j) mt.ginv[(size_t) i][(size_t) j] = a[(size_t) i][(size_t) (m + j)];
            // drain current leaves the drain node and enters the source node
            for (int i = 0; i < m; ++i)
                mt.z[(size_t) (i + nFixed)] = (source >= nFixed ? mt.ginv[(size_t) i][(size_t) (source - nFixed)] : 0.0)
                                            - (drain  >= nFixed ? mt.ginv[(size_t) i][(size_t) (drain  - nFixed)] : 0.0);
            for (int k = 0; k < nFixed; ++k) mt.z[(size_t) k] = 0.0;
        }

        // Square-law JFET (n-channel, vp < 0) with the triode region; gm / gds = partial derivatives.
        double jfet(double vgs, double vds, double& gm, double& gds) const
        {
            vgs = juce::jmin(vgs, 0.6);                                     // gate diode would conduct beyond this
            vds = juce::jmax(vds, 0.0);
            const double u = vgs - vp;                                      // overdrive
            if (u <= 0.0) { gm = gds = 0.0; return 0.0; }
            const double k = idss / (vp * vp);
            if (vds >= u) { gm = 2.0 * k * u; gds = 0.0; return k * u * u; }
            gm = 2.0 * k * vds; gds = 2.0 * k * (u - vds);
            return k * (2.0 * u * vds - vds * vds);
        }

        // One time step (dc = solve the operating point: caps open, no history). Read the result from x[].
        void step(double vin, bool dc = false)
        {
            const int m = nodes - nFixed;
            const bool be = ! dc && beSteps > 0;
            if (be) --beSteps;
            const Mat& mt = mat[be ? 1 : 0];
            const double cs = be ? 1.0 : 2.0;
            std::array<double, maxN> rhs {};
            if (! dc)
                for (auto& c : caps)
                {
                    const double g = cs * c.c / T, ieq = be ? g * c.v : g * c.v + c.i;
                    if (c.a >= nFixed) rhs[(size_t) (c.a - nFixed)] += ieq;
                    if (c.b >= nFixed) rhs[(size_t) (c.b - nFixed)] -= ieq;
                }
            x[0] = 0.0; x[1] = vdd; x[2] = vin;
            for (int i = 0; i < m; ++i)
                rhs[(size_t) i] -= mt.gfix[(size_t) i][1] * vdd + mt.gfix[(size_t) i][2] * vin;
            std::array<double, maxN> v0 {};
            for (int i = 0; i < m; ++i)
            {
                double acc = 0.0;
                for (int j = 0; j < m; ++j) acc += mt.ginv[(size_t) i][(size_t) j] * rhs[(size_t) j];
                v0[(size_t) (i + nFixed)] = acc;
            }
            v0[0] = 0.0; v0[1] = vdd; v0[2] = vin;
            // F(id) = id - Ijfet(vgs(id), vds(id)) is increasing in id: safeguarded Newton
            const auto& z = mt.z;
            const double zg = z[(size_t) gate], zd = z[(size_t) drain], zs = z[(size_t) source];
            double lo = 0.0, hi = idss * 2.0, i0 = juce::jlimit(lo, hi, id);
            for (int it = 0; it < 12; ++it)
            {
                const double vg = v0[(size_t) gate] + i0 * zg, vd = v0[(size_t) drain] + i0 * zd, vs = v0[(size_t) source] + i0 * zs;
                double gm = 0, gds = 0;
                const double f = i0 - jfet(vg - vs, vd - vs, gm, gds);
                if (f == 0.0) break;                                         // exact root
                if (f > 0.0) hi = i0; else lo = i0;
                const double fp = 1.0 - gm * (zg - zs) - gds * (zd - zs);
                double nxt = i0 - f / fp;
                if (nxt < lo || nxt > hi) nxt = 0.5 * (lo + hi);              // Newton left the bracket: bisect
                const bool done = std::abs(nxt - i0) < 1e-10;
                i0 = nxt;
                if (done) break;
            }
            id = i0;
            for (int k = 0; k < nodes; ++k) x[(size_t) k] = v0[(size_t) k] + id * z[(size_t) k];
            if (dc)
                for (auto& c : caps) { c.v = x[(size_t) c.a] - x[(size_t) c.b]; c.i = 0.0; }
            else
                for (auto& c : caps)
                {
                    const double g = cs * c.c / T, ieq = be ? g * c.v : g * c.v + c.i;
                    c.v = x[(size_t) c.a] - x[(size_t) c.b];
                    c.i = g * c.v - ieq;                                    // capacitor current at this step
                }
        }
        // operating point: a few Newton passes on the DC network (caps open), then back to the transient matrix
        void solveBias()
        {
            build(true);
            id = 0.0;
            for (int k = 0; k < 4; ++k) step(0.0, true);
            build(false);
        }
    };

    struct Ch
    {
        JfetNet a, b, p;              // Sea Blue Q1 + EQ, Sea Blue Q2, Baby Pink
        int rTrebUp = -1, rTrebDn = -1, rBassUp = -1, rBassDn = -1, rBoost = -1, outA = -1, outB = -1, outP = -1;
        double lastTreble = -1, lastBass = -1, lastBoost = -1, lastMix = -1;
        unsigned counter = 0;
    };
    std::array<Ch, 2> ch;

    static double audioTaper(double x) { return (std::pow(81.0, x) - 1.0) / 80.0; }   // "A": 10 % at noon

private:
    double sr = 48000.0, T = 1.0 / 96000.0;
    bool boostFirst = false, eqIdle = false;
    juce::SmoothedValue<float> eqMix, boostMix, volGain, orderGain;
    std::atomic<float> *treble{}, *bass{}, *level{}, *boost{}, *order{};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    static constexpr double eqVolume = 0.35;           // Sea Blue VOLUME (fixed): EQ section ~unity at 1 kHz, LOW / HIGH at noon
    static constexpr float levelMax = 2.8f, defaultLevel = 1.0f / levelMax;   // master LEVEL: x0 .. x2.8, unity by default
    static float levelGain(float p) { return levelMax * p; }
    static constexpr double potR = 50e3, minR = 10.0;

    void buildChannel(Ch& c)
    {
        // --- Sea Blue Q1 + passive EQ ---------------------------------------------------------------------------
        auto& a = c.a; a = {};
        a.T = T; a.vdd = 8.64; a.idss = 3.70e-3; a.vp = -2.28;               // 0.127 mA at Vgs -1.86 V, gm 0.6 mS
        const int n1 = a.addNode(), g1 = a.addNode(), d1 = a.addNode(), s1 = a.addNode(), s1b = a.addNode();
        const int nA = a.addNode(), t3 = a.addNode(), t1 = a.addNode(), nO = a.addNode(), b3 = a.addNode(), nM = a.addNode(), b1 = a.addNode();
        a.R(2, n1, 14.7e3); a.C(n1, g1, 22e-9); a.R(g1, 0, 1e6);
        a.R(d1, 1, 14.7e3); a.C(d1, 1, 1e-9);
        a.R(s1, 0, 14.7e3); a.C(s1, s1b, 22e-6); a.R(s1b, 0, 1e3);
        a.C(d1, nA, 470e-9);
        a.C(nA, t3, 4.7e-9); a.R(nA, t3, 47e3);
        c.rTrebUp = a.R(t3, nO, potR * 0.5); c.rTrebDn = a.R(nO, t1, potR * 0.5); a.C(t1, 0, 22e-9);
        a.R(nA, b3, 5.6e3);
        c.rBassUp = a.R(b3, nM, potR * 0.9); a.C(b3, nM, 22e-9);
        c.rBassDn = a.R(nM, b1, potR * 0.1); a.C(nM, b1, 220e-9);
        a.R(b1, 0, 1e3);
        a.R(nM, nO, 5.6e3);
        a.R(nO, 0, 1e6);                                                    // Q2's gate resistor
        a.gate = g1; a.drain = d1; a.source = s1; c.outA = nO;

        // --- Sea Blue Q2 (gate = this network's input) -------------------------------------------------------------
        auto& b = c.b; b = {};
        b.T = T; b.vdd = 8.64; b.idss = 4.02e-3; b.vp = -2.478;             // 0.137 mA at Vgs -2.02 V, gm 0.6 mS
        const int d2 = b.addNode(), s2 = b.addNode(), o2 = b.addNode();
        b.R(d2, 1, 14.7e3); b.C(d2, 1, 1e-9);
        b.R(s2, 0, 14.7e3); b.C(s2, 0, 1e-6);
        b.C(d2, o2, 1e-6); b.R(o2, 0, potR);                                // VOLUME pot (wiper ratio applied after)
        b.gate = 2; b.drain = d2; b.source = s2; c.outB = o2;

        // --- Baby Pink (2N5952) -------------------------------------------------------------------------------------
        auto& p = c.p; p = {};
        p.T = T; p.vdd = 8.64; p.idss = 3.0e-3; p.vp = -2.0;                // 0.13 mA, gm ~0.63 mS
        const int pn = p.addNode(), pg = p.addNode(), pd = p.addNode(), ps = p.addNode(), pc = p.addNode(), pl = p.addNode(), po = p.addNode();
        p.C(2, pn, 47e-9); p.R(pn, pg, 14.7e3); p.R(pg, 0, 2e6);
        p.R(pd, 1, 14.7e3); p.C(pd, ps, 100e-12);
        p.R(ps, 0, 12e3); p.C(ps, pc, 22e-6); p.R(pc, pl, 1e3);
        c.rBoost = p.R(pl, 0, 200e3);
        p.C(pd, po, 1e-6); p.R(po, 0, 150e3);
        p.gate = pg; p.drain = pd; p.source = ps; c.outP = po;
    }

    // Pot values -> resistors; rebuild a network only when something changed. bm = boost footswitch mix (0 = off).
    void updatePots(Ch& c, bool force, float bm = -1.0f)
    {
        const double t = treble != nullptr ? (double) treble->load() : 0.5;
        const double bs = bass   != nullptr ? (double) bass->load()   : 0.5;
        const double bo = boost  != nullptr ? (double) boost->load()  : 0.5;
        const double mix = bm >= 0.0f ? (double) bm : (secondaryOn() ? 1.0 : 0.0);
        if (force || t != c.lastTreble || bs != c.lastBass)
        {
            c.lastTreble = t; c.lastBass = bs;
            c.a.res[(size_t) c.rTrebUp].r = juce::jmax(minR, potR * (1.0 - t));      // B taper; up = wiper toward lug 3
            c.a.res[(size_t) c.rTrebDn].r = juce::jmax(minR, potR * t);
            const double lower = potR * audioTaper(bs);                              // A taper; up = more bass
            c.a.res[(size_t) c.rBassUp].r = juce::jmax(minR, potR - lower);
            c.a.res[(size_t) c.rBassDn].r = juce::jmax(minR, lower);
            c.a.build(false);
        }
        if (force || bo != c.lastBoost || mix != c.lastMix)
        {
            c.lastBoost = bo; c.lastMix = mix;
            // engaged: 1k + (BOOST lug 3 - wiper), "C" (reverse log) taper, clockwise = less resistance = more boost.
            // off: the wiper floats -> lug 3 to ground through the whole 50k + R10 150k. Blend conductances while switching.
            const double gOn = 1.0 / juce::jmax(minR, potR * audioTaper(1.0 - bo)), gOff = 1.0 / (potR + 150e3);
            c.p.res[(size_t) c.rBoost].r = 1.0 / (mix * gOn + (1.0 - mix) * gOff);
            c.p.build(false);
        }
    }

    void initBias(Ch& c) { c.a.solveBias(); c.b.solveBias(); c.p.solveBias(); }

    // input volts -> EQ section output volts (VOLUME wiper)
    double eqSection(Ch& c, double in)
    {
        c.a.step(in);
        c.b.step(c.a.x[(size_t) c.outA]);
        return c.b.x[(size_t) c.outB] * eqVolume;                             // VOLUME pot wiper (fixed), unloaded
    }
    double pinkStage(Ch& c, double in)
    {
        c.p.step(in);
        return c.p.x[(size_t) c.outP];
    }
};
