// Pedal benchmark / regression harness (headless, no plugin host needed).
//
// Runs every pedal on 20 s of a plucked-string test signal and prints, per pedal: CPU (% of one core, real time),
// output peak / RMS / DC, the slowest buffer, and a NaN/Inf check; then the same with the input 24 dB hot and with
// every knob at minimum and at maximum; then the Transposer in each state.
//   pedal_bench [sampleRate] [bufferSize] [pedal name]      e.g.  pedal_bench 48000 128 "Slow Burn"
//   DUMP=<dir> pedal_bench ...   also writes each pedal's output as raw float32 (<id>.raw, <id>_hot.raw, <id>_min.raw,
//                                <id>_max.raw) - build it from two versions of Source/ and compare the files to see
//                                exactly what a change did to the sound.
// Build: compile this file with the JUCE modules juce_core, juce_events, juce_data_structures, juce_graphics,
// juce_gui_basics, juce_gui_extra, juce_audio_basics, juce_audio_formats, juce_audio_processors,
// juce_audio_processors_headless and juce_dsp (JUCE 8.0.15), include paths: JUCE/modules, Source/, and the folder
// holding signalsmith-stretch/ and signalsmith-linear/. In CMake: juce_add_console_app + target_sources + the
// same include paths; link juce::juce_audio_processors juce::juce_dsp.
// First used for the 2026-11-11 code review (claude/code-review-2026-11-11.md in the project).
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Pedals/DriveBase.h"
#include "Pedals/RamhornFuzz.h"
#include "Pedals/Buzzsaw.h"
#include "Pedals/RailBender.h"
#include "Pedals/ValveHowler.h"
#include "Pedals/FeralHound.h"
#include "Pedals/SlowBurn.h"
#include "Pedals/Gatekeeper.h"
#include "Pedals/Thunderhead.h"
#include "Pedals/GoldenHaze.h"
#include "Pedals/NightCourier.h"
#include "Pedals/CoralTide.h"
#include "Pedals/Frostbite.h"
#include "Pedals/TwinVise.h"
#include "Pedals/SoftLanding.h"
#include "Global/Transposer.h"
#include <chrono>
#include <cstdio>

