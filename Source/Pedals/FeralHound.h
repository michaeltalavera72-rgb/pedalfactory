#pragma once
#include "PedalBase.h"

// Feral Hound: a model of the VFE Alpha Dog v2 distortion (VFE's take on the ProCo RAT), shown under our
// own name and artwork. There is no published schematic: the circuit below was traced from the stripboard
// layout (schematics/VFE/Alpha Dog/VFE Alpha.png) and cross-checked with the madbean PCB parts list and
// voltage table (VFE_AlphaDog.pdf). Signal path, in volts around the 4.5 V reference (single 9 V supply):
//
//   INPUT: 1k -> 220n -> LM308 "+" input, 1n to ground, 2M2/2M2 bias (1M AC)
//   GAIN STAGE (LM308N, non-inverting, 33p compensation -> GBW ~0.9 MHz, slew ~0.27 V/us):
//       feedback  GAIN (A1M, rheostat) || 10p
//                 SOFT (W1M): lug 1 = 1N34A (anode at "-") -> wiper = output  -> germanium, one polarity only
//                             lug 3 = BS170 gate+drain at "-", source to lug 3 (|| 100p) -> MOSFET (~2 V) one
//                             way, its body diode (~0.7 V) the other. Noon = 500k in series with both = ~clean.
//       ground legs 470R + 220n (~1.5 kHz) and FAT (C10k rheostat) + 470R + 1u (~340 Hz at max) -> Fat adds
//                   gain and low end.
//       The op-amp is modelled as a slew-limited integrator (GBW + slew rate = the RAT's fizzy, compressed
//       character at high gain) clipped at ~+/-3.6 V. "-" node solved per sample (backward Euler + Newton).
//   HARD CLIPPING: 4u7 -> 1k -> node X. HARD (W20k): lug 1 -> two stacked 1N4148 pairs + 1n to ground,
//       lug 3 -> one 1N4148 pair to ground, wiper = X. CCW = stacked (looser), CW = single pair (classic RAT),
//       noon = 10k in series with both = almost no clipping.
//   FILTER: X -> FILTER (C100k rheostat, CW = brightest) -> 1k -> 4n7 to ground (~2.5 kHz cut-off at noon).
//   The VFE designer's own notes on the v2 mods (input network, 470R+220n and Fat 470R..10.5k+1u legs,
//   C100k+1k+4n7 filter, W1M/W20k detent pots, M = BS170 body diode + Vgs(on) with 100p, G = one 1N34A,
//   V = classic silicon pair, T = two stacked pairs + small cap) match this trace.
//   OUTPUT: 220n / 1M -> 2N5458 source follower (10k) -> 1u -> LEVEL (A100k) -> 1k -> out.
class FeralHound : public PedalBase
{
public:
    juce::String getId() const override { return "ad"; }
    juce::String getName() const override { return "Feral Hound"; }
    PedalCategory getCategory() const override { return PedalCategory::Distortion; }
    juce::String getBasedOn() const override { return "VFE Alpha Dog v2 distortion (LM308 RAT-style circuit with Fat, Soft and Hard clipping controls)"; }

    // Tooltip text for each control (the editor adds the control's name on top).
    juce::String describe(const juce::String& id) const override
    {
        if (id == "ad_bypass") return "Footswitch: turns Feral Hound on or off.";
        if (id == "ad_gain") return "Amount of distortion, from light overdrive to thick, compressed fuzz.";
        if (id == "ad_filter") return "Tone filter: down = dark and smooth, up = brighter and more aggressive.";
        if (id == "ad_level") return "Output level of the pedal.";
        if (id == "ad_fat") return "Adds low end and extra gain (a bigger bass shelf in the gain stage).";
        if (id == "ad_soft") return "Extra clipping in the op-amp feedback. Noon (detent) = off. Left = germanium diode (warm, soft); right = MOSFET (more open, amp-like). Double-click: noon.";
        if (id == "ad_hard") return "Clipping diodes to ground. Noon (detent) = off. Left = stacked silicon (louder, looser); right = classic single pair (tight, compressed RAT sound). Double-click: noon.";
        return {};
    }

