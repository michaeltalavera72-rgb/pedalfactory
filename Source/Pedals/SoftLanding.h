#pragma once
#include "PedalBase.h"

// Soft Landing: a model of the Toadworks Mr. Squishy (shown under our own name), a discrete JFET compressor.
// Built from Michael's PDF of Timm H.'s drawing (Rev 1.0, 3/26/2012); full trace in claude/mr-squishy-trace.md,
// decisions in claude/soft-landing-plan.md, small-signal reference in Tools/Analysis/mr_squishy.py.
//
//   Input   GAIN (A500K: lug 3 = input, lug 1 = 0 V) with a 470p treble bleed from the input to the wiper (drawn "?":
//           value chosen by Michael), 4M7 to 0 V at the wiper.
//   Cell    wiper -> 0.1u -> node B -> 0.1u -> U1A "+" (470k / 470k bias = 235k). Q2 (2N5457) from B to node N is a
//           voltage-controlled SHUNT resistor: N is held at a DC voltage V_N and bypassed by 4u7, B has no other DC
//           path, so Q2 sits at Vds = 0 (ohmic region). Gate: 470k from the control node Vc, 470k + 560p from the
//           drain (= gate follows half the drain swing above ~300 Hz: the usual JFET-VCR linearisation).
//           Q2's gate junction clamps it at about +0.5 V, which caps the gain reduction.
//   Bias    Q1 (2N5457, 6K2 = 1K5 + 4K7 self-bias) is a constant-current source into N; N -> 20K and SQUISH (A10K, lug 1
//           to 0 V, lug 3 -> 22K -> 0 V, wiper to N) to 0 V. V_N = I_Q1 x (20K || SQUISH network) = Q2's rest bias.
//           SQUISH up -> Q2 nearer pinch-off at rest -> more level at rest and more gain-reduction range.
//   Gain    U1A (RC4558 on 9 V) non-inverting: 220k over 10k + 4u7 = x23 (+27 dB) at audio, x1 at DC.
//   Side-chain (feedback type: detects the OUTPUT): 4u7 coupling -> node O (LEVEL A10K to 0 V) -> 2K1 -> 1N914 ->
//           Vc (4u7 || 100k, 470k to Q2's gate). Attack ~10 ms, release 0.47 s. Vc up -> Q2 conducts more -> less gain.
//   Output  LEVEL wiper.
// JFET VOICING (Michael: "good pair"): the drawing has no trimmer, so a real unit's squish depends on its two
// 2N5457s; with a matched datasheet-typical pair Q2 can never reach pinch-off at rest (V_N < |Vgs(Q1)| < |Vp|) and
// the pedal only gives a few dB of gain reduction on top of a large fixed loss. We model a pair where Q1 sets V_N
// past Q2's pinch-off near the top of SQUISH: a real threshold and plenty of squish. Behavioural constants, by ear.
//
// Model: 2x oversampled. The input / cell network (6 nodes) is a linear MNA network (trapezoidal capacitors,
// backward-Euler steps after pot rebuilds) with Q2's channel (symmetric square law) and its two gate junctions as
// nonlinear currents, solved per sample by a 3-unknown Newton (B, N, G). The detector (O, Vc + diode) is a second
// small network solved after the op-amp, using this sample's gate voltage; the cell uses the previous sample's Vc
// (a 4u7 node: one sample of lag is inaudible).
class SoftLanding : public PedalBase
{
public:
    juce::String getId() const override { return "sl"; }
    juce::String getName() const override { return "Soft Landing"; }
    PedalCategory getCategory() const override { return PedalCategory::Compressor; }
    juce::String getBasedOn() const override { return "Toadworks Mr. Squishy (discrete JFET compressor)"; }

