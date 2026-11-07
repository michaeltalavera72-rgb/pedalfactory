#pragma once
#include "PluginProcessor.h"
#include "Faces/PedalFace.h"
#include "UI/StompboxControls.h"
#include "UI/LevelControl.h"
#include "UI/TransposerPanel.h"
#include "UI/PresetBar.h"

class BoardComponent;

// One slot: a slim header (grab to drag, X to remove) above the pedal.
// Pedals with a PedalFace get stompbox artwork, knobs and a footswitch;
// others get the plain generic strip with a Bypass toggle.
class PedalStrip : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    PedalStrip(PedalboardProcessor&, PedalBase&, BoardComponent&);
    ~PedalStrip() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    juce::String getTooltip() override;          // "based on ..." over the header name and the printed name
    PedalBase& pedal;
    bool dragging = false;
private:
    void timerCallback() override;              // repaints the LED when bypass changes (click or automation)
    juce::Rectangle<float> bodyBounds() const;
    BoardComponent& board;
    std::unique_ptr<PedalFace> face;
    StompKnobLookAndFeel knobLook;
    juce::TextButton remove { "X" };
    juce::ToggleButton bypass { "Bypass" };
    Footswitch footswitch, footswitch2;          // footswitch2 only for pedals with a second switch
    MiniToggle toggle;                           // only for pedals with PedalBase::toggleId (2 positions)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAtt;
    ThreeWayToggle toggle3;                      // ... or a 3-way toggle (togglePositions == 3)
    std::unique_ptr<juce::ParameterAttachment> toggle3Att;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> atts;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> secondAtt;
    bool headerDrag = false, lastPrimary = true, lastSecondary = false;
    int lastMeter = -1;
    int numSwitches() const { return pedal.secondarySwitchId().isNotEmpty() ? 2 : 1; }
    FaceLayout layout() const;
};

// The horizontal row of slots. Handles drag-and-drop reordering.
class BoardComponent : public juce::Component, private juce::Timer
{
public:
    static constexpr int slotW = 230, gap = 8, headerH = 26, boardH = 414;   // pedal body 226 x 372 (= FaceLayout::bodyAspect)
    explicit BoardComponent(PedalboardProcessor&);
    void rebuild();
    void layoutSlots(bool animate);
    void beginDrag(PedalStrip&);
    void dragTo(PedalStrip&);                       // follows the mouse (also used by auto-scroll)
    void setViewport(juce::Viewport* v) { viewport = v; }
    void endDrag(PedalStrip&);
    void removePedal(PedalBase&);
    // Slot geometry: a pedal takes PedalBase::widthUnits() slots (double-wide pedals span two slots + the gap).
    static int widthFor(int units) { return units * slotW + (units - 1) * gap; }
    int stripWidth(int i) const;
    int slotX(int i) const;                         // left edge of strip i in the current order
    PedalboardProcessor& proc;
private:
    void timerCallback() override;
    juce::OwnedArray<PedalStrip> strips;
    juce::ComponentAnimator animator;
    juce::Viewport* viewport = nullptr;
    PedalStrip* dragged = nullptr;
    int dragIndex = -1, grabOffset = 0;
};

class PedalboardEditor : public juce::AudioProcessorEditor, private juce::ChangeListener
{
public:
    explicit PedalboardEditor(PedalboardProcessor&);
    ~PedalboardEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { board.rebuild(); updateSize(); }
    void updateSize();
    PedalboardProcessor& proc;
    BoardComponent board;
    juce::Viewport viewport;
    juce::TextButton addButton { "+ Add pedal" };
    juce::TooltipWindow tooltips { this, 500 };   // shows every setTooltip()/getTooltip() in the editor
    LevelControl inputLevel, outputLevel;     // signal flow: INPUT on the left, OUTPUT on the right
    TransposerPanel transposerPanel;          // global section below the board
    PresetBar presetBar;                      // preset row above the level bar
    static constexpr int presetH = PresetBar::barH, topBarH = 44, minWidth = 680, globalH = TransposerPanel::panelH + 8;
    static constexpr int editorH = presetH + BoardComponent::boardH + topBarH + 14 + globalH;
};
