#pragma once
#include "PedalBase.h"

// Slow Burn: a tube distortion built on the Soldano Super Lead Overdrive pedal (Soldano's SLO-100 in a box),
// shown under our own name and artwork. Unlike the real pedal, which is all op-amps, Slow Burn puts a real
// 12AX7 (ECC83), the SLO-100's preamp valve, where the pedal's two big gain stages imitate the amp's cascaded
// triodes.
//
// The real pedal was traced from a stripboard layout (schematics/Soldano/Soldano SLO Pedal.png; netlist check in
// Tools/Analysis/slo_stripboard_netlist.py; write-up in schematics/Soldano/SLO_Pedal_trace.md). It runs its
// TL072s on +/-9 V (TC1044 inverter). Signal path, with what Slow Burn keeps (=) or changes (*):
//
//  = INPUT BUFFER  IC1B unity follower.
//  = INTERSTAGE    buffer -> 1n -> (470k || 2n2) -> GAIN (A500k) lug 3, lug 1 -> 4k7 -> ground, 1n "bright"
//                  cap lug 3 -> wiper. The 1n coupling cap cuts the lows (~160 Hz), the SLO's tight bottom end.
//  * GAIN STAGE 1  real pedal: IC2A x148 (1M || 100p over 6k8 + 1u: 23 Hz .. 1.6 kHz), two 5.6 V zeners
//                  across the feedback (~+/-6.2 V limit).
//                  Slow Burn: the same op-amp network with 22k + 330n instead of 6k8 + 1u (x46, same corners)
//                  drives V1A through a 68k grid stopper; the zeners are gone, the triode does the clipping.
//                  V1A: 12AX7, B+ 250 V, 100k plate, 1k5 || 10u cathode.
//  = COUPLING      22n -> 330k to ground -> 220k (here: V1B's grid stopper; grid current charges the 22n =
//                  real valve "blocking" compression).
//  * GAIN STAGE 2  real pedal: IC3A x48 (470k || 100p over 10k + 1u: 16 Hz .. 3.4 kHz), rail clipping.
//                  Slow Burn: V1B, 12AX7, 100k plate, 1k5 || 10u cathode, 470p plate cap (gentle top roll-off), then
//                  22n into the 1M grid of a cathode follower that drives the tone stack (as in the amp).
//  = TONE STACK    from the stage-2 output: 470p -> TREBLE (B250k) lug 3; 47k slope -> node T; T -> 22n ->
//                  TREBLE lug 1 = BASS wiper; T -> 22n -> MID wiper; BASS (A1M) lug 1 -> MID (B25k) lug 3,
//                  MID lug 1 to ground; plus 1n + 10k from the stack input to the TREBLE wiper.
//                  TREBLE wiper -> 1M -> IC3B "+", 33k to ground (the master-volume load).
//  = POWER STAGE   IC3B: out -> (33k || 22n) -> D -> 10k -> "-", 4k7 to ground; PRESENCE (C25k rheostat) from
//                  "-" via 100n to 1k || (44n + 220R) to ground; DEEP switches 22k across 33k || 22n
//                  (in = less bass; DEEP on = 22k out = full low end).
//  = OUTPUT        1u -> 10k -> VOLUME (B25k) with 22n + 1k shunt (~ -20 dB shelf above 660 Hz, a simple
//                  speaker-style roll-off).
//
// Processing: the linear front end (buffer, interstage network, op-amp filter) runs at the host rate; the
// op-amp's +/-7.5 V output swing and both triodes run 4x oversampled (Koren 12AX7 model with grid current,
// Miller capacitance and cathode bypass; backward Euler + 3-unknown Newton per triode); the tone stack, power
// stage and output network are linear and run at the host rate as small nodal (MNA) circuits.
class SlowBurn : public PedalBase
{
public:
    juce::String getId() const override { return "sb"; }
    juce::String getName() const override { return "Slow Burn"; }
    PedalCategory getCategory() const override { return PedalCategory::Distortion; }
    juce::String getBasedOn() const override
    {
        return "Soldano Super Lead Overdrive pedal (the SLO-100 amp in a box), here with a 12AX7 valve in its two "
               "main gain stages";
    }

