#include "PluginEditor.h"
#include <algorithm>
#include <limits>

// ---------------- PedalStrip ----------------
// Tooltip for a control: its name in capitals, then what it does (PedalBase::describe).
static juce::String tipFor(const PedalBase& pd, const juce::String& paramId, const juce::String& name)
{
    const auto d = pd.describe(paramId);
    return name.toUpperCase() + (d.isNotEmpty() ? "\n" + d : juce::String(" on/off"));
}

PedalStrip::PedalStrip(PedalboardProcessor& p, PedalBase& pd, BoardComponent& b)
    : pedal(pd), board(b), face(createFaceFor(pd.getId()))
{
    addAndMakeVisible(remove);
    remove.onClick = [this] { board.removePedal(pedal); };
    auto& bypassButton = face ? static_cast<juce::Button&>(footswitch) : static_cast<juce::Button&>(bypass);
    addAndMakeVisible(bypassButton);
    bypassButton.setTooltip(tipFor(pd, pd.getId() + "_bypass", pd.secondarySwitchId().isEmpty() ? juce::String("Bypass")
                                   : pd.primarySwitchLabel().isNotEmpty() ? pd.primarySwitchLabel() : juce::String("Drive")));
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, pd.getId() + "_bypass", bypassButton);
    for (auto& id : pd.knobIds)
    {
        const bool detent = pd.detentKnobIds.contains(id);
        auto* s = knobs.add(detent ? new DetentSlider() : new juce::Slider(juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox));
        if (detent) s->setDoubleClickReturnValue(true, 0.5);     // range is 0..1 (attachment), noon = 0.5
        auto* l = labels.add(new juce::Label({}, p.apvts.getParameter(id)->getName(40)));
        l->setJustificationType(juce::Justification::centred);
        l->setInterceptsMouseClicks(false, false);
        if (face)
        {
            auto* look = face->knobLookAndFeel();
            s->setLookAndFeel(look != nullptr ? look : &knobLook);
            s->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f, true);   // -135..+135 deg (= Blender filmstrip)
            l->setText(l->getText().toUpperCase(), juce::dontSendNotification);
            l->setFont(juce::FontOptions(11.5f, juce::Font::bold));
            l->setColour(juce::Label::textColourId, face->labelColour());
        }
        s->setTooltip(tipFor(pd, id, p.apvts.getParameter(id)->getName(40)));
        addAndMakeVisible(s); addChildComponent(l); l->setVisible(! (face && face->drawsLabels()));
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, id, *s));
    }
    if (face)
    {
        footswitch.imageFor = [this](bool down) { return face->footswitchImage(down); };
        if (pd.secondarySwitchId().isNotEmpty())
        {
            footswitch2.imageFor = footswitch.imageFor;
            addAndMakeVisible(footswitch2);
            secondAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                p.apvts, pd.secondarySwitchId(), footswitch2);
            footswitch2.setTooltip(tipFor(pd, pd.secondarySwitchId(), pd.secondarySwitchLabel()));
        }
        if (pd.toggleId.isNotEmpty() && pd.togglePositions == 3)
        {
            auto* param = p.apvts.getParameter(pd.toggleId);
            toggle3.imageFor = [this](int pos) { return face->toggleImage3(pos); };
            toggle3Att = std::make_unique<juce::ParameterAttachment>(*param, [this](float v) { toggle3.setPosition(juce::roundToInt(v)); });
            toggle3.onChange = [this](int pos) { toggle3Att->setValueAsCompleteGesture((float) pos); };
            toggle3Att->sendInitialUpdate();
            addAndMakeVisible(toggle3);
            toggle3.setTooltip(tipFor(pd, pd.toggleId, pd.toggleLabel));
        }
        else if (pd.toggleId.isNotEmpty())
        {
            toggle.imageFor = [this](bool up) { return face->toggleImage(up); };
            addAndMakeVisible(toggle);
            toggleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, pd.toggleId, toggle);
            toggle.setTooltip(tipFor(pd, pd.toggleId, pd.toggleLabel));
        }
        startTimerHz(20);
    }
    lastPrimary = pedal.primaryOn(); lastSecondary = pedal.secondaryOn();
}
PedalStrip::~PedalStrip()
{
    for (auto* s : knobs) s->setLookAndFeel(nullptr);
}
void PedalStrip::timerCallback()
{
    // repaint the LEDs when a switch changes (mouse or host automation)
    const int meter = pedal.isActive() ? pedal.ledMeter() : juce::jmin(pedal.ledMeter(), 0);   // bypassed: meter dark
    if (pedal.primaryOn() != lastPrimary || pedal.secondaryOn() != lastSecondary || meter != lastMeter)
    {
        lastPrimary = pedal.primaryOn(); lastSecondary = pedal.secondaryOn(); lastMeter = meter;
        repaint();
    }
}
FaceLayout PedalStrip::layout() const
{
    auto f = FaceLayout::make(bodyBounds(), knobs.size(), numSwitches(), pedal.toggleId.isNotEmpty(), pedal.getId());
    f.secondaryOn = pedal.secondaryOn();
    f.meter = pedal.ledMeter() < 0 ? -1 : (pedal.isActive() ? pedal.ledMeter() : 0);
    return f;
}
juce::Rectangle<float> PedalStrip::bodyBounds() const
{
    return getLocalBounds().toFloat().withTrimmedTop((float) BoardComponent::headerH + 4.0f).reduced(2.0f, 2.0f);
}
void PedalStrip::paint(juce::Graphics& g)
{
    // header: drag handle + name
    auto head = getLocalBounds().removeFromTop(BoardComponent::headerH).toFloat().reduced(2.0f, 0.0f);
    g.setColour(dragging ? juce::Colour(0xff3b8ce0) : juce::Colour(0xff2e3236));
    g.fillRoundedRectangle(head, 6.0f);
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    for (int i = 0; i < 3; ++i) g.fillEllipse(head.getX() + 10.0f + (float) i * 7.0f, head.getCentreY() - 2.0f, 4.0f, 4.0f);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText(pedal.getName(), head, juce::Justification::centred);

    auto body = bodyBounds();
    if (face)
    {
        face->paint(g, layout(), pedal.primaryOn());
        return;
    }
    g.setColour(juce::Colour(0xff2b6cb0));
    g.fillRoundedRectangle(body, 10.0f);
}
void PedalStrip::resized()
{
    auto head = getLocalBounds().removeFromTop(BoardComponent::headerH).reduced(4, 3);
    remove.setBounds(head.removeFromRight(22));
    if (face)
    {
        auto f = layout();
        for (int i = 0; i < knobs.size(); ++i)
        {
            knobs[i]->setBounds(face->knobBounds(f.knobs[i]).toNearestInt());
            labels[i]->setBounds(f.labels[i].toNearestInt());
        }
        footswitch.setBounds(face->switchBounds(f.footswitch).toNearestInt());
        if (f.twoSwitches) footswitch2.setBounds(face->switchBounds(f.footswitch2).toNearestInt());
        if (f.hasToggle) (pedal.togglePositions == 3 ? static_cast<juce::Component&>(toggle3) : toggle)
                             .setBounds(face->toggleBounds(f.toggle).toNearestInt());
        return;
    }
    auto r = bodyBounds().toNearestInt().reduced(8);
    bypass.setBounds(r.removeFromBottom(26));
    if (knobs.isEmpty()) return;
    r = r.withSizeKeepingCentre(r.getWidth(), juce::jmin(r.getHeight(), 110));
    int w = r.getWidth() / knobs.size();
    for (int i = 0; i < knobs.size(); ++i)
    {
        auto c = r.removeFromLeft(w);
        labels[i]->setBounds(c.removeFromBottom(20));
        knobs[i]->setBounds(c);
    }
}
juce::String PedalStrip::getTooltip()
{
    const auto basedOn = pedal.getBasedOn();
    if (basedOn.isEmpty() || dragging) return {};
    const auto pos = getMouseXYRelative().toFloat();
    bool overName = pos.y < (float) BoardComponent::headerH;
    if (face)
    {
        const auto f = layout();
        overName = overName || f.name.contains(pos) || f.subtitle.contains(pos);
    }
    if (! overName) return {};
    if (basedOn.startsWith("Generic")) return basedOn;
    return "Based on the " + basedOn + "\n"
           "Product names belong to their owners and are used only to identify the circuit modelled; "
           "not affiliated with or endorsed by them.";
}
void PedalStrip::mouseDown(const juce::MouseEvent& e)
{
    headerDrag = e.getPosition().y < BoardComponent::headerH;
    if (headerDrag) { dragging = true; setMouseCursor(juce::MouseCursor::DraggingHandCursor); board.beginDrag(*this); repaint(); }
}
void PedalStrip::mouseDrag(const juce::MouseEvent&) { if (headerDrag) board.dragTo(*this); }
void PedalStrip::mouseUp(const juce::MouseEvent&)
{
    if (! headerDrag) return;
    headerDrag = false; dragging = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    board.endDrag(*this); repaint();
}