    void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& l) override
    {
        using P = juce::AudioParameterFloat;
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"ad_bypass",1}, "Feral Hound Bypass", false));
        l.add(std::make_unique<P>(juce::ParameterID{"ad_gain",1},   "Gain",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"ad_filter",1}, "Filter", 0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"ad_level",1},  "Level",  0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"ad_fat",1},    "Fat",    0.0f, 1.0f, 0.3f));
        l.add(std::make_unique<P>(juce::ParameterID{"ad_soft",1},   "Soft",   0.0f, 1.0f, 0.5f));
        l.add(std::make_unique<P>(juce::ParameterID{"ad_hard",1},   "Hard",   0.0f, 1.0f, 1.0f));   // CW = classic RAT diodes
        // Top row (big knobs) then bottom row (small knobs), left to right on the face.
        knobIds = { "ad_gain", "ad_filter", "ad_level", "ad_fat", "ad_soft", "ad_hard" };
        detentKnobIds = { "ad_soft", "ad_hard" };      // centre detent on the real pots (noon = no clipping)
    }
    void bindParameters(juce::AudioProcessorValueTreeState& s) override
    {
        bypassParam = s.getRawParameterValue("ad_bypass");
        gain   = s.getRawParameterValue("ad_gain");
        filter = s.getRawParameterValue("ad_filter");
        level  = s.getRawParameterValue("ad_level");
        fat    = s.getRawParameterValue("ad_fat");
        soft   = s.getRawParameterValue("ad_soft");
        hard   = s.getRawParameterValue("ad_hard");
    }
    void prepare(const juce::dsp::ProcessSpec& spec) override
    {
        sr = spec.sampleRate;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 4x
        oversampler->initProcessing(spec.maximumBlockSize);
        h = 1.0 / (sr * 4.0);
        gDc = coeff(5.0, sr);
        {   // per-sample constants of the fixed parts (companion conductances at the 4x rate)
            constexpr double Ci = 220e-9, Cp = 1e-9, Cout = 4.7e-6, Cf = 4.7e-9, Cg = 220e-9, Ch1 = 1e-9;
            k.gIn = 1.0 / (1000.0 + h / Ci); k.cp = Cp / h; k.inDen = 1.0 / (k.gIn + 1.0e-6 + k.cp); k.inUpd = h / Ci * k.gIn;
            k.GA = 1.0 / (470.0 + h / 220e-9); k.aUpd = h / 220e-9 * k.GA; k.hC1u = h / 1e-6;
            k.c10 = 10e-12 / h; k.c100 = 100e-12 / h;
            k.G2 = 1.0 / (1000.0 + h / Cout); k.outUpd = h / Cout * k.G2;
            k.cf = Cf / h; k.G3 = 1.0 / (1.0e6 + h / Cg); k.gUpd = h / Cg; k.c1 = Ch1 / h;
        }
        for (auto* sm : { &rgSm, &rfatSm, &rsaSm, &rsbSm, &r1Sm, &r3Sm, &rfilSm }) sm->reset(sr * 4.0, 0.03);
        outGain.reset(sr, 0.03);
        if (gain != nullptr) { targets(); snapSmoothers(); }
        reset();
    }
    void reset() override
    {
        for (auto& c : ch) c = {};
        if (oversampler) oversampler->reset();
    }

    void process(juce::AudioBuffer<float>& buf) override
    {
        targets();
        juce::dsp::AudioBlock<float> block(buf);
        auto up = oversampler->processSamplesUp(block);
        for (size_t c = 0; c < up.getNumChannels() && c < ch.size(); ++c)
        {
            auto& s = ch[c];
            auto* x = up.getChannelPointer(c);
            // pot-dependent conductances: once per buffer, or per sample only while a knob is moving
            bool moving = false;
            for (auto* sm : { &rgSm, &rfatSm, &rsaSm, &rsbSm, &r1Sm, &r3Sm, &rfilSm }) moving = moving || sm->isSmoothing();
            Pots p { rgSm.getCurrentValue(), rfatSm.getCurrentValue(), rsaSm.getCurrentValue(), rsbSm.getCurrentValue(),
                     r1Sm.getCurrentValue(), r3Sm.getCurrentValue(), rfilSm.getCurrentValue() };
            derive(p);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                if (moving)
                {
                    p = { rgSm.getNextValue(), rfatSm.getNextValue(), rsaSm.getNextValue(), rsbSm.getNextValue(),
                          r1Sm.getNextValue(), r3Sm.getNextValue(), rfilSm.getNextValue() };
                    derive(p);
                }
                x[i] = (float) tick(s, (double) x[i] * 0.5, p);          // full scale ~ 0.5 V guitar
            }
        }
        oversampler->processSamplesDown(block);

        for (int c = 0; c < buf.getNumChannels() && c < (int) ch.size(); ++c)
        {
            auto& s = ch[(size_t) c];
            auto* x = buf.getWritePointer(c);
            auto g = outGain;                                    // same ramp on every channel
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                float o = x[i] * g.getNextValue();
                o -= lowpass(s.dc, o, gDc);
                x[i] = o;
            }
        }
        outGain.skip(buf.getNumSamples());
    }

    // ---- pot positions -> resistances (exposed for tests)
    static double audioTaper(double x) { return (std::pow(10.0, 2.0 * x) - 1.0) / 99.0; }      // ~9 % at noon
    // log taper with a chosen value at noon (mid < 0.5)
    static double logTaper(double x, double mid) { const double b = (1.0 / mid - 1.0) * (1.0 / mid - 1.0); return (std::pow(b, x) - 1.0) / (b - 1.0); }
    // W taper with centre detent: each half is log-shaped towards its end, both sides = Rtotal/2 at noon.
    // Returns {lug1..wiper, wiper..lug3}.
    static std::pair<double, double> wTaper(double pos, double total)
    {
        const double u = 2.0 * pos - 1.0, half = 0.5 * total;
        const double active = half * audioTaper(1.0 - std::abs(u)) + 40.0;   // ~40R end resistance
        return u < 0.0 ? std::make_pair(active, total - active + 40.0) : std::make_pair(total - active + 40.0, active);
    }

