#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

// Top-level groups in the "+ Add pedal" menu, in the order they appear there.
// To add a category: add it to the enum, give it a name in categoryName() and list it in allCategories.
// Categories with no pedals are left out of the menu.
enum class PedalCategory { Tools, Compressor, BoostEq, Overdrive, Fuzz, Distortion, Modulation };

inline juce::String categoryName(PedalCategory c)
{
    switch (c)
    {
        case PedalCategory::Tools:      return "Tools";
        case PedalCategory::Compressor: return "Compressor";
        case PedalCategory::BoostEq:    return "Boost / EQ";
        case PedalCategory::Overdrive:  return "Overdrive";
        case PedalCategory::Fuzz:       return "Fuzz";
        case PedalCategory::Distortion: return "Distortion";
        case PedalCategory::Modulation: return "Modulation";
    }
    return "Other";
}
inline constexpr PedalCategory allCategories[] = { PedalCategory::Tools, PedalCategory::Compressor, PedalCategory::BoostEq, PedalCategory::Overdrive, PedalCategory::Fuzz, PedalCategory::Distortion,
                                                    PedalCategory::Modulation };

// Every pedal implements this interface. The board owns pedals in a vector
// and runs them in order (guitar -> pedal 1 -> pedal 2 -> ... -> output).
class PedalBase
{
public:
    virtual ~PedalBase() = default;
    virtual juce::String getId() const = 0;          // stable id, used in parameter ids
    virtual juce::String getName() const = 0;        // display name
    virtual PedalCategory getCategory() const = 0;   // which "+ Add pedal" submenu it is listed under
    // Which real pedal/circuit this models, shown as a tooltip on the pedal's name.
    // Real product names only identify the circuit; the pedal is shown under its own name and artwork.
    virtual juce::String getBasedOn() const { return {}; }
    // Tooltip description of one control (knob, toggle or footswitch), by parameter id. The editor shows the
    // control's name above it, so the printed labels (often tiny on the artwork) can always be read.
    virtual juce::String describe(const juce::String& paramId) const { juce::ignoreUnused(paramId); return {}; }
    virtual void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout&) = 0;
    virtual void bindParameters(juce::AudioProcessorValueTreeState&) = 0;
    virtual void prepare(const juce::dsp::ProcessSpec&) = 0;
    virtual void process(juce::AudioBuffer<float>&) = 0;
    virtual void reset() {}

    // Board slots the pedal takes (1 = standard 226 x 372 px body; 2 = double-wide 464 x 372, e.g. Coral Tide).
    virtual int widthUnits() const { return 1; }

    // Main footswitch ("<id>_bypass"). The board only calls process() while isActive() is true.
    virtual bool isActive() const { return primaryOn() || secondaryOn(); }
    bool primaryOn() const { return bypassParam == nullptr || bypassParam->load() < 0.5f; }
    std::atomic<float>* bypassParam = nullptr;

    // Optional second footswitch (e.g. a boost). Empty id = the pedal has only one footswitch.
    // The parameter is a bool where true = ON. Pedals with one must check both states in process().
    virtual juce::String secondarySwitchId() const { return {}; }
    // Name of the main footswitch in its tooltip on two-switch pedals (default "Drive"; single-switch pedals: "Bypass").
    virtual juce::String primarySwitchLabel() const { return {}; }
    virtual juce::String secondarySwitchLabel() const { return {}; }
    bool secondaryOn() const { return secondaryParam != nullptr && secondaryParam->load() >= 0.5f; }
    std::atomic<float>* secondaryParam = nullptr;

    // Optional bicolour level LED (e.g. Twin Vise's input meter): -1 = none, 0 = off, 1 = green, 2 = red.
    // Read by the editor ~20 times a second; implement with an atomic.
    virtual int ledMeter() const { return -1; }

    juce::StringArray knobIds;   // parameter ids the editor should show as knobs
    juce::StringArray detentKnobIds;   // knobs with a centre detent: snap to noon, double-click = noon
    // Optional mini toggle switch on the face (e.g. a voicing switch). Bool parameter, true = up/ON.
    // Not a footswitch: it never bypasses the pedal.
    juce::String toggleId, toggleLabel;     // toggleLabel = the toggle's name in its tooltip (e.g. "HARD / SOFT")
    int togglePositions = 2;                // 2 = bool parameter (true = up); 3 = 3-way: choice parameter 0 = up, 1 = centre, 2 = down

    // Optional "effects loop" return stage (e.g. a keyed noise gate). When hasReturnStage() is true the board
    // calls process() at the pedal's slot, then processReturn() on the same block after the later pedals:
    // just before the first time-based pedal, or at the end of the chain.
    virtual bool hasReturnStage() const { return false; }
    virtual void processReturn(juce::AudioBuffer<float>&) {}
    // Delay/reverb-type pedals return true so a keyed gate's return lands before them (tails ring out).
    virtual bool isTimeBased() const { return false; }
};