    // Tooltip text for each control (the editor adds the control's name on top).
    juce::String describe(const juce::String& id) const override
    {
        if (id == "sb_bypass") return "Footswitch: turns Slow Burn on or off.";
        if (id == "sb_gain") return "How hard the 12AX7 valve stages are driven: from edge-of-breakup crunch to saturated lead.";
        if (id == "sb_presence") return "Upper-treble lift in the power stage: adds sparkle and cut.";
        if (id == "sb_volume") return "Output level of the pedal.";
        if (id == "sb_bass") return "Low frequencies in the SLO-style tone stack.";
        if (id == "sb_mid") return "Mid frequencies in the tone stack. Down = scooped, up = thicker and more forward.";
        if (id == "sb_treble") return "High frequencies in the tone stack.";
        if (id == "sb_deep") return "Up = full low end (like the amp's Depth switch on), down = tighter, leaner bass.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"sb_bypass",1}, "Slow Burn Bypass", false));
        l.add(std::make_unique<P>(juce::ParameterID{"sb_gain",1},     "Gain",     0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"sb_presence",1}, "Presence", 0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"sb_volume",1},   "Volume",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"sb_bass",1},     "Bass",     0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"sb_mid",1},      "Middle",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"sb_treble",1},   "Treble",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"sb_deep",1}, "Deep", false));
        // Top row (big knobs) then bottom row (small knobs, the EQ), left to right on the face.
        knobIds = { "sb_gain", "sb_presence", "sb_volume", "sb_bass", "sb_mid", "sb_treble" };
        toggleId = "sb_deep"; toggleLabel = "DEEP";
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam = s.getRawParameterValue("sb_bypass");
        gain     = s.getRawParameterValue("sb_gain");
        presence = s.getRawParameterValue("sb_presence");
        volume   = s.getRawParameterValue("sb_volume");
        bass     = s.getRawParameterValue("sb_bass");
        mid      = s.getRawParameterValue("sb_mid");
        treble   = s.getRawParameterValue("sb_treble");
        deep     = s.getRawParameterValue("sb_deep");
    }

    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 4x
        oversampler->initProcessing(spec.maximumBlockSize);
        h = 1.0 / (sr * 4.0);
        v1a.setup(h, false); v1b.setup(h, true);
        gDc = coeff(5.0, sr);
        for (auto* sm : { &gainSm, &presSm, &volSm, &bassSm, &midSm, &trebSm, &deepSm }) sm->reset(sr, 0.03);
        if (gain != nullptr) { targets(); snap(); }
        rebuild(true);
        reset();
    }
    void reset() override
    {
        for (auto& c : ch)
        {
            c.inNet.clearState(); c.stack.clearState(); c.power.clearState(); c.out.clearState();
            c.stage1.z1 = c.stage1.z2 = 0.0; c.dc = 0.0f;
            v1a.initDc(c.ta); v1b.initDc(c.tb);
        }
        if (oversampler) oversampler->reset();
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        targets();
        const int n = buf.getNumSamples();
        const int nc = juce::jmin(buf.getNumChannels(), (int) ch.size());
        juce::dsp::AudioBlock<float> whole(buf);

        // The whole pedal runs in chunks of <= 32 samples, so every stage (also the tone stack, power stage and VOLUME
        // after the valves) follows a moving knob every 32 samples, whatever the host's buffer size.
        for (int i0 = 0; i0 < n; i0 += 32)
        {
            const int len = juce::jmin(32, n - i0);
            const double vol0 = volFrac;
            advanceSmoothers(len);
            rebuild(false);
            const double vol1 = volFrac;

            // ---- host rate: buffer + interstage network + stage-1 op-amp filter (linear)
            for (int c = 0; c < nc; ++c)
            {
                auto& s = ch[(size_t) c];
                auto* x = buf.getWritePointer(c);
                for (int i = i0; i < i0 + len; ++i)
                {
                    const double vin = (double) x[i] * 0.5;              // full scale ~ 0.5 V guitar
                    s.inNet.step(vin);
                    const double w = s.inNet.node(inW);                  // gain-pot wiper = op-amp "+"
                    x[i] = (float) s.stage1.process(w);                 // op-amp output before its swing limit
                }
            }

            // ---- 4x: op-amp swing, V1A, V1B
            auto sub = whole.getSubBlock((size_t) i0, (size_t) len);
            auto up = oversampler->processSamplesUp(sub);
            for (size_t c = 0; c < up.getNumChannels() && c < ch.size(); ++c)
            {
                auto& s = ch[c];
                auto* x = up.getChannelPointer(c);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    double d;
                    const double u = softRail((double) x[i], opRail, d);    // TL072 on +/-9 V
                    // V1A, loaded by 22n -> (330k || 220k to V1B's grid, previous sample's grid voltage)
                    const double gnA = 1.0 / 330e3 + 1.0 / 220e3, jA = s.tb.vg / 220e3;
                    const double iA = v1a.tick(s.ta, u, gnA, jA);
                    const double vc = (iA + jA) / gnA;                       // coupling node (330k / 220k)
                    // V1B, grid via 220k from the coupling node; load = 22n into the cathode follower's 1M
                    const double iB = v1b.tick(s.tb, vc, 1.0 / 1.0e6, 0.0);
                    x[i] = (float) (iB * 1.0e6 * cfScale);                   // cathode follower -> stack drive
                }
            }
            oversampler->processSamplesDown(sub);

            // ---- host rate: tone stack, power stage, output network (linear), VOLUME ramped across the chunk
            for (int c = 0; c < nc; ++c)
            {
                auto& s = ch[(size_t) c];
                auto* x = buf.getWritePointer(c);
                for (int i = i0; i < i0 + len; ++i)
                {
                    s.stack.step((double) x[i]);
                    s.power.step(s.stack.node(stP));
                    s.out.step(s.power.node(pwO));
                    const double vol = vol0 + (vol1 - vol0) * (double) (i - i0 + 1) / (double) len;
                    float o = (float) (s.out.node(outB) * vol * levelScale);
                    o -= lowpass(s.dc, o, gDc);
                    x[i] = o;
                }
            }
        }
    }

    // ---- tapers (exposed for tests)
    static double audioTaper(double x) { return (std::pow(10.0, 2.0 * x) - 1.0) / 99.0; }   // ~9 % at noon

    // ------------------------------------------------------------------------------------------
    // Small linear circuit solved by modified nodal analysis, trapezoidal capacitors. Node 0 = ground,
    // node 1 = an ideal voltage source (the input). Ideal op-amps are nullors (V+ = V-, output current free).
    // The matrix is inverted when values change (pots), so a step is one matrix-vector product.
    struct Mna
    {
        static constexpr int maxN = 14;
        struct Rel { int a, b; double v; };
        struct Cap { int a, b; double c, v = 0, i = 0, g = 0; };
        struct Amp { int p, n, o; };
        int nodes = 1;                       // circuit nodes excluding ground (node 1 = source)
        std::vector<Rel> res;
        std::vector<Cap> caps;
        std::vector<Amp> amps;
        double T = 1.0 / 48000.0;
        std::array<std::array<double, maxN>, maxN> inv {};
        std::array<double, maxN> x {};
        int size() const { return nodes + 1 + (int) amps.size(); }   // + source current + op-amp currents
        int addNode() { return ++nodes; }
        int R(int a, int b, double ohms) { res.push_back({ a, b, ohms }); return (int) res.size() - 1; }
        int C(int a, int b, double farads) { caps.push_back({ a, b, farads }); return (int) caps.size() - 1; }
        void opamp(int p, int n, int o) { amps.push_back({ p, n, o }); }
        double node(int k) const { return k == 0 ? 0.0 : x[(size_t) k - 1]; }
        void clearState() { for (auto& c : caps) c.v = c.i = 0; x.fill(0.0); }

        void build()
        {
            const int N = size();
            std::array<std::array<double, 2 * maxN>, maxN> m {};
            auto stamp = [&](int a, int b, double g)
            {
                if (a > 0) m[(size_t) a - 1][(size_t) a - 1] += g;
                if (b > 0) m[(size_t) b - 1][(size_t) b - 1] += g;
                if (a > 0 && b > 0) { m[(size_t) a - 1][(size_t) b - 1] -= g; m[(size_t) b - 1][(size_t) a - 1] -= g; }
            };
            for (auto& r : res)  stamp(r.a, r.b, 1.0 / r.v);
            for (auto& c : caps) { c.g = 2.0 * c.c / T; stamp(c.a, c.b, c.g); }
            const int ks = nodes;                                   // source row/column
            m[0][(size_t) ks] += 1.0; m[(size_t) ks][0] += 1.0;
            for (size_t k = 0; k < amps.size(); ++k)
            {
                const auto& a = amps[k];
                const size_t r = (size_t) nodes + 1 + k;
                m[(size_t) a.o - 1][r] += 1.0;                     // output current enters at the output node
                if (a.p > 0) m[r][(size_t) a.p - 1] += 1.0;
                if (a.n > 0) m[r][(size_t) a.n - 1] -= 1.0;
            }
            // Gauss-Jordan inverse
            for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) m[(size_t) i][(size_t) (N + j)] = i == j ? 1.0 : 0.0;
            for (int col = 0; col < N; ++col)
            {
                int piv = col;
                for (int r = col + 1; r < N; ++r) if (std::abs(m[(size_t) r][(size_t) col]) > std::abs(m[(size_t) piv][(size_t) col])) piv = r;
                std::swap(m[(size_t) col], m[(size_t) piv]);
                const double d = 1.0 / m[(size_t) col][(size_t) col];
                for (int j = 0; j < 2 * N; ++j) m[(size_t) col][(size_t) j] *= d;
                for (int r = 0; r < N; ++r)
                    if (r != col && m[(size_t) r][(size_t) col] != 0.0)
                    {
                        const double f = m[(size_t) r][(size_t) col];
                        for (int j = 0; j < 2 * N; ++j) m[(size_t) r][(size_t) j] -= f * m[(size_t) col][(size_t) j];
                    }
            }
            for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) inv[(size_t) i][(size_t) j] = m[(size_t) i][(size_t) (N + j)];
        }
        void step(double vin)
        {
            const int N = size();
            std::array<double, maxN> rhs {};
            for (auto& c : caps)
            {
                const double ieq = c.g * c.v + c.i;     // trapezoidal companion
                c.i = ieq;                                // kept for the update below
                if (c.a > 0) rhs[(size_t) c.a - 1] += ieq;
                if (c.b > 0) rhs[(size_t) c.b - 1] -= ieq;
            }
            rhs[(size_t) nodes] = vin;
            for (int i = 0; i < N; ++i)
            {
                double acc = 0.0;
                for (int j = 0; j < N; ++j) acc += inv[(size_t) i][(size_t) j] * rhs[(size_t) j];
                x[(size_t) i] = acc;
            }
            for (auto& c : caps)
            {
                const double ieq = c.i;
                c.v = node(c.a) - node(c.b);
                c.i = c.g * c.v - ieq;
            }
        }
    };

    // ------------------------------------------------------------------------------------------
    // One 12AX7 triode, common cathode: grid stopper from a source, Miller (grid-plate) and grid-cathode
    // capacitance, plate resistor to B+, optional plate cap to ground, cathode resistor || bypass cap, and a
    // coupling cap into a load node (conductance gn to ground plus a known current jn drawn from it).
    // Koren model (mu 100, ex 1.4, Kg1 1060, Kp 600, Kvb 300); grid current = smooth ~1k5 diode above ~0.2 V.
    struct TriodeState { double vg = 0, vp = 0, vk = 0, vcc = 0, vkc = 0, vpc = 0, qgp = 0, qgk = 0; };
    struct Triode
    {
        double Rgs = 68e3, B = 250.0, Rp = 100e3, Rk = 1500.0, Ck = 2.2e-6, Cpl = 0.0, Cc = 22e-9;
        double h = 1.0 / 192000.0;
        static constexpr double mu = 100.0, ex = 1.4, Kg1 = 1060.0, Kp = 600.0, Kvb = 300.0;
        static constexpr double Cgp = 1.7e-12, Cgk = 1.6e-12, Rgk = 1500.0, Vg0 = 0.2, sG = 0.12;

        void setup(double step, bool second)
        {
            h = step;
            if (second) { Rgs = 220e3; Ck = 10e-6; Cpl = 470e-12; }
            else        { Rgs = 68e3;  Ck = 10e-6; Cpl = 0.0; }
        }
        // plate current and its derivatives (d/dVgk, d/dVpk)
        static double plate(double vgk, double vpk, double& gm, double& gp)
        {
            vpk = juce::jmax(vpk, 1e-3);
            const double q = std::sqrt(Kvb + vpk * vpk);
            const double a = Kp * (1.0 / mu + vgk / q);
            double sp, sg;
            if (a > 30.0) { sp = a; sg = 1.0; }
            else if (a < -30.0) { sp = sg = std::exp(a); }
            else { const double e = std::exp(a); sp = std::log1p(e); sg = e / (1.0 + e); }
            const double e1 = vpk / Kp * sp;
            if (e1 <= 1e-12) { gm = gp = 0.0; return 0.0; }
            const double pw = std::pow(e1, ex - 1.0);
            const double ip = 2.0 * pw * e1 / Kg1, dip = 2.0 * ex * pw / Kg1;
            gm = dip * vpk * sg / q;
            gp = dip * (sp / Kp - sg * vgk * vpk * vpk / (q * q * q));
            return ip;
        }
        static double grid(double vgk, double& g)
        {
            const double z = (vgk - Vg0) / sG;
            if (z > 30.0) { g = 1.0 / Rgk; return sG * z / Rgk; }
            const double e = std::exp(juce::jmax(z, -40.0));
            g = e / (1.0 + e) / Rgk;
            return sG * std::log1p(e) / Rgk;
        }
        // DC operating point (grid at 0 V, caps open)
        void initDc(TriodeState& s) const
        {
            double vp = B * 0.6, vk = 1.0;
            for (int it = 0; it < 60; ++it)
            {
                double gm, gp;
                const double ip = plate(-vk, vp - vk, gm, gp);
                const double F1 = (B - vp) / Rp - ip, F2 = ip - vk / Rk;
                const double a11 = -1.0 / Rp - gp, a12 = gm + gp;          // d/dvp, d/dvk
                const double a21 = gp, a22 = -gm - gp - 1.0 / Rk;
                const double det = a11 * a22 - a12 * a21;
                const double dp = (F1 * a22 - a12 * F2) / det, dk = (a11 * F2 - a21 * F1) / det;
                vp -= juce::jlimit(-20.0, 20.0, dp); vk -= juce::jlimit(-0.5, 0.5, dk);
                if (std::abs(dp) + std::abs(dk) < 1e-9) break;
            }
            s = {};
            s.vg = 0.0; s.vp = vp; s.vk = vk; s.vcc = vp; s.vkc = vk; s.vpc = vp; s.qgp = -vp; s.qgk = -vk;
        }
        // One sample: source voltage vs; returns the current through the coupling cap into the load node.
        double tick(TriodeState& s, double vs, double gn, double jn) const
        {
            const double cgp = Cgp / h, cgk = Cgk / h, ck = Ck / h, cpl = Cpl / h;
            const double geq = 1.0 / (h / Cc + 1.0 / gn), off = s.vcc + jn / gn;   // icc = geq (vp - off)
            double vg = s.vg, vp = s.vp, vk = s.vk;
            for (int it = 0; it < 10; ++it)
            {
                double gm, gp, gI;
                const double ip = plate(vg - vk, vp - vk, gm, gp);
                const double ig = grid(vg - vk, gI);
                const double mil = cgp * ((vg - vp) - s.qgp), gk = cgk * ((vg - vk) - s.qgk);
                const double F1 = (vs - vg) / Rgs - ig - mil - gk;
                const double F2 = (B - vp) / Rp - ip - geq * (vp - off) - cpl * (vp - s.vpc) + mil;
                const double F3 = ip + ig + gk - vk / Rk - ck * (vk - s.vkc);
                const double a11 = -1.0 / Rgs - gI - cgp - cgk, a12 = cgp, a13 = gI + cgk;
                const double a21 = -gm + cgp, a22 = -1.0 / Rp - gp - geq - cpl - cgp, a23 = gm + gp;
                const double a31 = gm + gI + cgk, a32 = gp, a33 = -gm - gp - gI - cgk - 1.0 / Rk - ck;
                const double det = a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31) + a13 * (a21 * a32 - a22 * a31);
                const double d1 = (F1 * (a22 * a33 - a23 * a32) - a12 * (F2 * a33 - a23 * F3) + a13 * (F2 * a32 - a22 * F3)) / det;
                const double d2 = (a11 * (F2 * a33 - a23 * F3) - F1 * (a21 * a33 - a23 * a31) + a13 * (a21 * F3 - F2 * a31)) / det;
                const double d3 = (a11 * (a22 * F3 - F2 * a32) - a12 * (a21 * F3 - F2 * a31) + F1 * (a21 * a32 - a22 * a31)) / det;
                vg -= juce::jlimit(-2.0, 2.0, d1);
                vp -= juce::jlimit(-25.0, 25.0, d2);
                vk -= juce::jlimit(-1.0, 1.0, d3);
                if (std::abs(d1) + std::abs(d2) * 0.01 + std::abs(d3) < 1e-5) break;
            }
            const double icc = geq * (vp - off);
            s.vcc += h / Cc * icc;
            s.vg = vg; s.vp = vp; s.vk = vk; s.vkc = vk; s.vpc = vp;
            s.qgp = vg - vp; s.qgk = vg - vk;
            return icc;
        }
    };