private:
    struct Ch
    {
        float dc = 0;
        double vcIn = 0, vp = 0;               // input coupling cap, + input
        double vn = 0, v1 = 0, v3 = 0, u = 0;  // - input, D5 cathode, BS170 source, op-amp integrator
        double vcA = 0, vcB = 0;               // ground-leg caps (220n, 1u)
        double vcOut = 0, x = 0, h1 = 0, h3 = 0, f = 0, vcG = 0;
    };
    struct Pots
    {
        double rg, rfat, rsa, rsb, r1, r3, rfil;
        // derived (see derive): conductances and the filter node's reduction
        double gg = 0, gsa = 0, gsb = 0, GB = 0, Gl = 0, bUpd = 0, gf = 0, invD = 0, gXF = 0, g1 = 0, g3 = 0;
    };
    struct Consts { double gIn = 0, cp = 0, inDen = 0, inUpd = 0, GA = 0, aUpd = 0, hC1u = 0, c10 = 0, c100 = 0,
                           G2 = 0, outUpd = 0, cf = 0, G3 = 0, gUpd = 0, c1 = 0; } k;
    void derive(Pots& p) const
    {
        p.gg = 1.0 / p.rg; p.gsa = 1.0 / p.rsa; p.gsb = 1.0 / p.rsb;
        p.GB = 1.0 / (p.rfat + k.hC1u); p.Gl = k.GA + p.GB; p.bUpd = k.hC1u * p.GB;
        p.gf = 1.0 / p.rfil; p.invD = 1.0 / (p.gf + k.cf + k.G3); p.gXF = p.gf * (1.0 - p.gf * p.invD);
        p.g1 = 1.0 / p.r1; p.g3 = 1.0 / p.r3;
    }
    std::array<Ch, 2> ch;
    double sr = 48000.0, h = 1.0 / 192000.0;
    float gDc = 0;
    juce::SmoothedValue<double> rgSm, rfatSm, rsaSm, rsbSm, r1Sm, r3Sm, rfilSm;
    juce::SmoothedValue<float> outGain;
    std::atomic<float> *gain{}, *filter{}, *level{}, *fat{}, *soft{}, *hard{};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    static constexpr float levelScale = 2.9f;    // volts -> digital; calibrated: same loudness as the Valve Howler at noon (-18 dBFS sine in)

    // LM308 + parts
    static constexpr double Vt = 0.02585;
    static constexpr double GBW = 2.0 * 3.14159265358979 * 0.9e6, SR = 0.27e6, RAIL = 3.6;
    static constexpr double IsGe = 2.0e-7, nGe = 1.35;                 // 1N34A
    static constexpr double IsSi = 2.52e-9, nSi = 1.752;               // 1N4148
    static constexpr double Kmos = 0.1, Vth = 2.1, sMos = 0.06;        // BS170 channel (square law, smoothed)
    static constexpr double IsBody = 5.0e-11, nBody = 1.6;             // BS170 body diode

    void targets()
    {
        rgSm.setTargetValue(1.0e6 * audioTaper(gain->load()) + 100.0);
        rfatSm.setTargetValue(10.0e3 * audioTaper(1.0 - fat->load()) + 470.0);       // C taper: CW = 0 ohm
        // C taper, CW = brightest. Noon ~11.5 % (11.5k + 1k + ~1k source into 4n7 = ~2.5 kHz): VFE quotes a
        // 2.3-2.8 kHz treble cut-off at 12:00 for the v2 FILTER.
        rfilSm.setTargetValue(100.0e3 * logTaper(1.0 - filter->load(), 0.115) + 1000.0);
        auto [sa, sb] = wTaper(soft->load(), 1.0e6);
        rsaSm.setTargetValue(sa); rsbSm.setTargetValue(sb);
        auto [h1, h3] = wTaper(hard->load(), 20.0e3);
        r1Sm.setTargetValue(h1); r3Sm.setTargetValue(h3);
        outGain.setTargetValue(levelScale * (float) audioTaper(level->load()) * 0.95f);   // 2N5458 follower ~0.95
    }
    void snapSmoothers()
    {
        for (auto* sm : { &rgSm, &rfatSm, &rsaSm, &rsbSm, &r1Sm, &r3Sm, &rfilSm }) sm->setCurrentAndTargetValue(sm->getTargetValue());
        outGain.setCurrentAndTargetValue(outGain.getTargetValue());
    }

    static float coeff(double fc, double fs) { auto g = std::tan(juce::MathConstants<double>::pi * fc / fs); return (float) (g / (1.0 + g)); }
    static float lowpass(float& s, float in, float g) { float v = (in - s) * g; float y = v + s; s = y + v; return y; }
    static double ex(double a) { return std::exp(juce::jlimit(-50.0, 40.0, a)); }
    // smooth output-stage clip: linear up to ~RAIL, then bends over (k = 4)
    static double softRail(double v, double& d)
    {
        const double r = v / RAIL, r4 = r * r * r * r, den = std::sqrt(std::sqrt(1.0 + r4));
        d = 1.0 / (den * (1.0 + r4));
        return v / den;
    }

    // One 4x-rate sample through the whole circuit. vin in volts; returns the 2N5458 gate voltage.
    double tick(Ch& s, double vin, const Pots& p) const
    {
        // ---- input: 1k + 220n in series, 1n and 1M (bias) at the + input
        {
            const double vp = (k.gIn * (vin - s.vcIn) + k.cp * s.vp) * k.inDen;
            s.vcIn += k.inUpd * (vin - s.vcIn - vp);
            s.vp = vp;
        }

        // ---- gain stage: unknowns Vn, V1 (1N34A cathode), V3 (BS170 source)
        const double GA = k.GA, GB = p.GB;
        const double Gl = p.Gl, Jl = GA * s.vcA + GB * s.vcB;
        const double c10 = k.c10, c100 = k.c100;
        const double q10 = s.vn - softRailNoD(s.u), q100 = s.vn - s.v3;   // previous cap voltages
        const double gsa = p.gsa, gsb = p.gsb, gg = p.gg;
        double vn = s.vn, v1 = s.v1, v3 = s.v3, u = s.u, vo = 0;
        for (int it = 0; it < 12; ++it)
        {
            // op-amp: slew-limited integrator + output clip
            const double e = GBW * (s.vp - vn) / SR, th = std::tanh(e);
            double du_dvn = -h * GBW * (1.0 - th * th);
            u = s.u + h * SR * th;
            if (u > RAIL + 0.6)       { u = RAIL + 0.6;  du_dvn = 0.0; }
            else if (u < -RAIL - 0.6) { u = -RAIL - 0.6; du_dvn = 0.0; }
            double dr; vo = softRail(u, dr);
            const double dO = dr * du_dvn;

            // 1N34A, anode at Vn
            const double vd = vn - v1;
            const double eD = ex(vd / (nGe * Vt));
            const double iD = IsGe * (eD - 1.0), gD = IsGe / (nGe * Vt) * eD + 1e-12;
            // BS170 diode-connected (gate = drain = Vn), source V3: channel one way, body diode the other
            const double vm = vn - v3;
            const double z = (vm - Vth) / sMos;
            double sp, sg;
            if (z > 30.0) { sp = z; sg = 1.0; }
            else if (z < -30.0) { sp = sg = std::exp(z); }
            else { const double ez = std::exp(z); sp = std::log1p(ez); sg = ez / (1.0 + ez); }
            const double veff = sMos * sp;
            const double eB = ex(-vm / (nBody * Vt));
            const double iM = 0.5 * Kmos * veff * veff - IsBody * (eB - 1.0);
            const double gM = Kmos * veff * sg + IsBody / (nBody * Vt) * eB + 1e-12;

            const double F0 = Gl * vn - Jl + (vn - vo) * gg + c10 * ((vn - vo) - q10) + iD + iM + c100 * (vm - q100);
            const double F1 = iD - (v1 - vo) * gsa;
            const double F2 = iM + c100 * (vm - q100) - (v3 - vo) * gsb;

            const double a00 = Gl + (1.0 - dO) * (gg + c10) + gD + gM + c100, a01 = -gD, a02 = -gM - c100;
            const double a10 = gD + dO * gsa, a11 = -gD - gsa;
            const double a20 = gM + c100 + dO * gsb, a22 = -gM - c100 - gsb;
            // solve [a00 a01 a02; a10 a11 0; a20 0 a22] d = F  (eliminate V1, V3)
            const double k1 = a01 / a11, k2 = a02 / a22;
            const double d0 = (F0 - k1 * F1 - k2 * F2) / (a00 - k1 * a10 - k2 * a20);
            const double d1 = (F1 - a10 * d0) / a11, d2 = (F2 - a20 * d0) / a22;
            vn -= juce::jlimit(-0.5, 0.5, d0);
            v1 -= juce::jlimit(-0.5, 0.5, d1);
            v3 -= juce::jlimit(-0.5, 0.5, d2);
            if (std::abs(d0) + std::abs(d1) + std::abs(d2) < 1e-5) break;
        }
        {   // final op-amp state for the converged Vn
            u = juce::jlimit(-RAIL - 0.6, RAIL + 0.6, s.u + h * SR * std::tanh(GBW * (s.vp - vn) / SR));
            double dr; vo = softRail(u, dr);
        }
        s.vcA += k.aUpd * (vn - s.vcA);
        s.vcB += p.bUpd * (vn - s.vcB);
        s.vn = vn; s.v1 = v1; s.v3 = v3; s.u = u;

        // ---- hard clipping + filter: unknowns X, H1 (stacked pairs + 1n), H3 (single pair)
        const double G2 = k.G2, J2 = G2 * (vo - s.vcOut);
        const double cf = k.cf, G3 = k.G3, gf = p.gf;
        const double Nf = cf * s.f + G3 * s.vcG;                            // D = gf + cf + G3 (p.invD = 1 / D)
        const double gXF = p.gXF, jXF = gf * Nf * p.invD;                   // i(X->F) = gXF X - jXF
        const double g1 = p.g1, g3 = p.g3, c1 = k.c1;
        double x = s.x, h1 = s.h1, h3 = s.h3;
        for (int it = 0; it < 12; ++it)
        {
            const double a1 = juce::jlimit(-40.0, 40.0, h1 / (2.0 * nSi * Vt)), a3 = juce::jlimit(-40.0, 40.0, h3 / (nSi * Vt));
            const double e1 = std::exp(a1), e1i = 1.0 / e1, e3 = std::exp(a3), e3i = 1.0 / e3;
            const double i1 = IsSi * (e1 - e1i), gi1 = 0.5 * IsSi / (nSi * Vt) * (e1 + e1i);   // 2 Is sinh, d/dv
            const double i3 = IsSi * (e3 - e3i), gi3 = IsSi / (nSi * Vt) * (e3 + e3i);
            const double F0 = -(J2 - G2 * x) + gXF * x - jXF + (x - h1) * g1 + (x - h3) * g3;
            const double F1 = (h1 - x) * g1 + c1 * (h1 - s.h1) + i1;
            const double F2 = (h3 - x) * g3 + i3;
            const double a00 = G2 + gXF + g1 + g3, a01 = -g1, a02 = -g3;
            const double a11 = g1 + c1 + gi1, a22 = g3 + gi3;
            const double k1 = a01 / a11, k2 = a02 / a22;
            const double d0 = (F0 - k1 * F1 - k2 * F2) / (a00 + k1 * g1 + k2 * g3);
            const double d1 = (F1 + g1 * d0) / a11, d2 = (F2 + g3 * d0) / a22;
            x -= juce::jlimit(-0.5, 0.5, d0);
            h1 -= juce::jlimit(-0.5, 0.5, d1);
            h3 -= juce::jlimit(-0.5, 0.5, d2);
            if (std::abs(d0) + std::abs(d1) + std::abs(d2) < 1e-5) break;
        }
        s.vcOut += k.outUpd * (vo - s.vcOut - x);
        const double fN = (gf * x + Nf) * p.invD;
        const double iG = G3 * (fN - s.vcG);
        s.vcG += k.gUpd * iG;
        s.f = fN; s.x = x; s.h1 = h1; s.h3 = h3;
        return iG * 1.0e6;                                                  // gate voltage (1M to ground)
    }
    static double softRailNoD(double v) { double d; return softRail(v, d); }
};