    juce::String describe(const juce::String& id) const override
    {
        if (id == "sl_bypass") return "Turns Soft Landing on or off.";
        if (id == "sl_gain")   return "GAIN: how hard your guitar drives the compressor. More gain = earlier, deeper compression "
                                      "and more sustain (and more noise between notes). Down low it is closer to a clean leveller.";
        if (id == "sl_squish") return "SQUISH: how much the compressor can clamp down. Low = gentle levelling with most of your "
                                      "dynamics; up = flatter, longer sustain, softer pick attack. The slow (about half a second) "
                                      "release lets notes bloom back after you pick.";
        if (id == "sl_level")  return "LEVEL: output volume.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"sl_bypass",1}, "Soft Landing Bypass", false));
        l.add(std::make_unique<P>(juce::ParameterID{"sl_gain",1},   "Gain",   0.0f, 1.0f, defaultGain));
        l.add(std::make_unique<P>(juce::ParameterID{"sl_squish",1}, "Squish", 0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"sl_level",1},  "Level",  0.0f, 1.0f, 0.5f));
        knobIds = { "sl_gain", "sl_squish", "sl_level" };
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam = s.getRawParameterValue("sl_bypass");
        gain   = s.getRawParameterValue("sl_gain");
        squish = s.getRawParameterValue("sl_squish");
        level  = s.getRawParameterValue("sl_level");
    }

    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        fs = sr * 2.0;
        T = 1.0 / fs;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 2x
        oversampler->initProcessing(spec.maximumBlockSize);
        iQ1 = currentSource(q1Idss, q1Vp, 6.2e3);
        {   // SQUISH travel: the rotation where V_N reaches |Vp| + vnEnd
            double lo = 0.0, hi = 1.0;
            for (int it = 0; it < 50; ++it)
            {
                const double m = 0.5 * (lo + hi);
                (iQ1 * squishRN(m) > std::abs(q2Vp) + vnEnd ? lo : hi) = m;
            }
            squishTravel = lo;
        }
        // U1A: (1 + s 230k C) / (1 + s 10k C), C = 4u7 (bilinear)
        { const double C = 4.7e-6, k = 2.0 * fs, a = 230e3 * C * k, b = 10e3 * C * k;
          amp.b0 = (1.0 + a) / (1.0 + b); amp.b1 = (1.0 - a) / (1.0 + b); amp.a1 = (1.0 - b) / (1.0 + b); }
        levelSm.reset(fs, 0.03);
        for (auto& c : ch) { buildCell(c); buildDet(c); }
        reset();
    }
    void reset() override
    {
        const double g = gain != nullptr ? gain->load() : (double) defaultGain, q = squish != nullptr ? squish->load() : 0.5;
        levelSm.setCurrentAndTargetValue(level != nullptr ? levelGain(level->load()) : levelGain(0.5));
        for (auto& c : ch)
            if (c.cell.nodes > 0)
            {
                c.gainPos = g; c.squishPos = q;
                setPots(c);
                c.cell.build(); c.det.build();
                initState(c);
            }
        if (oversampler) oversampler->reset();
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        levelSm.setTargetValue(levelGain(level->load()));
        const double gTarget = gain->load(), qTarget = squish->load();

        juce::dsp::AudioBlock<float> block(buf);
        auto up = oversampler->processSamplesUp(block);
        const size_t nCh = juce::jmin(up.getNumChannels(), ch.size());
        const size_t n = up.getNumSamples();
        for (size_t i = 0; i < n; ++i)
        {
            const double lv = levelSm.getNextValue();
            for (size_t c = 0; c < nCh; ++c)
            {
                auto& s = ch[c];
                // pots: glide towards the knob (full turn in ~17 ms), rebuilding the network every 8 samples while moving
                if ((s.counter++ & 7) == 0 && (s.gainPos != gTarget || s.squishPos != qTarget))
                {
                    s.gainPos += juce::jlimit(-0.005, 0.005, gTarget - s.gainPos);
                    s.squishPos += juce::jlimit(-0.005, 0.005, qTarget - s.squishPos);
                    setPots(s);
                    s.cell.build();
                }
                float* x = up.getChannelPointer(c);
                x[i] = (float) (tick(s, (double) x[i] * 0.5) * lv);           // full scale ~ 0.5 V guitar
            }
        }
        oversampler->processSamplesDown(block);
    }

    // ---- exposed for tests -------------------------------------------------------------------------------------
    double controlVoltage() const { return ch[0].det.x[4]; }          // Vc (envelope cap)
    double restBias() const { return ch[0].cell.x[nN]; }              // V_N (Q2 source)
    double q1Current() const { return iQ1; }

    // Small linear MNA network. Node 0 = ground; nodes 1 .. nFix-1 are fixed voltages; the rest are unknowns.
    template <int NF, int NMAX>
    struct Net
    {
        struct Res { int a, b; double r; };
        struct Cap { int a, b; double c, v = 0, i = 0; };
        std::vector<Res> res;
        std::vector<Cap> caps;
        std::array<double, (size_t) NMAX> isrc {};                                 // fixed currents INTO each node
        int nodes = 0;
        double T = 1.0 / 96000.0;
        std::array<double, (size_t) NMAX> x {};
        struct Mat
        {
            std::array<std::array<double, (size_t) NMAX>, (size_t) NMAX> ginv {};
            std::array<std::array<double, (size_t) NF>, (size_t) NMAX> gfix {};
        };
        std::array<Mat, 2> mat;                                            // [0] trapezoidal, [1] backward Euler
        int beSteps = 0;

        int R(int a, int b, double r) { res.push_back({ a, b, r }); return (int) res.size() - 1; }
        void C(int a, int b, double c) { caps.push_back({ a, b, c }); }
        void build() { buildMat(mat[0], 2.0); buildMat(mat[1], 1.0); beSteps = 2; }
        void buildMat(Mat& mt, double capScale)
        {
            const int m = nodes - NF;
            std::array<std::array<double, (size_t) NMAX>, (size_t) NMAX> g {};
            auto stamp = [&](int a, int b, double y)
            {
                g[(size_t) a][(size_t) a] += y; g[(size_t) b][(size_t) b] += y;
                g[(size_t) a][(size_t) b] -= y; g[(size_t) b][(size_t) a] -= y;
            };
            for (auto& r : res) stamp(r.a, r.b, 1.0 / r.r);
            for (auto& c : caps) stamp(c.a, c.b, capScale * c.c / T);
            std::array<std::array<double, (size_t) (2 * NMAX)>, (size_t) NMAX> a {};
            for (int i = 0; i < m; ++i)
            {
                for (int j = 0; j < m; ++j) a[(size_t) i][(size_t) j] = g[(size_t) (i + NF)][(size_t) (j + NF)];
                a[(size_t) i][(size_t) (m + i)] = 1.0;
                for (int k = 0; k < NF; ++k) mt.gfix[(size_t) i][(size_t) k] = g[(size_t) (i + NF)][(size_t) k];
            }
            for (int col = 0; col < m; ++col)                               // Gauss-Jordan with pivoting
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
        }
        // Linear solution (no nonlinear currents) for this step into v0; fixed node voltages in fix[1..NF-1].
        // Returns the matrix used, for the Newton's transfer impedances.
        const Mat& linear(const std::array<double, (size_t) NF>& fix, std::array<double, (size_t) NMAX>& v0)
        {
            const int m = nodes - NF;
            const bool be = beSteps > 0;
            const Mat& mt = mat[be ? 1 : 0];
            const double cs = be ? 1.0 : 2.0;
            std::array<double, (size_t) NMAX> rhs {};
            for (int i = 0; i < m; ++i) rhs[(size_t) i] = isrc[(size_t) (i + NF)];
            for (auto& c : caps)
            {
                const double g = cs * c.c / T, ieq = be ? g * c.v : g * c.v + c.i;
                if (c.a >= NF) rhs[(size_t) (c.a - NF)] += ieq;
                if (c.b >= NF) rhs[(size_t) (c.b - NF)] -= ieq;
            }
            for (int i = 0; i < m; ++i)
                for (int k = 1; k < NF; ++k) rhs[(size_t) i] -= mt.gfix[(size_t) i][(size_t) k] * fix[(size_t) k];
            for (int k = 0; k < NF; ++k) v0[(size_t) k] = fix[(size_t) k];
            for (int i = 0; i < m; ++i)
            {
                double acc = 0.0;
                for (int j = 0; j < m; ++j) acc += mt.ginv[(size_t) i][(size_t) j] * rhs[(size_t) j];
                v0[(size_t) (i + NF)] = acc;
            }
            return mt;
        }
        // Transfer impedance: voltage at node a per amp injected into node b (both unknown nodes).
        static double z(const Mat& mt, int a, int b) { return mt.ginv[(size_t) (a - NF)][(size_t) (b - NF)]; }
        // Accept the step: node voltages final in x; update the capacitor histories.
        void commit()
        {
            const bool be = beSteps > 0;
            if (be) --beSteps;
            const double cs = be ? 1.0 : 2.0;
            for (auto& c : caps)
            {
                const double g = cs * c.c / T, ieq = be ? g * c.v : g * c.v + c.i;
                c.v = x[(size_t) c.a] - x[(size_t) c.b];
                c.i = g * c.v - ieq;
            }
        }
    };

    // Input / gain-cell network: fixed 0 gnd, 1 input, 2 Vc; unknowns A, B (Q2 drain), M (560p / 470k), G (gate),
    // N (Q2 source, V_N), P (U1A "+", relative to its 4.5 V bias).
    static constexpr int nIn = 1, nVc = 2, nA = 3, nB = 4, nM = 5, nG = 6, nN = 7, nP = 8;
    using CellNet = Net<3, 9>;
    // Detector network: fixed 0 gnd, 1 op-amp output, 2 gate G; unknowns O (output / LEVEL), C (Vc).
    static constexpr int dOut = 1, dG = 2, dO = 3, dC = 4;
    using DetNet = Net<3, 5>;

    // smooth op-amp output limit (k = 4)
    static double softRail(double v, double r)
    {
        const double q = v / r, q4 = q * q * q * q;
        return v / std::sqrt(std::sqrt(1.0 + q4));
    }
    static double audioTaper(double x) { return (std::pow(81.0, x) - 1.0) / 80.0; }   // "A": 10 % at noon