// ---------------- BoardComponent ----------------
BoardComponent::BoardComponent(PedalboardProcessor& p) : proc(p) { rebuild(); }

void BoardComponent::rebuild()
{
    auto ids = proc.getBoard();
    juce::StringArray current;
    for (auto* s : strips) current.add(s->pedal.getId());
    if (ids == current) return;                       // nothing changed (e.g. our own drop)
    animator.cancelAllAnimations(false);
    strips.clear();
    for (auto& id : ids)
        if (auto* pd = proc.findPedal(id)) addAndMakeVisible(strips.add(new PedalStrip(proc, *pd, *this)));
    int w = gap;
    for (int i = 0; i < strips.size(); ++i) w += stripWidth(i) + gap;
    setSize(juce::jmax(slotW, w), boardH);
    layoutSlots(false);
}
int BoardComponent::stripWidth(int i) const
{
    return juce::isPositiveAndBelow(i, strips.size()) ? widthFor(juce::jmax(1, strips[i]->pedal.widthUnits())) : slotW;
}
int BoardComponent::slotX(int i) const
{
    int x = gap;
    for (int k = 0; k < i && k < strips.size(); ++k) x += stripWidth(k) + gap;
    return x;
}
void BoardComponent::layoutSlots(bool animate)
{
    for (int i = 0; i < strips.size(); ++i)
    {
        if (strips[i]->dragging) continue;
        juce::Rectangle<int> b(slotX(i), 4, stripWidth(i), getHeight() - 8);
        if (animate) animator.animateComponent(strips[i], b, 1.0f, 150, false, 1.0, 1.0);
        else strips[i]->setBounds(b);
    }
}
void BoardComponent::beginDrag(PedalStrip& s)
{
    animator.cancelAllAnimations(true);
    dragged = &s;
    dragIndex = strips.indexOf(&s);
    grabOffset = getLocalPoint(nullptr, juce::Desktop::getMousePosition()).x - s.getX();
    s.toFront(false);
    startTimerHz(60);               // drives auto-scroll near the edges
}
void BoardComponent::dragTo(PedalStrip& s)
{
    int mx = getLocalPoint(nullptr, juce::Desktop::getMousePosition()).x;
    const int sw = stripWidth(dragIndex);
    int x = juce::jlimit(0, juce::jmax(0, getWidth() - sw), mx - grabOffset);
    s.setTopLeftPosition(x, s.getY());
    // Target slot = the position (among the other strips) whose left edge is nearest the dragged strip's left edge.
    // Works for mixed single / double-wide pedals.
    int target = dragIndex, best = std::numeric_limits<int>::max(), left = gap, t = 0;
    for (int k = 0; k <= strips.size(); ++k)
    {
        if (k == dragIndex) continue;
        if (const int dist = std::abs(x - left); dist < best) { best = dist; target = t; }
        if (k < strips.size()) left += stripWidth(k) + gap;
        ++t;
    }
    target = juce::jlimit(0, strips.size() - 1, target);
    if (target != dragIndex)
    {
        strips.move(dragIndex, target);
        dragIndex = target;
        layoutSlots(true);          // the other pedals slide out of the way
    }
}
void BoardComponent::timerCallback()
{
    if (dragged == nullptr || viewport == nullptr) { stopTimer(); return; }
    // Mouse position relative to the visible area (not the full-width board)
    auto p = viewport->getLocalPoint(nullptr, juce::Desktop::getMousePosition());
    const int edge = 60, maxSpeed = 24, w = viewport->getMaximumVisibleWidth();
    int speed = 0;
    if (p.x < edge)          speed = -juce::jmap(juce::jmax(0, p.x), 0, edge, maxSpeed, 2);
    else if (p.x > w - edge) speed =  juce::jmap(juce::jmin(w, p.x), w - edge, w, 2, maxSpeed);
    if (speed != 0)
    {
        auto pos = viewport->getViewPosition();
        viewport->setViewPosition(juce::jlimit(0, juce::jmax(0, getWidth() - w), pos.x + speed), pos.y);
        dragTo(*dragged);           // keep the pedal under the cursor as the view moves
    }
}
void BoardComponent::endDrag(PedalStrip& s)
{
    stopTimer();
    dragged = nullptr;
    animator.animateComponent(&s, { slotX(dragIndex), 4, stripWidth(dragIndex), getHeight() - 8 }, 1.0f, 120, false, 1.0, 1.0);
    juce::StringArray ids;
    for (auto* p : strips) ids.add(p->pedal.getId());
    proc.setBoard(ids);             // commit new order to the audio chain + saved state
    dragIndex = -1;
}
void BoardComponent::removePedal(PedalBase& p)
{
    auto ids = proc.getBoard(); ids.removeString(p.getId());
    // don't delete the button mid-click; the editor may be closed before this runs
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<BoardComponent>(this), ids]
                                    { if (safe != nullptr) safe->proc.setBoard(ids); });
}