struct Host : juce::AudioProcessor
{
    std::vector<std::unique_ptr<PedalBase>> pedals;
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout mk(std::vector<std::unique_ptr<PedalBase>>& p)
    {
        p.push_back(std::make_unique<RamhornFuzz>()); p.push_back(std::make_unique<Buzzsaw>());
        p.push_back(std::make_unique<RailBender>()); p.push_back(std::make_unique<ValveHowler>());
        p.push_back(std::make_unique<FeralHound>()); p.push_back(std::make_unique<SlowBurn>());
        p.push_back(std::make_unique<Gatekeeper>()); p.push_back(std::make_unique<Thunderhead>());
        p.push_back(std::make_unique<GoldenHaze>()); p.push_back(std::make_unique<NightCourier>());
        p.push_back(std::make_unique<CoralTide>()); p.push_back(std::make_unique<Frostbite>());
        p.push_back(std::make_unique<TwinVise>()); p.push_back(std::make_unique<SoftLanding>());
        p.push_back(std::make_unique<Overdrive>());
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        for (auto& x : p) x->addParameters(l);
        return l;
    }
    Host() : apvts(*this, nullptr, "S", mk(pedals)) { for (auto& p : pedals) p->bindParameters(apvts); }
    const juce::String getName() const override { return "h"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void set(const juce::String& id, float v) { auto* p = apvts.getParameter(id); p->setValueNotifyingHost(p->convertTo0to1(v)); }
};

// guitar-like test signal: plucked strings (Karplus-Strong), peaks ~ -12 dBFS, with gaps
static std::vector<float> guitar(double sr, double seconds)
{
    std::vector<float> out((size_t) (sr * seconds), 0.0f);
    juce::Random r(42);
    const double notes[] = { 82.4, 110.0, 146.8, 196.0, 246.9, 329.6, 440.0, 123.5 };
    size_t pos = 0; int k = 0;
    while (pos < out.size())
    {
        const int chord = (k % 3 == 2) ? 3 : 1;
        const size_t len = (size_t) (sr * 0.45);
        for (int c = 0; c < chord; ++c)
        {
            const double f = notes[(k + c * 2) % 8];
            const int N = (int) (sr / f);
            std::vector<float> d((size_t) N);
            for (auto& v : d) v = r.nextFloat() * 2.0f - 1.0f;
            float lp = 0; int idx = 0;
            for (size_t i = 0; i < len && pos + i < out.size(); ++i)
            {
                const float y = d[(size_t) idx];
                lp = 0.5f * (y + d[(size_t) ((idx + 1) % N)]) * 0.996f;
                d[(size_t) idx] = lp; idx = (idx + 1) % N;
                out[pos + i] += y * 0.25f / (float) chord * 1.6f;
            }
        }
        pos += (size_t) (sr * (k % 5 == 4 ? 0.9 : 0.5)); ++k;
    }
    return out;
}

struct Stats { double cpu = 0, peak = 0, rms = 0, dc = 0; bool bad = false; double worstBlockUs = 0; };
template <class F> static Stats run(const std::vector<float>& in, std::vector<float>& out, int bs, double sr, F&& f)
{
    Stats st; out = in;
    juce::ScopedNoDenormals nd;
    double total = 0;
    for (size_t p = 0; p + (size_t) bs <= out.size(); p += (size_t) bs)
    {
        float* chans[1] = { out.data() + p };
        juce::AudioBuffer<float> b(chans, 1, bs);
        auto t0 = std::chrono::high_resolution_clock::now();
        f(b);
        auto t1 = std::chrono::high_resolution_clock::now();
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        total += us; st.worstBlockUs = std::max(st.worstBlockUs, us);
    }
    double s2 = 0, s1 = 0;
    for (float v : out) { if (! std::isfinite(v)) st.bad = true; st.peak = std::max(st.peak, (double) std::abs(v)); s2 += (double) v * v; s1 += v; }
    st.rms = std::sqrt(s2 / (double) out.size()); st.dc = s1 / (double) out.size();
    st.cpu = total * 1e-6 / ((double) out.size() / sr) * 100.0;
    return st;
}
static double db(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    Host h;
    const double sr = argc > 1 ? atof(argv[1]) : 48000.0; const int bs = argc > 2 ? atoi(argv[2]) : 128;
    const double secs = 20.0;
    auto in = guitar(sr, secs);
    std::vector<float> out;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 1 };
    printf("sr %.0f block %d, %g s of plucked-guitar test signal (CPU = %% of one core, real time)\n", sr, bs, secs);
    printf("%-16s %7s %9s %9s %9s %10s %s\n", "pedal", "CPU%", "peak dB", "rms dB", "DC", "worst us", "");
    double sum = 0;
    for (auto& p : h.pedals)
    {
        if (argc > 3 && p->getName() != juce::String(argv[3])) continue;
        p->prepare(spec); p->reset();
        // settle / warm caches
        auto st = run(in, out, bs, sr, [&](juce::AudioBuffer<float>& b) { p->process(b); if (p->hasReturnStage()) p->processReturn(b); });
        st = run(in, out, bs, sr, [&](juce::AudioBuffer<float>& b) { p->process(b); if (p->hasReturnStage()) p->processReturn(b); });
        sum += st.cpu;
        if (auto* dir = getenv("DUMP")) { juce::File(juce::String(dir) + "/" + p->getId() + ".raw").replaceWithData(out.data(), out.size() * sizeof(float)); }
        printf("%-16s %7.2f %9.1f %9.1f %9.5f %10.0f %s\n", p->getName().toRawUTF8(), st.cpu, db(st.peak), db(st.rms), st.dc, st.worstBlockUs, st.bad ? "NaN/Inf!" : "");
    }
    printf("sum of all pedals: %.2f %%\n", sum);
    if (argc > 3) return 0;
    // hot input (+24 dB) and silence robustness
    printf("\n-- robustness: input x16 (+24 dB), then knobs at extremes --\n");
    auto hot = in; for (auto& v : hot) v *= 16.0f;
    for (auto& p : h.pedals)
    {
        p->prepare(spec); p->reset();
        auto st = run(hot, out, bs, sr, [&](juce::AudioBuffer<float>& b) { p->process(b); if (p->hasReturnStage()) p->processReturn(b); });
        if (auto* dir = getenv("DUMP")) juce::File(juce::String(dir) + "/" + p->getId() + "_hot.raw").replaceWithData(out.data(), out.size() * sizeof(float));
        printf("%-16s hot: cpu %6.2f peak %6.1f dB %s", p->getName().toRawUTF8(), st.cpu, db(st.peak), st.bad ? "NaN/Inf!" : "ok");
        for (int e = 0; e < 2; ++e)
        {
            for (auto& id : p->knobIds) h.set(id, e == 0 ? 0.0f : 1.0f);
            p->prepare(spec); p->reset();
            auto s2 = run(in, out, bs, sr, [&](juce::AudioBuffer<float>& b) { p->process(b); if (p->hasReturnStage()) p->processReturn(b); });
            if (auto* dir = getenv("DUMP")) juce::File(juce::String(dir) + "/" + p->getId() + (e == 0 ? "_min.raw" : "_max.raw")).replaceWithData(out.data(), out.size() * sizeof(float));
            printf(" | knobs %s: cpu %6.2f peak %6.1f dB %s", e == 0 ? "min" : "max", s2.cpu, db(s2.peak), s2.bad ? "NaN/Inf!" : "ok");
        }
        for (auto& id : p->knobIds) { auto* prm = h.apvts.getParameter(id); prm->setValueNotifyingHost(prm->getDefaultValue()); }
        printf("\n");
    }
    // transposer
    printf("\n-- transposer --\n");
    {
        transposer::Transposer t;
        struct Case { const char* name; bool on; bool studio; int st; } cases[] = {
            { "OFF (default)", false, false, 0 }, { "ON live 0 st", true, false, 0 }, { "ON live -2 st", true, false, -2 },
            { "ON studio -2 st", true, true, -2 }, { "OFF, studio selected", false, true, -2 } };
        for (auto& c : cases)
        {
            t.setOn(c.on); t.setMode(c.studio ? transposer::Transposer::Mode::studio : transposer::Transposer::Mode::live); t.setSemitones(c.st);
            t.prepare(sr, bs);
            auto st = run(in, out, bs, sr, [&](juce::AudioBuffer<float>& b) { t.process(b.getWritePointer(0), b.getNumSamples()); });
            printf("%-24s cpu %6.2f %%  worst block %6.0f us  (block = %.0f us)\n", c.name, st.cpu, st.worstBlockUs, bs / sr * 1e6);
        }
        printf("studio latency: %d samples\n", t.studioLatencySamples());
    }
    return 0;
}