private:
    // stage-1 op-amp: non-inverting, Zf = 1M || 100p, Zg = 22k + 330n -> x46, 22 Hz .. 1.6 kHz (bilinear)
    struct OpStage
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void design(double fs)
        {
            constexpr double Rf = 1.0e6, Cf = 100e-12, Rg = 22e3, Cg = 330e-9;
            // H(s) = 1 + Rf Cg s / ((1 + s Rf Cf)(1 + s Rg Cg)) = (B2 s^2 + B1 s + 1) / (A2 s^2 + A1 s + 1)
            const double A2 = Rf * Cf * Rg * Cg, A1 = Rf * Cf + Rg * Cg, B2 = A2, B1 = A1 + Rf * Cg;
            const double k = 2.0 * fs, k2 = k * k;
            const double a0 = A2 * k2 + A1 * k + 1.0;
            b0 = (B2 * k2 + B1 * k + 1.0) / a0; b1 = (2.0 - 2.0 * B2 * k2) / a0; b2 = (B2 * k2 - B1 * k + 1.0) / a0;
            a1 = (2.0 - 2.0 * A2 * k2) / a0; a2 = (A2 * k2 - A1 * k + 1.0) / a0;
        }
        double process(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };

    struct Ch
    {
        Mna inNet, stack, power, out;
        OpStage stage1;
        TriodeState ta, tb;
        float dc = 0;
    };
    std::array<Ch, 2> ch;
    Triode v1a, v1b;
    double sr = 48000.0, h = 1.0 / 192000.0;
    float gDc = 0;
    double volFrac = 0.5;
    juce::SmoothedValue<double> gainSm, presSm, volSm, bassSm, midSm, trebSm, deepSm;
    std::atomic<float> *gain{}, *presence{}, *volume{}, *bass{}, *mid{}, *treble{}, *deep{};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    double lastG = -1, lastP = -1, lastB = -1, lastM = -1, lastT = -1, lastD = -1;

    static constexpr double opRail = 7.5;        // TL072 output swing on +/-9 V
    static constexpr double cfScale = 0.075;     // stage-2 swing (~ +/-100 V) scaled to the op-amp level the stack expects
    static constexpr float levelScale = 0.73f;   // volts -> digital; calibrated: same loudness as the Valve Howler at noon (-18 dBFS sine in)

    // node / element indices (set in buildCircuits)
    int inW = 0, inRa = 0, inRb = 0;
    int stP = 0, stTt = 0, stTb = 0, stBass = 0, stMt = 0, stMb = 0;
    int pwO = 0, pwPres = 0, pwDeep = 0;
    int outB = 0;

    void targets()
    {
        gainSm.setTargetValue(gain->load()); presSm.setTargetValue(presence->load()); volSm.setTargetValue(volume->load());
        bassSm.setTargetValue(bass->load()); midSm.setTargetValue(mid->load()); trebSm.setTargetValue(treble->load());
        deepSm.setTargetValue(deep->load() >= 0.5f ? 1.0 : 0.0);
    }
    void snap() { for (auto* sm : { &gainSm, &presSm, &volSm, &bassSm, &midSm, &trebSm, &deepSm }) sm->setCurrentAndTargetValue(sm->getTargetValue()); }
    void advanceSmoothers(int len) { for (auto* sm : { &gainSm, &presSm, &volSm, &bassSm, &midSm, &trebSm, &deepSm }) sm->skip(len); }

    void buildCircuits(Ch& c)
    {
        const double T = 1.0 / sr;
        // interstage: 1 = buffer out, 2 = Y, 3 = gain lug 3, 4 = wiper, 5 = lug 1
        auto& a = c.inNet; a = {}; a.T = T;
        const int y = a.addNode(), z = a.addNode(), w = a.addNode(), l1 = a.addNode();
        a.C(1, y, 1e-9); a.R(y, z, 470e3); a.C(y, z, 2.2e-9);
        inRa = a.R(z, w, 250e3); a.C(z, w, 1e-9); inRb = a.R(w, l1, 250e3); a.R(l1, 0, 4.7e3);
        inW = w;
        // tone stack + master load: 1 = stack drive
        auto& s = c.stack; s = {}; s.T = T;
        const int t3 = s.addNode(), tw = s.addNode(), t1 = s.addNode(), tn = s.addNode(), mw = s.addNode(),
                  b1 = s.addNode(), n1 = s.addNode(), p = s.addNode();
        s.C(1, t3, 470e-12); stTt = s.R(t3, tw, 125e3); stTb = s.R(tw, t1, 125e3);
        s.R(1, tn, 47e3); s.C(tn, t1, 22e-9); s.C(tn, mw, 22e-9);
        stBass = s.R(t1, b1, 100e3); stMt = s.R(b1, mw, 12.5e3); stMb = s.R(mw, 0, 12.5e3);
        s.C(1, n1, 1e-9); s.R(n1, tw, 10e3);
        s.R(tw, p, 1.0e6); s.R(p, 0, 33e3);
        stP = p;
        // power stage: 1 = "+" input
        auto& q = c.power; q = {}; q.T = T;
        const int nn = q.addNode(), o = q.addNode(), d = q.addNode(), p1 = q.addNode(), qn = q.addNode(), rn = q.addNode();
        q.opamp(1, nn, o);
        q.R(o, d, 33e3); q.C(o, d, 22e-9); pwDeep = q.R(o, d, 22e3);
        q.R(d, nn, 10e3); q.R(nn, 0, 4.7e3);
        q.C(nn, p1, 100e-9); pwPres = q.R(p1, qn, 12.5e3); q.R(qn, 0, 1e3); q.C(qn, rn, 44e-9); q.R(rn, 0, 220.0);
        pwO = o;
        // output: 1 = power-stage output
        auto& u = c.out; u = {}; u.T = T;
        const int ua = u.addNode(), ub = u.addNode(), us = u.addNode();
        u.C(1, ua, 1e-6); u.R(ua, ub, 10e3); u.C(ub, us, 22e-9); u.R(us, 0, 1e3); u.R(ub, 0, 25e3);
        outB = ub;
        c.stage1.design(sr);
    }

    void rebuild(bool force)
    {
        if (force) for (auto& c : ch) buildCircuits(c);
        const double g = gainSm.getCurrentValue(), p = presSm.getCurrentValue(), b = bassSm.getCurrentValue(),
                     m = midSm.getCurrentValue(), t = trebSm.getCurrentValue(), dp = deepSm.getCurrentValue();
        volFrac = juce::jlimit(0.0, 1.0, volSm.getCurrentValue());              // 25kB, wiper unloaded
        if (! force && g == lastG && p == lastP && b == lastB && m == lastM && t == lastT && dp == lastD) return;
        lastG = g; lastP = p; lastB = b; lastM = m; lastT = t; lastD = dp;
        const double ga = audioTaper(g);                                         // GAIN A500k
        const double rTreb = 250e3, rMid = 25e3;
        for (auto& c : ch)
        {
            c.inNet.res[(size_t) inRa].v = 500e3 * (1.0 - ga) + 100.0;
            c.inNet.res[(size_t) inRb].v = 500e3 * ga + 100.0;
            c.inNet.build();
            c.stack.res[(size_t) stTt].v = rTreb * (1.0 - t) + 50.0;              // TREBLE B250k
            c.stack.res[(size_t) stTb].v = rTreb * t + 50.0;
            c.stack.res[(size_t) stBass].v = 1.0e6 * audioTaper(b) + 50.0;        // BASS A1M, CW = more bass
            c.stack.res[(size_t) stMt].v = rMid * (1.0 - m) + 10.0;               // MID B25k, CW = more mids
            c.stack.res[(size_t) stMb].v = rMid * m + 10.0;
            c.stack.build();
            c.power.res[(size_t) pwPres].v = 25e3 * audioTaper(1.0 - p) + 50.0;    // PRESENCE C25k rheostat, CW = 0
            c.power.res[(size_t) pwDeep].v = 22e3 + dp * 1.0e12;                  // DEEP on = 22k out of circuit
            c.power.build();
            c.out.build();
        }
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