// ---------------- Editor ----------------
PedalboardEditor::PedalboardEditor(PedalboardProcessor& p)
    : AudioProcessorEditor(p), proc(p), board(p),
      inputLevel("INPUT", p.apvts, "board_in", [&p] { return PedalboardProcessor::takePeak(p.inPeak); }),
      outputLevel("OUTPUT", p.apvts, "board_out", [&p] { return PedalboardProcessor::takePeak(p.outPeak); }),
      transposerPanel(p.apvts, [&p] { return p.studioLatencySamples(); }),
      presetBar(p.presets)
{
    addAndMakeVisible(presetBar);
    addAndMakeVisible(transposerPanel);
    addAndMakeVisible(inputLevel); addAndMakeVisible(outputLevel);
    viewport.setViewedComponent(&board, false);
    board.setViewport(&viewport);
    viewport.setScrollBarsShown(false, true);
    addAndMakeVisible(viewport); addAndMakeVisible(addButton);
    addButton.onClick = [this]
    {
        // One submenu per category (menu order = allCategories), pedals A-Z inside each.
        // Item id = index in proc.pedals + 1. Pedals already on the board are greyed out.
        juce::PopupMenu m; auto onBoard = proc.getBoard();
        for (auto cat : allCategories)
        {
            std::vector<int> idx;
            for (int i = 0; i < (int) proc.pedals.size(); ++i)
                if (proc.pedals[(size_t) i]->getCategory() == cat) idx.push_back(i);
            if (idx.empty()) continue;
            std::sort(idx.begin(), idx.end(), [this](int a, int b)
                { return proc.pedals[(size_t) a]->getName().compareNatural(proc.pedals[(size_t) b]->getName()) < 0; });
            juce::PopupMenu sub; int available = 0;
            for (int i : idx)
            {
                auto& pd = proc.pedals[(size_t) i];
                const bool free = ! onBoard.contains(pd->getId());
                sub.addItem(i + 1, pd->getName(), free, ! free);    // ticked + greyed = already on the board
                available += free ? 1 : 0;
            }
            m.addSubMenu(categoryName(cat), sub, available > 0);
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addButton), [this](int r)
        {
            if (r <= 0) return;
            auto ids = proc.getBoard(); ids.add(proc.pedals[(size_t) r - 1]->getId()); proc.setBoard(ids);
        });
    };
    proc.addChangeListener(this);
    setResizable(true, true); setResizeLimits(minWidth, editorH, 2400, editorH);
    updateSize();
}
PedalboardEditor::~PedalboardEditor() { proc.removeChangeListener(this); }
void PedalboardEditor::updateSize() { setSize(juce::jlimit(minWidth, 1400, board.getWidth() + 8), editorH); }
void PedalboardEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    auto r = getLocalBounds();
    g.setColour(juce::Colour(0xff1d1f22));
    g.fillRect(r.removeFromTop(presetH));
    g.setColour(juce::Colour(0xff232629));
    g.fillRect(r.removeFromTop(topBarH));
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRect(0, presetH, getWidth(), 1);
}
void PedalboardEditor::resized()
{
    auto r = getLocalBounds();
    presetBar.setBounds(r.removeFromTop(presetH));
    auto bar = r.removeFromTop(topBarH).reduced(8, 6);
    inputLevel.setBounds(bar.removeFromLeft(240));
    outputLevel.setBounds(bar.removeFromRight(240));
    addButton.setBounds(bar.withSizeKeepingCentre(130, 28));
    transposerPanel.setBounds(r.removeFromBottom(globalH).withTrimmedBottom(6));
    viewport.setBounds(r);
}