private:
    struct Ch
    {
        CellNet cell;
        DetNet det;
        int rTop = 0, rBot = 0, rN = 0;
        double gainPos = 0.5, squishPos = 0.5;
        double ampX = 0.0, ampY = 0.0;                                    // U1A filter state
        double dI = 0.0;                                                  // detector diode current (Newton start)
        uint32_t counter = 0;
    };
    std::array<Ch, 2> ch;
    struct { double b0 = 1, b1 = 0, a1 = 0; } amp;
    double sr = 48000.0, fs = 96000.0, T = 1.0 / 96000.0, iQ1 = 0.25e-3;
    juce::SmoothedValue<double> levelSm;
    std::atomic<float> *gain{}, *squish{}, *level{};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // ---- JFET pair ("good pair", see the header) ----
    // A high-Vp Q1 (I ~ 0.40 mA through the 6K2) and a low-Vp Q2, both inside the 2N5457 spread (Idss 1-5 mA,
    // Vp -0.5 .. -6 V): over the knob V_N runs from ~2 V (SQUISH min: Q2 pinched off until the output nears ~2 V peak =
    // close to a clean boost) through ~1.2 V at noon down to just above Q2's pinch-off (fully CW: lowest threshold,
    // deepest squish). Below that the original goes to 0 V (Q2 on at rest: big fixed loss, less range): not modelled.
    static constexpr double q1Idss = 3.3e-3, q1Vp = -3.0;             // Q1: current source (6K2 self-bias): ~0.33 mA
    static constexpr double q2Idss = 1.0e-3, q2Vp = -0.5;             // Q2: the gain-control VCR
    static constexpr double bleedC = 470e-12;                          // GAIN treble bleed (drawn "?", Michael: 470p)
    static constexpr double pinchSoft = 0.03;                          // V: width of Q2's pinch-off knee
    static constexpr double gateIs = 1e-12, gateNVt = 1.5 * 0.02585;   // Q2 gate junction
    static constexpr double dIs = 2.52e-9, dNVt = 1.752 * 0.02585;     // D1 1N914
    static constexpr double vnEnd = 0.15;                              // SQUISH fully CW: V_N = |Vp(Q2)| + this
    double squishTravel = 0.41;                                        // fraction of the pot's rotation (set in prepare)
    static constexpr double rail = 3.3;                                // RC4558 swing on 9 V (about 4.5 V +/- 3.3)
    // volts -> digital, calibrated: same loudness as the Valve Howler at noon (-18 dBFS 440 Hz sine in)
    static constexpr double levelScale = 5.9;

    static constexpr float defaultGain = 0.8f;                         // A500K: noon is only ~10 % of the input
    static double levelGain(double v) { return audioTaper(v) * levelScale; }

    // self-biased JFET current source: I = Idss (1 - I Rs / |Vp|)^2 (bisection)
    static double currentSource(double idss, double vp, double rs)
    {
        double lo = 0.0, hi = std::abs(vp) / rs;
        for (int it = 0; it < 60; ++it)
        {
            const double i = 0.5 * (lo + hi), f = i - idss * juce::square(1.0 - i * rs / std::abs(vp));
            (f > 0.0 ? hi : lo) = i;
        }
        return 0.5 * (lo + hi);
    }

    void buildCell(Ch& s)
    {
        auto& n = s.cell;
        n = CellNet {};
        n.nodes = 9; n.T = T;
        s.rTop = n.R(nIn, nA, 250e3);
        s.rBot = n.R(nA, 0, 250e3);
        n.R(nA, 0, 4.7e6);
        n.R(nM, nG, 470e3);
        n.R(nG, nVc, 470e3);
        n.R(nP, 0, 235e3);
        s.rN = n.R(nN, 0, 5e3);
        n.C(nIn, nA, bleedC);                                             // treble bleed (drawn "?")
        n.C(nA, nB, 0.1e-6);
        n.C(nB, nM, 560e-12);
        n.C(nB, nP, 0.1e-6);
        n.C(nN, 0, 4.7e-6);
        n.isrc[(size_t) nN] = iQ1;                                        // Q1 current source into N
    }
    void buildDet(Ch& s)
    {
        auto& n = s.det;
        n = DetNet {};
        n.nodes = 5; n.T = T;
        n.R(dO, 0, 10e3);                                                 // LEVEL pot (whole track)
        n.R(dC, 0, 100e3);
        n.R(dC, dG, 470e3);
        n.C(dOut, dO, 4.7e-6);
        n.C(dC, 0, 4.7e-6);
    }
    void setPots(Ch& s)
    {
        const double rb = juce::jmax(1.0, 500e3 * audioTaper(s.gainPos));
        s.cell.res[(size_t) s.rBot].r = rb;
        s.cell.res[(size_t) s.rTop].r = juce::jmax(1.0, 500e3 - rb);
        s.cell.res[(size_t) s.rN].r = squishRN(squishTravel * s.squishPos);
    }
    // SQUISH: wiper to N; lug 1 -> 0 V, lug 3 -> 22K -> 0 V; in parallel with the 20K. Knob CW = wiper toward lug 1
    // (V_N falls, Q2 conducts more = more squish; fully CCW the original is a clean boost - ovnilab review), so the A
    // taper is measured from lug 1 with the rotation reversed. The plugin knob covers the part of the rotation that
    // matters: past Q2's pinch-off (V_N < |Vp|) the original only adds a big fixed loss (Q2 on at rest, ~-40 dB at noon
    // GAIN) with LESS gain-reduction range (the gate clamp limits it), so the knob stops just short of it.
    static double squishRN(double rotation)
    {
        const double r1 = juce::jmax(1.0, 10e3 * audioTaper(1.0 - rotation));
        const double rsq = r1 * (32e3 - r1) / 32e3;
        return 20e3 * rsq / (20e3 + rsq);
    }
    void initState(Ch& s)
    {
        const double vn = iQ1 * s.cell.res[(size_t) s.rN].r;
        auto& n = s.cell;
        n.x.fill(0.0);
        n.x[nB] = n.x[nN] = vn;
        for (auto& c : n.caps) { c.v = n.x[(size_t) c.a] - n.x[(size_t) c.b]; c.i = 0.0; }
        s.det.x.fill(0.0);
        for (auto& c : s.det.caps) { c.v = 0.0; c.i = 0.0; }
        s.ampX = s.ampY = 0.0; s.dI = 0.0;
        n.beSteps = s.det.beSteps = 0;
    }

    // Q2's overdrive (Vgs - Vp) with a soft (softplus) pinch-off: the sub-threshold tail that rounds the knee a little.
    // sl = d overdrive / d vgs.
    static double overdrive(double vgs, double& sl)
    {
        const double u = (vgs - q2Vp) / pinchSoft;
        if (u > 30.0) { sl = 1.0; return vgs - q2Vp; }
        const double e = std::exp(u);
        sl = e / (1.0 + e);
        return pinchSoft * std::log1p(e);
    }
    static double diode(double v, double is, double nvt, double& g)
    {
        const double a = v / nvt;
        if (a > 40.0) { const double e = std::exp(40.0); g = is * e / nvt; return is * (e - 1.0) + g * (v - 40.0 * nvt); }
        const double e = std::exp(a);
        g = is * e / nvt;
        return is * (e - 1.0);
    }

    double tick(Ch& s, double vin)
    {
        // ---- input / gain cell: Newton on (vB, vN, vG) with Q2's channel and gate junctions
        auto& n = s.cell;
        std::array<double, 9> v0 {};
        const auto& mt = n.linear({ 0.0, vin, s.det.x[dC] }, v0);
        const int idx[3] = { nB, nN, nG };
        double Z[3][3];
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) Z[r][c] = CellNet::z(mt, idx[r], idx[c]);
        double vB = n.x[nB], vN = n.x[nN], vG = n.x[nG];
        const double k = q2Idss / (q2Vp * q2Vp);
        double inj[3] = { 0, 0, 0 };
        for (int it = 0; it < 12; ++it)
        {
            double sa = 0, sb = 0;
            const double a = overdrive(vG - vN, sa), b = overdrive(vG - vB, sb);
            const double ich = k * (a * a - b * b);                       // B -> N
            double gs = 0, gd = 0;
            const double igs = diode(vG - vN, gateIs, gateNVt, gs), igd = diode(vG - vB, gateIs, gateNVt, gd);
            inj[0] = -ich + igd; inj[1] = ich + igs; inj[2] = -igs - igd;
            // d inj / d (vB, vN, vG)
            const double ga = 2 * k * a * sa, gb = 2 * k * b * sb;      // d ich / d vGS, d ich / d vGD
            const double D[3][3] = { { -gb - gd,  ga,       -(ga - gb) + gd },
                                     {  gb,      -ga - gs,   (ga - gb) + gs },
                                     {  gd,       gs,       -gs - gd } };
            const double xv[3] = { vB, vN, vG };
            double F[3], J[3][3];
            for (int r = 0; r < 3; ++r)
            {
                F[r] = xv[r] - v0[(size_t) idx[r]] - (Z[r][0] * inj[0] + Z[r][1] * inj[1] + Z[r][2] * inj[2]);
                for (int c = 0; c < 3; ++c)
                    J[r][c] = (r == c ? 1.0 : 0.0) - (Z[r][0] * D[0][c] + Z[r][1] * D[1][c] + Z[r][2] * D[2][c]);
            }
            double dx[3];
            solve3(J, F, dx);
            double step = 1.0;
            const double maxd = juce::jmax(std::abs(dx[0]), std::abs(dx[1]), std::abs(dx[2]));
            if (maxd > 0.2) step = 0.2 / maxd;                            // damp big steps (diode exponentials)
            vB -= step * dx[0]; vN -= step * dx[1]; vG -= step * dx[2];
            if (maxd < 1e-10) break;
        }
        {   // final currents at the solution, then every node voltage
            double sa = 0, sb = 0;
            const double a = overdrive(vG - vN, sa), b = overdrive(vG - vB, sb);
            const double ich = k * (a * a - b * b);
            double gs = 0, gd = 0;
            const double igs = diode(vG - vN, gateIs, gateNVt, gs), igd = diode(vG - vB, gateIs, gateNVt, gd);
            inj[0] = -ich + igd; inj[1] = ich + igs; inj[2] = -igs - igd;
        }
        for (int q = 0; q < n.nodes; ++q)
        {
            double v = v0[(size_t) q];
            if (q >= 3) for (int c = 0; c < 3; ++c) v += CellNet::z(mt, q, idx[c]) * inj[c];
            n.x[(size_t) q] = v;
        }
        n.commit();

        // ---- U1A x23 (DC unity), rail
        const double vp = n.x[nP];
        const double y = amp.b0 * vp + amp.b1 * s.ampX - amp.a1 * s.ampY;
        s.ampX = vp; s.ampY = y;
        const double out = softRail(y, rail);

        // ---- detector: 2K1 + D1 from O to Vc (scalar Newton on the diode current)
        auto& d = s.det;
        std::array<double, 5> w0 {};
        const auto& md = d.linear({ 0.0, out, n.x[nG] }, w0);
        const double zo = DetNet::z(md, dO, dC) - DetNet::z(md, dO, dO);  // dvO per amp leaving O, entering C
        const double zc = DetNet::z(md, dC, dC) - DetNet::z(md, dC, dO);
        double lo = 0.0, hi = 0.02, i0 = juce::jlimit(lo, hi, s.dI);
        for (int it = 0; it < 30; ++it)
        {
            const double vd = (w0[dO] - w0[dC]) + i0 * (zo - zc) - 2.1e3 * i0;
            double g = 0;
            const double f = i0 - diode(vd, dIs, dNVt, g);
            if (f > 0.0) hi = i0; else lo = i0;
            const double fp = 1.0 - g * (zo - zc - 2.1e3);
            double nxt = i0 - f / fp;
            if (nxt <= lo || nxt >= hi) nxt = 0.5 * (lo + hi);
            const bool done = std::abs(nxt - i0) < 1e-14;
            i0 = nxt;
            if (done) break;
        }
        s.dI = i0;
        d.x[0] = 0.0; d.x[dOut] = out; d.x[dG] = n.x[nG];
        d.x[dO] = w0[dO] + i0 * zo;
        d.x[dC] = w0[dC] + i0 * zc;
        d.commit();
        return d.x[dO];
    }

    static void solve3(const double J[3][3], const double F[3], double x[3])
    {
        const double det = J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1])
                         - J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0])
                         + J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);
        if (std::abs(det) < 1e-300) { x[0] = x[1] = x[2] = 0.0; return; }
        const double id = 1.0 / det;
        x[0] = id * (F[0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1]) - J[0][1] * (F[1] * J[2][2] - J[1][2] * F[2]) + J[0][2] * (F[1] * J[2][1] - J[1][1] * F[2]));
        x[1] = id * (J[0][0] * (F[1] * J[2][2] - J[1][2] * F[2]) - F[0] * (J[1][0] * J[2][2] - J[1][2] * J[2][0]) + J[0][2] * (J[1][0] * F[2] - F[1] * J[2][0]));
        x[2] = id * (J[0][0] * (J[1][1] * F[2] - F[1] * J[2][1]) - J[0][1] * (J[1][0] * F[2] - F[1] * J[2][0]) + F[0] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]));
    }
};
