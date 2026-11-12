#pragma once
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
#include "Global/PresetManager.h"

class PedalboardProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster, private juce::Timer
{
public:
    PedalboardProcessor();
    const juce::String getName() const override { return "Virtual Pedalboard"; }
    void prepareToPlay(double sr, int bs) override;
    void releaseResources() override {}
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    // Delay / reverb pedals ring on after the input stops; tell the host so it doesn't cut the tail of a render.
    double getTailLengthSeconds() const override { return hasTimeBased.load() ? 6.0 : 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- Board (the horizontal row of slots) ----
    juce::StringArray getBoard() const;                 // pedal ids, left to right
    void setBoard(const juce::StringArray& ids);        // reorder / add / remove in one go
    PedalBase* findPedal(const juce::String& id) const;
    std::vector<std::unique_ptr<PedalBase>> pedals;     // every available pedal type

    juce::AudioProcessorValueTreeState apvts;
    // Board presets: pedals + order + their settings + the transposer (not the INPUT/OUTPUT levels).
    PresetManager presets;

    // ---- Board input / output level (parameters "board_in" / "board_out", in dB) ----
    // Peak meters for the UI: the audio thread raises them, the editor reads and decays them.
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    static float takePeak(std::atomic<float>& p) { return p.exchange(0.0f); }

    // ---- Global section (below the board) ----
    // Semitone Transposer: first in the chain, right after INPUT level. Parameters "gx_transpose" (int st),
    // "gx_mix" (%), "gx_level" (dB), "gx_on" (true = on), "gx_studio" (false = LIVE, true = STUDIO).
    int studioLatencySamples() const { return studioLatency.load(); }

private:
    void timerCallback() override;                        // message thread: keeps the reported latency current
    transposer::Transposer transposer;
    std::atomic<float> *gxTranspose = nullptr, *gxMix = nullptr, *gxLevel = nullptr, *gxOn = nullptr, *gxStudio = nullptr;
    std::atomic<int> studioLatency { 0 };
    int wantedLatency() const { return (gxOn->load() >= 0.5f && gxStudio->load() >= 0.5f) ? studioLatency.load() : 0; }

    std::atomic<float> *inGainDb = nullptr, *outGainDb = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.0f }, outGain { 1.0f };
    static void raisePeak(std::atomic<float>& p, float v)
    {
        float cur = p.load();
        while (v > cur && ! p.compare_exchange_weak(cur, v)) {}
    }
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(
        std::vector<std::unique_ptr<PedalBase>>&);
    static constexpr int maxSlots = 16;
    // The board has two views. "board" (the ids) is read and written under boardLock, never by the audio thread.
    // "chain" (the pedal pointers) is swapped under chainLock, held only for that copy; the audio thread try-locks
    // it to refresh its own copy (audioChain) and, if it can't, simply runs the previous buffer's chain.
    mutable juce::CriticalSection boardLock;
    juce::StringArray board { "bm" };             // default board
    juce::SpinLock chainLock;
    PedalBase* chain[maxSlots] {};                      // written by setBoard
    int chainSize = 0;
    PedalBase* audioChain[maxSlots] {};                 // audio thread only
    int audioChainSize = 0;
    juce::uint64 blockCounter = 1;                      // audio thread only: buffers processed (see PedalBase::lastBlockRun)
    std::atomic<bool> hasTimeBased { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalboardProcessor)
};
