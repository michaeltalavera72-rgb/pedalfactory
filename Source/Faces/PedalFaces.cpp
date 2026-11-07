#include "PedalFace.h"
#include "BinaryData.h"
#include "../UI/StompboxControls.h"

namespace
{
    const juce::Colour steelLight  { 0xffeceef0 }, steelDark { 0xffc2c6cb },
                       charcoal    { 0xff1d1f22 }, amber     { 0xffd9861c }, amberDark { 0xff8a4f0c };

    juce::Typeface::Ptr slabTypeface()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor(BinaryData::AlfaSlabOneRegular_ttf,
                                                                 (size_t) BinaryData::AlfaSlabOneRegular_ttfSize);
        return tf;
    }

    // One curled horn: a tapering spiral, thin at the curl's centre, wide at the root.
    juce::Path hornPath(float R, float turns)
    {
        juce::Path p;
        const int n = 120;
        const float tMax = turns * juce::MathConstants<float>::twoPi;
        juce::Array<juce::Point<float>> outer, inner;
        for (int i = 0; i <= n; ++i)
        {
            const float u = (float) i / (float) n, t = u * tMax;
            const float r = R * (0.10f + 0.90f * u);
            const float w = R * (0.04f + 0.36f * u * u);
            const juce::Point<float> dir { std::cos(t), std::sin(t) };
            outer.add(dir * (r + w * 0.5f));
            inner.add(dir * juce::jmax(0.0f, r - w * 0.5f));
        }
        p.startNewSubPath(outer[0]);
        for (int i = 1; i <= n; ++i) p.lineTo(outer[i]);
        for (int i = n; i >= 0; --i) p.lineTo(inner[i]);
        p.closeSubPath();
        return p;
    }

    void drawScrew(juce::Graphics& g, juce::Point<float> c, float r, float angle)
    {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff4f5f6), c.x - r, c.y - r,
                                               juce::Colour(0xff7c8186), c.x + r, c.y + r, false));
        g.fillEllipse(c.x - r, c.y - r, r * 2, r * 2);
        g.setColour(juce::Colour(0xff55595e));
        g.drawEllipse(c.x - r, c.y - r, r * 2, r * 2, 0.8f);
        const float dx = std::cos(angle) * r * 0.7f, dy = std::sin(angle) * r * 0.7f;
        g.drawLine(c.x - dx, c.y - dy, c.x + dx, c.y + dy, 1.4f);
        g.drawLine(c.x + dy, c.y - dx, c.x - dy, c.y + dx, 1.4f);
    }
}

// ---------------------------------------------------------------------------
// Shared behaviour for pedals with Blender renders (Tools/Blender/render_<name>.py).
// If <prefix>_face.png is compiled in, the face, knob frames, footswitch and LED images are used;
// otherwise the pedal's own vector drawing (paintVector) is shown.
// Pad factors and grid columns MUST match pedal_render_common.py / the render script.
class RenderedFace : public PedalFace
{
public:
    static constexpr float knobPad = 1.30f, switchPad = 1.35f, ledPad = 2.6f;
    static constexpr int knobGridColumns = 8;                                     // = pedal_render_common.GRID_COLS

    explicit RenderedFace(const juce::String& prefix, int widthUnits = 1)
    {
        face      = loadImage(prefix + "_face_png");
        if (face.isValid() && std::abs((float) face.getHeight() / (float) face.getWidth() - FaceLayout::aspectFor(widthUnits)) > 0.02f)
            face = {};                     // rendered for an older body shape: use the vector face until re-rendered
        switchUp  = loadImage(prefix + "_switch_up_png");
        switchDn  = loadImage(prefix + "_switch_down_png");
        ledOn     = loadImage(prefix + "_led_on_png");
        ledOff    = loadImage(prefix + "_led_off_png");
        led2On    = loadImage(prefix + "_led2_on_png");                 // optional: a second LED colour
        led2Off   = loadImage(prefix + "_led2_off_png");
        auto strip = loadImage(prefix + "_knob_strip_png");
        if (strip.isValid())
            knobLook = std::make_unique<FilmstripKnobLookAndFeel>(strip, knobGridColumns);
    }

    bool rendered() const { return face.isValid(); }
    bool drawsLabels() const override { return rendered(); }
    juce::LookAndFeel* knobLookAndFeel() override { return rendered() ? knobLook.get() : nullptr; }
    juce::Rectangle<float> knobBounds(juce::Rectangle<float> r) const override
    {
        return rendered() && knobLook ? r.withSizeKeepingCentre(r.getWidth() * knobPad, r.getHeight() * knobPad) : r;
    }
    juce::Rectangle<float> switchBounds(juce::Rectangle<float> r) const override
    {
        return rendered() && switchUp.isValid() ? r.withSizeKeepingCentre(r.getWidth() * switchPad, r.getHeight() * switchPad) : r;
    }
    const juce::Image* footswitchImage(bool down) const override
    {
        if (! rendered()) return nullptr;
        auto& img = down ? switchDn : switchUp;
        return img.isValid() ? &img : nullptr;
    }

    void paint(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        if (rendered()) paintRendered(g, f, active);
        else            paintVector(g, f, active);
    }

protected:
    virtual void paintVector(juce::Graphics&, const FaceLayout&, bool active) = 0;

    static void drawLed(juce::Graphics& g, juce::Rectangle<float> l, bool active, bool blue = false)
    {
        using namespace juce;
        g.setColour(Colour(0xff6f7378)); g.fillEllipse(l.expanded(2.5f));
        g.setColour(Colour(0xff2a2c2f)); g.fillEllipse(l.expanded(1.0f));
        if (active)
        {
            g.setColour((blue ? Colour(0xff2a6cff) : Colour(0xffff2a1a)).withAlpha(0.25f)); g.fillEllipse(l.expanded(6.0f));
            g.setGradientFill(ColourGradient(blue ? Colour(0xffd0e4ff) : Colour(0xffffd0c0), l.getCentreX() - 1, l.getCentreY() - 2,
                                             blue ? Colour(0xff1440e0) : Colour(0xffe01a0e), l.getRight(), l.getBottom(), true));
        }
        else
            g.setColour(blue ? Colour(0xff0c1a4a) : Colour(0xff4a0d0a));
        g.fillEllipse(l);
    }

    static juce::Image loadImage(const juce::String& resourceName)
    {
        int size = 0;
        if (auto* data = BinaryData::getNamedResource(resourceName.toRawUTF8(), size))
            return juce::ImageCache::getFromMemory(data, size);
        return {};
    }

private:
    juce::Image face, switchUp, switchDn, ledOn, ledOff, led2On, led2Off;
    std::unique_ptr<FilmstripKnobLookAndFeel> knobLook;

    void paintRendered(juce::Graphics& g, const FaceLayout& f, bool active)
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setImageResamplingQuality(Graphics::highResamplingQuality);
        g.setOpacity(1.0f);                         // drawImage uses the current colour's alpha
        g.drawImage(face, b);

        // LED lens; the lit image already contains its glow
        const auto led = f.led.withSizeKeepingCentre(f.led.getWidth() * ledPad, f.led.getHeight() * ledPad);
        if (auto& img = active ? ledOn : ledOff; img.isValid())
            g.drawImage(img, led);
        if (f.twoSwitches)
        {
            const auto led2 = f.led2.withSizeKeepingCentre(f.led2.getWidth() * ledPad, f.led2.getHeight() * ledPad);
            const bool own = led2On.isValid() && led2Off.isValid();
            if (auto& img = f.secondaryOn ? (own ? led2On : ledOn) : (own ? led2Off : ledOff); img.isValid())
                g.drawImage(img, led2);
        }
    }
};

// ---------------------------------------------------------------------------
// Ramhorn Fuzz: brushed steel enclosure, big black knobs, heavy slab name in
// charcoal with an amber shadow, and a twin-horn emblem.
class RamhornFace : public RenderedFace
{
public:
    RamhornFace() : RenderedFace("ramhorn") {}
    juce::Colour labelColour() const override { return charcoal; }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        const float corner = 9.0f;

        // enclosure shadow + brushed steel (cached, regenerated when the size changes)
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), corner);
        Path body; body.addRoundedRectangle(b, corner);
        {
            Graphics::ScopedSaveState s(g);
            g.reduceClipRegion(body);
            g.drawImage(steel(b.getWidth(), b.getHeight(), g), b);
        }
        // edge bevel
        g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.7f), b.getX(), b.getY(),
                                         Colours::black.withAlpha(0.35f), b.getX(), b.getBottom(), false));
        g.drawRoundedRectangle(b.reduced(0.75f), corner, 1.5f);

        // screws
        const float sr = 4.0f, inset = 9.0f;
        drawScrew(g, { b.getX() + inset, b.getY() + inset }, sr, 0.3f);
        drawScrew(g, { b.getRight() - inset, b.getY() + inset }, sr, 1.1f);
        drawScrew(g, { b.getX() + inset, b.getBottom() - inset }, sr, 0.8f);
        drawScrew(g, { b.getRight() - inset, b.getBottom() - inset }, sr, 0.1f);

        // name: heavy slab letters, amber offset shadow under charcoal
        auto tf = slabTypeface();
        Font nameFont(FontOptions(tf).withHeight(f.name.getHeight() * 0.95f).withKerningFactor(0.02f));
        g.setFont(nameFont);
        g.setColour(amber);
        g.drawFittedText("RAMHORN", f.name.translated(2.0f, 2.0f).toNearestInt(), Justification::centred, 1, 0.7f);
        g.setColour(charcoal);
        g.drawFittedText("RAMHORN", f.name.toNearestInt(), Justification::centred, 1, 0.7f);

        // subtitle with thin rules either side
        Font subFont(FontOptions(tf).withHeight(f.subtitle.getHeight() * 0.9f).withKerningFactor(0.45f));
        g.setFont(subFont);
        g.setColour(amberDark);
        g.drawText("FUZZ", f.subtitle, Justification::centred);
        const float ruleY = f.subtitle.getCentreY(), textHalf = f.subtitle.getWidth() * 0.19f;
        g.setColour(charcoal);
        g.fillRect(f.subtitle.getX() + 6.0f, ruleY - 0.75f, f.subtitle.getWidth() * 0.5f - textHalf - 10.0f, 1.5f);
        g.fillRect(f.subtitle.getCentreX() + textHalf + 4.0f, ruleY - 0.75f, f.subtitle.getWidth() * 0.5f - textHalf - 10.0f, 1.5f);

        // emblem: two horns curling outward, mirrored
        const auto e = f.emblem;
        const float R = e.getHeight() * 0.5f;
        auto horn = hornPath(R, 1.6f);
        const float gap = e.getWidth() * 0.27f;
        auto left  = AffineTransform::rotation(-MathConstants<float>::halfPi * 0.6f)
                         .translated(e.getCentreX() - gap, e.getCentreY());
        auto right = AffineTransform::scale(-1.0f, 1.0f)
                         .followedBy(AffineTransform::rotation(MathConstants<float>::halfPi * 0.6f))
                         .translated(e.getCentreX() + gap, e.getCentreY());
        for (auto& t : { left, right })
        {
            g.setColour(amber.withAlpha(0.9f));
            g.fillPath(horn, t.translated(1.5f, 1.5f));
            g.setColour(charcoal);
            g.fillPath(horn, t);
        }
        // small diamond between the horns
        Path dia;
        const auto dc = e.getCentre();
        const float ds = R * 0.32f;
        dia.addQuadrilateral(dc.x, dc.y - ds, dc.x + ds * 0.7f, dc.y, dc.x, dc.y + ds, dc.x - ds * 0.7f, dc.y);
        g.setColour(amber); g.fillPath(dia);

        // status LED (bezel + lens; glows when the pedal is on)
        const auto l = f.led;
        g.setColour(Colour(0xff6f7378)); g.fillEllipse(l.expanded(2.5f));
        g.setColour(Colour(0xff2a2c2f)); g.fillEllipse(l.expanded(1.0f));
        if (active)
        {
            g.setColour(Colour(0xffff2a1a).withAlpha(0.25f)); g.fillEllipse(l.expanded(6.0f));
            g.setGradientFill(ColourGradient(Colour(0xffffd0c0), l.getCentreX() - 1, l.getCentreY() - 2,
                                             Colour(0xffe01a0e), l.getRight(), l.getBottom(), true));
        }
        else
            g.setColour(Colour(0xff4a0d0a));
        g.fillEllipse(l);
    }

    juce::Image steelCache;

    const juce::Image& steel(float w, float h, juce::Graphics& g)
    {
        const float scale = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int iw = juce::roundToInt(w * scale), ih = juce::roundToInt(h * scale);
        if (steelCache.getWidth() == iw && steelCache.getHeight() == ih) return steelCache;

        steelCache = juce::Image(juce::Image::ARGB, iw, ih, true);
        juce::Graphics s(steelCache);
        s.setGradientFill(juce::ColourGradient(steelLight, 0, 0, steelDark, 0, (float) ih, false));
        s.fillAll();
        juce::Random rng(0x5eed);                 // fixed seed: same grain every time
        for (int y = 0; y < ih; ++y)              // horizontal brushing
        {
            s.setColour((rng.nextBool() ? juce::Colours::white : juce::Colours::black)
                            .withAlpha(rng.nextFloat() * 0.045f));
            s.fillRect(0, y, iw, 1);
        }
        for (int i = 0; i < ih * 3; ++i)          // short streaks
        {
            const int y = rng.nextInt(ih), x = rng.nextInt(iw), len = 10 + rng.nextInt(iw / 2);
            s.setColour((rng.nextBool() ? juce::Colours::white : juce::Colours::black)
                            .withAlpha(0.02f + rng.nextFloat() * 0.035f));
            s.fillRect(x, y, len, 1);
        }
        // soft diagonal sheen
        juce::ColourGradient sheen(juce::Colours::transparentWhite, 0, 0, juce::Colours::transparentWhite, (float) iw, (float) ih, false);
        sheen.addColour(0.45, juce::Colours::white.withAlpha(0.18f));
        s.setGradientFill(sheen);
        s.fillAll();
        return steelCache;
    }
};

// ---------------------------------------------------------------------------
// Buzzsaw: blood-red textured powder coat, bone-white lettering, circular saw-blade emblem,
// four knobs (Level, Low, High, Dist). Vector fallback for before render_buzzsaw.py is run.
namespace
{
    const juce::Colour powderRed { 0xff7a0d0f }, powderRedDark { 0xff4a0608 }, bone { 0xffe6dfcc }, inkBlack { 0xff121212 };

    juce::Typeface::Ptr metalTypeface()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor(BinaryData::MetalManiaRegular_ttf,
                                                                 (size_t) BinaryData::MetalManiaRegular_ttfSize);
        return tf;
    }

    // circular saw blade: toothed disc with a centre hole (original design)
    juce::Path sawBlade(juce::Point<float> c, float R, int teeth = 18)
    {
        juce::Path p;
        for (int i = 0; i < teeth; ++i)
        {
            const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) teeth;
            const float a1 = a0 + juce::MathConstants<float>::twoPi * 0.62f / (float) teeth;   // tooth tip
            const float a2 = a0 + juce::MathConstants<float>::twoPi / (float) teeth;
            auto pt = [&](float a, float r) { return juce::Point<float>(c.x + std::cos(a) * r, c.y + std::sin(a) * r); };
            if (i == 0) p.startNewSubPath(pt(a0, R * 0.80f)); else p.lineTo(pt(a0, R * 0.80f));
            p.lineTo(pt(a1, R));
            p.lineTo(pt(a2, R * 0.80f));
        }
        p.closeSubPath();
        p.addEllipse(c.x - R * 0.22f, c.y - R * 0.22f, R * 0.44f, R * 0.44f);
        p.setUsingNonZeroWinding(false);
        return p;
    }
}

class BuzzsawFace : public RenderedFace
{
public:
    BuzzsawFace() : RenderedFace("buzzsaw") {}
    juce::Colour labelColour() const override { return bone; }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(powderRed.brighter(0.15f), b.getX(), b.getY(), powderRedDark, b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        g.setColour(Colours::white.withAlpha(0.18f));
        g.drawRoundedRectangle(b.reduced(0.75f), 9.0f, 1.5f);
        const float sr = 4.0f, inset = 9.0f;
        drawScrew(g, { b.getX() + inset, b.getY() + inset }, sr, 0.3f);
        drawScrew(g, { b.getRight() - inset, b.getY() + inset }, sr, 1.1f);
        drawScrew(g, { b.getX() + inset, b.getBottom() - inset }, sr, 0.8f);
        drawScrew(g, { b.getRight() - inset, b.getBottom() - inset }, sr, 0.1f);

        auto tf = metalTypeface();
        g.setFont(Font(FontOptions(tf).withHeight(f.name.getHeight() * 1.05f)));
        g.setColour(inkBlack.withAlpha(0.8f));
        g.drawFittedText("BUZZSAW", f.name.translated(1.5f, 1.5f).toNearestInt(), Justification::centred, 1, 0.6f);
        g.setColour(bone);
        g.drawFittedText("BUZZSAW", f.name.toNearestInt(), Justification::centred, 1, 0.6f);
        g.setFont(Font(FontOptions(f.subtitle.getHeight() * 0.62f, Font::bold).withKerningFactor(0.35f)));
        g.drawText("DISTORTION", f.subtitle, Justification::centred);

        const auto e = f.emblem;
        const float R = e.getHeight() * 0.5f;
        g.setColour(inkBlack.withAlpha(0.8f));
        g.fillPath(sawBlade(e.getCentre().translated(1.2f, 1.2f), R));
        g.setColour(bone);
        g.fillPath(sawBlade(e.getCentre(), R));

        drawLed(g, f.led, active);
    }
};

// ---------------------------------------------------------------------------
// Rail Bender: burnt-orange paint with rust patina, black lettering in Rye, a bent railway-track
// emblem, four knobs (Drive, Tone, Level, Boost) and two footswitches (Drive / Boost).
namespace
{
    const juce::Colour burntOrange { 0xffa4501e }, rustDark { 0xff5a2810 }, railInk { 0xff141210 };

    juce::Typeface::Ptr ryeTypeface()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor(BinaryData::RyeRegular_ttf,
                                                                 (size_t) BinaryData::RyeRegular_ttfSize);
        return tf;
    }

    // a short length of railway track bent into an arc: two rails and the sleepers (original design)
    void drawBentRail(juce::Graphics& g, juce::Rectangle<float> e, juce::Colour ink)
    {
        using namespace juce;
        const float R = e.getWidth() * 1.05f;                       // arc radius
        const Point<float> c { e.getCentreX(), e.getBottom() + R * 0.62f };
        const float a0 = -0.48f, a1 = 0.48f;                       // radians either side of vertical
        const float gauge = e.getHeight() * 0.30f;
        auto arc = [&](float r) { Path p; p.addCentredArc(c.x, c.y, r, r, 0.0f, a0, a1, true); return p; };
        g.setColour(ink.withAlpha(0.85f));
        for (int i = 0; i <= 8; ++i)                               // sleepers
        {
            const float a = a0 + (a1 - a0) * (float) i / 8.0f;
            const Point<float> p1 { c.x + std::sin(a) * (R - gauge * 1.35f), c.y - std::cos(a) * (R - gauge * 1.35f) };
            const Point<float> p2 { c.x + std::sin(a) * (R + gauge * 0.35f), c.y - std::cos(a) * (R + gauge * 0.35f) };
            g.drawLine({ p1, p2 }, 3.0f);
        }
        g.setColour(ink);
        g.strokePath(arc(R), PathStrokeType(2.6f, PathStrokeType::curved, PathStrokeType::rounded));
        g.strokePath(arc(R - gauge), PathStrokeType(2.6f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

class RailBenderFace : public RenderedFace
{
public:
    RailBenderFace() : RenderedFace("railbender") {}
    juce::Colour labelColour() const override { return railInk; }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(burntOrange.brighter(0.1f), b.getX(), b.getY(), burntOrange.darker(0.35f), b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        // a few rust blotches near the edges
        Random rng(7);
        for (int i = 0; i < 26; ++i)
        {
            const bool edgeX = rng.nextBool();
            const float x = edgeX ? (rng.nextBool() ? b.getX() + rng.nextFloat() * 14.0f : b.getRight() - rng.nextFloat() * 14.0f)
                                  : b.getX() + rng.nextFloat() * b.getWidth();
            const float y = edgeX ? b.getY() + rng.nextFloat() * b.getHeight()
                                  : (rng.nextBool() ? b.getY() + rng.nextFloat() * 12.0f : b.getBottom() - rng.nextFloat() * 12.0f);
            const float r = 2.0f + rng.nextFloat() * 6.0f;
            g.setColour(rustDark.withAlpha(0.35f + rng.nextFloat() * 0.3f));
            g.fillEllipse(x - r, y - r * 0.7f, r * 2.0f, r * 1.4f);
        }
        g.setColour(Colours::white.withAlpha(0.15f));
        g.drawRoundedRectangle(b.reduced(0.75f), 9.0f, 1.5f);
        const float sr = 4.0f, inset = 9.0f;
        drawScrew(g, { b.getX() + inset, b.getY() + inset }, sr, 0.3f);
        drawScrew(g, { b.getRight() - inset, b.getY() + inset }, sr, 1.1f);
        drawScrew(g, { b.getX() + inset, b.getBottom() - inset }, sr, 0.8f);
        drawScrew(g, { b.getRight() - inset, b.getBottom() - inset }, sr, 0.1f);

        g.setColour(railInk);
        g.setFont(Font(FontOptions(ryeTypeface()).withHeight(f.name.getHeight() * 0.72f)));
        g.drawFittedText("RAIL BENDER", f.name.toNearestInt(), Justification::centred, 1, 0.6f);
        g.setFont(Font(FontOptions(f.subtitle.getHeight() * 0.62f, Font::bold).withKerningFactor(0.35f)));
        g.drawText("OVERDRIVE", f.subtitle, Justification::centred);
        drawBentRail(g, f.emblem, railInk);

        g.setFont(FontOptions(10.5f, Font::bold));
        g.drawText("DRIVE", f.switchLabel1, Justification::centred);
        g.drawText("BOOST", f.switchLabel2, Justification::centred);
        drawLed(g, f.led, active);
        drawLed(g, f.led2, f.secondaryOn);
    }
};

// ---------------------------------------------------------------------------
// Valve Howler: metallic jade-green paint, cream lettering in Poppins Bold Italic with a thin cream
// pinstripe panel, a vacuum-tube emblem with a glowing filament and "howl" waves on both sides,
// three knobs (Drive, Tone, Level) and one footswitch. Original design.
namespace
{
    const juce::Colour jade { 0xff1c7457 }, jadeDeep { 0xff0d3f2f }, creamInk { 0xfff0e4c4 }, filament { 0xffffa53a };

    juce::Typeface::Ptr poppinsTypeface()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor(BinaryData::PoppinsBoldItalic_ttf,
                                                                 (size_t) BinaryData::PoppinsBoldItalic_ttfSize);
        return tf;
    }

    // Geometry shared with render_valvehowler.py (keep in sync): everything is relative to the emblem rect e.
    // Tube: centred, height = e.h, glass width = 0.50 e.h; base = bottom 18 %; dome top.
    // Waves: three arcs each side, radii 0.25 e.h + 0.36/0.52/0.68 e.h from the glass centre, +/-0.50 rad.
    void drawValveEmblem(juce::Graphics& g, juce::Rectangle<float> e, juce::Colour ink, bool glow)
    {
        using namespace juce;
        const float h = e.getHeight(), cx = e.getCentreX();
        const float gw = h * 0.50f, baseH = h * 0.18f;
        const Rectangle<float> glass { cx - gw * 0.5f, e.getY(), gw, h - baseH };
        const Rectangle<float> base  { cx - gw * 0.42f, glass.getBottom(), gw * 0.84f, baseH * 0.62f };
        const float stroke = jmax(1.2f, h * 0.045f);

        Path env;                                             // glass envelope: dome top, straight sides
        env.startNewSubPath(glass.getX(), glass.getBottom());
        env.lineTo(glass.getX(), glass.getY() + gw * 0.5f);
        env.addCentredArc(cx, glass.getY() + gw * 0.5f, gw * 0.5f, gw * 0.5f, 0.0f,
                          -MathConstants<float>::halfPi, MathConstants<float>::halfPi);
        env.lineTo(glass.getRight(), glass.getBottom());
        if (glow)
        {
            g.setGradientFill(ColourGradient(filament.withAlpha(0.55f), cx, glass.getCentreY() + h * 0.08f,
                                             filament.withAlpha(0.0f), cx + gw * 0.5f, glass.getY(), true));
            g.fillPath(env);
        }
        g.setColour(ink);
        g.strokePath(env, PathStrokeType(stroke, PathStrokeType::curved, PathStrokeType::rounded));
        g.fillRoundedRectangle(base, stroke);
        for (int i = 0; i < 3; ++i)                           // pins
        {
            const float px = base.getX() + base.getWidth() * (0.25f + 0.25f * (float) i);
            g.drawLine(px, base.getBottom(), px, glass.getBottom() + baseH, stroke * 0.7f);
        }
        // plate (open rectangle) and filament (zig-zag) inside the glass
        const Rectangle<float> plate { cx - gw * 0.24f, glass.getY() + gw * 0.45f, gw * 0.48f, glass.getHeight() * 0.48f };
        g.drawRect(plate, stroke * 0.7f);
        Path fil;
        const float fy0 = plate.getBottom() - plate.getHeight() * 0.15f, fy1 = plate.getY() + plate.getHeight() * 0.25f;
        fil.startNewSubPath(cx - gw * 0.12f, glass.getBottom() - h * 0.02f);
        fil.lineTo(cx - gw * 0.12f, fy0);
        for (int i = 0; i < 4; ++i) fil.lineTo(cx + (i % 2 == 0 ? gw * 0.12f : -gw * 0.12f), fy0 - (fy0 - fy1) * (float) (i + 1) / 4.0f);
        g.setColour(glow ? filament : ink);
        g.strokePath(fil, PathStrokeType(stroke * 0.8f, PathStrokeType::mitered, PathStrokeType::rounded));
        // howl waves
        g.setColour(ink);
        const Point<float> c { cx, glass.getCentreY() };
        for (int side = -1; side <= 1; side += 2)
            for (int i = 0; i < 3; ++i)
            {
                const float r = h * (0.36f + 0.16f * (float) i) + gw * 0.5f;
                const float mid = side * MathConstants<float>::halfPi;
                Path a; a.addCentredArc(c.x, c.y, r, r, 0.0f, mid - 0.50f, mid + 0.50f, true);
                g.strokePath(a, PathStrokeType(stroke * (1.0f - 0.15f * (float) i), PathStrokeType::curved, PathStrokeType::rounded));
            }
    }
}

class ValveHowlerFace : public RenderedFace
{
public:
    ValveHowlerFace() : RenderedFace("valvehowler") {}
    juce::Colour labelColour() const override { return creamInk; }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(jade.brighter(0.25f), b.getX(), b.getY(), jadeDeep, b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        Random rng(11);                                       // metal-flake sparkle
        for (int i = 0; i < 260; ++i)
        {
            g.setColour(Colours::white.withAlpha(0.05f + rng.nextFloat() * 0.12f));
            g.fillRect(b.getX() + rng.nextFloat() * b.getWidth(), b.getY() + rng.nextFloat() * b.getHeight(), 1.0f, 1.0f);
        }
        g.setColour(Colours::white.withAlpha(0.15f));
        g.drawRoundedRectangle(b.reduced(0.75f), 9.0f, 1.5f);
        const float sr = 4.0f, inset = 9.0f;
        drawScrew(g, { b.getX() + inset, b.getY() + inset }, sr, 0.5f);
        drawScrew(g, { b.getRight() - inset, b.getY() + inset }, sr, 1.3f);
        drawScrew(g, { b.getX() + inset, b.getBottom() - inset }, sr, 0.2f);
        drawScrew(g, { b.getRight() - inset, b.getBottom() - inset }, sr, 0.9f);

        // cream pinstripe panel from the name down to the bottom, framing emblem, LED and footswitch (= render_valvehowler.py PANEL)
        g.setColour(creamInk);
        g.drawRoundedRectangle(panelRect(b), b.getWidth() * 0.035f, 1.4f);

        g.setFont(Font(FontOptions(poppinsTypeface()).withHeight(f.name.getHeight() * 0.62f)));
        g.drawFittedText("VALVE HOWLER", f.name.toNearestInt(), Justification::centred, 1, 0.7f);
        g.setFont(Font(FontOptions(f.subtitle.getHeight() * 0.62f, Font::bold).withKerningFactor(0.35f)));
        g.drawText("OVERDRIVE", f.subtitle, Justification::centred);
        drawValveEmblem(g, f.emblem, creamInk, active);
        drawLed(g, f.led, active);
    }

public:
    static juce::Rectangle<float> panelRect(juce::Rectangle<float> b)
    {
        return { b.getX() + b.getWidth() * 0.07f, b.getY() + b.getHeight() * 0.335f,
                 b.getWidth() * 0.86f, b.getHeight() * 0.612f };   // down to the bottom: same margin as the sides
    }
};

// ---------------------------------------------------------------------------
// Feral Hound: matte black enclosure, blood-red ink, a full-colour snarling hound printed big in the
// lower half (Assets/feralhound_dog.png, Michael's artwork; black background already turned into alpha),
// "FERAL HOUND" in Black Ops One across its ears with a black knock-out outline, six knobs in two rows
// (Gain/Filter/Level big, Fat/Soft/Hard small), footswitch with "DISTORTION" to its left and the LED to
// its right. Original design.
namespace
{
    const juce::Colour matteBlack { 0xff151517 }, matteDeep { 0xff09090a }, bloodInk { 0xffc0161d };

    juce::Typeface::Ptr stencilTypeface()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor(BinaryData::BlackOpsOneRegular_ttf,
                                                                 (size_t) BinaryData::BlackOpsOneRegular_ttfSize);
        return tf;
    }
}

class FeralHoundFace : public RenderedFace
{
public:
    FeralHoundFace() : RenderedFace("feralhound")
    {
        dog = juce::ImageCache::getFromMemory(BinaryData::feralhound_dog_png, BinaryData::feralhound_dog_pngSize);
    }
    juce::Colour labelColour() const override { return bloodInk.brighter(0.25f); }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(matteBlack.brighter(0.08f), b.getX(), b.getY(), matteDeep, b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        Random rng(23);                                       // powder-coat grain
        for (int i = 0; i < 420; ++i)
        {
            g.setColour(Colours::white.withAlpha(0.02f + rng.nextFloat() * 0.04f));
            g.fillRect(b.getX() + rng.nextFloat() * b.getWidth(), b.getY() + rng.nextFloat() * b.getHeight(), 1.0f, 1.0f);
        }
        g.setColour(Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(b.reduced(0.75f), 9.0f, 1.5f);
        const float sr = 4.0f, inset = 9.0f;
        drawScrew(g, { b.getX() + inset, b.getY() + inset }, sr, 0.7f);
        drawScrew(g, { b.getRight() - inset, b.getY() + inset }, sr, 0.1f);
        drawScrew(g, { b.getX() + inset, b.getBottom() - inset }, sr, 1.1f);
        drawScrew(g, { b.getRight() - inset, b.getBottom() - inset }, sr, 0.4f);

        if (dog.isValid())
        {
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.setOpacity(1.0f);
            g.drawImage(dog, f.emblem);
        }

        // name: black knock-out outline under the red, so it reads over the fur
        GlyphArrangement ga;
        const Font nameFont(FontOptions(stencilTypeface()).withHeight(f.name.getHeight() * 0.70f));
        ga.addFittedText(nameFont, "FERAL HOUND", f.name.getX(), f.name.getY(), f.name.getWidth(), f.name.getHeight(),
                         Justification::centred, 1, 0.7f);
        Path text; ga.createPath(text);
        DropShadow(Colours::black.withAlpha(0.8f), 6, {}).drawForPath(g, text);
        g.setColour(Colours::black.withAlpha(0.9f));
        g.strokePath(text, PathStrokeType(2.4f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour(bloodInk);
        g.fillPath(text);

        g.setFont(Font(FontOptions(f.subtitle.getHeight() * 0.62f, Font::bold).withKerningFactor(0.12f)));
        g.drawText("DISTORTION", f.subtitle, Justification::centred);
        drawLed(g, f.led, active);
    }

private:
    juce::Image dog;
};


// ---------------------------------------------------------------------------
// Slow Burn: gloss black enclosure; Michael's candle-and-sun artwork (Assets/slowburn_art.png, black background
// turned into alpha) printed on the left, its own "SLOW BURN" lettering = the name; a 12AX7 standing upright in
// a chrome-framed window on the right, heaters glowing (brighter when the pedal is on); cream labels, burnt-orange
// "12AX7 TUBE DRIVE" under the window. Six knobs in two rows (Gain/Presence/Volume big, Bass/Middle/Treble
// small), DEEP mini toggle left of the footswitch, LED right. Rendered images (Tools/Blender/render_slowburn.py)
// add slowburn_tube_on/off (the valve window, glow baked) and slowburn_toggle_up/down.
namespace
{
    const juce::Colour glossDeep { 0xff030304 }, sbCream { 0xffebcea0 }, sbOrange { 0xffd04217 };
}

class SlowBurnFace : public RenderedFace
{
public:
    static constexpr float tubePad = 1.10f, togglePad = 1.30f;    // = render_slowburn.py

    SlowBurnFace() : RenderedFace("slowburn")
    {
        art = juce::ImageCache::getFromMemory(BinaryData::slowburn_art_png, BinaryData::slowburn_art_pngSize);
        tubeOn = loadImage("slowburn_tube_on_png");   tubeOff = loadImage("slowburn_tube_off_png");
        toggleUp = loadImage("slowburn_toggle_up_png"); toggleDn = loadImage("slowburn_toggle_down_png");
    }
    juce::Colour labelColour() const override { return sbCream; }
    juce::Rectangle<float> toggleBounds(juce::Rectangle<float> r) const override
    {
        return rendered() && toggleUp.isValid() ? r.withSizeKeepingCentre(r.getWidth() * togglePad, r.getHeight() * togglePad) : r;
    }
    const juce::Image* toggleImage(bool up) const override
    {
        if (! rendered()) return nullptr;
        auto& img = up ? toggleUp : toggleDn;
        return img.isValid() ? &img : nullptr;
    }
    void paint(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        RenderedFace::paint(g, f, active);
        if (rendered())
            if (auto& img = active ? tubeOn : tubeOff; img.isValid())
            {
                g.setOpacity(1.0f);
                g.drawImage(img, f.window.withSizeKeepingCentre(f.window.getWidth() * tubePad, f.window.getHeight() * tubePad));
            }
    }

    // The valve in its window, lying left-right in w (pins left, getter dome right). Also used rotated.
    static void drawTubeWindow(juce::Graphics& g, juce::Rectangle<float> w, bool active)
    {
        using namespace juce;
        const float cr = w.getHeight() * 0.22f;
        // chrome bezel
        g.setColour(Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(w.translated(0.0f, 2.0f), cr);
        g.setGradientFill(ColourGradient(Colour(0xfff2f3f5), w.getX(), w.getY(), Colour(0xff6c7075), w.getRight(), w.getBottom(), false));
        g.fillRoundedRectangle(w, cr);
        auto in = w.reduced(w.getHeight() * 0.075f);
        g.setGradientFill(ColourGradient(Colour(0xff050506), in.getX(), in.getY(), Colour(0xff1a1410), in.getX(), in.getBottom(), false));
        g.fillRoundedRectangle(in, cr * 0.7f);

        // glass envelope, pins to the left, getter flash at the right end
        auto glass = in.reduced(in.getWidth() * 0.07f, in.getHeight() * 0.17f).translated(in.getWidth() * 0.02f, 0.0f);
        const float gr = glass.getHeight() * 0.5f;
        const float glow = active ? 1.0f : 0.45f;
        for (int k = 0; k < 2; ++k)                                           // warm light inside the glass
        {
            const auto c = Point<float>(glass.getX() + glass.getWidth() * (k == 0 ? 0.36f : 0.62f), glass.getCentreY());
            const float R = glass.getHeight() * 0.95f;
            g.setGradientFill(ColourGradient(Colour(0xffff7a2a).withAlpha(0.55f * glow), c.x, c.y, Colour(0x00ff5a00), c.x + R, c.y, true));
            g.fillEllipse(c.x - R, c.y - R, R * 2, R * 2);
        }
        g.setColour(Colour(0xff2c2622).withAlpha(0.55f));
        g.fillRoundedRectangle(glass, gr);
        const float pw = glass.getWidth() * 0.20f, ph = glass.getHeight() * 0.62f;  // plates (two triodes) + heaters
        for (int k = 0; k < 2; ++k)
        {
            const float cx = glass.getX() + glass.getWidth() * (k == 0 ? 0.36f : 0.62f);
            auto plate = Rectangle<float>(pw, ph).withCentre({ cx, glass.getCentreY() });
            g.setGradientFill(ColourGradient(Colour(0xff4a4644), plate.getX(), plate.getY(), Colour(0xff1a1918), plate.getX(), plate.getBottom(), false));
            g.fillRect(plate);
            g.setColour(Colour(0xff0e0d0c));
            for (int i = 1; i < 3; ++i) g.drawVerticalLine((int) (plate.getX() + plate.getWidth() * (float) i / 3.0f), plate.getY(), plate.getBottom());
            auto slit = plate.withSizeKeepingCentre(plate.getWidth() * 0.85f, plate.getHeight() * 0.16f);
            g.setColour(Colour(0xffff6a1a).withAlpha(0.95f * glow));
            g.fillRoundedRectangle(slit, slit.getHeight() * 0.5f);
            g.setColour(Colour(0xffffc070).withAlpha(0.8f * glow));
            g.fillRoundedRectangle(slit.reduced(slit.getWidth() * 0.15f, slit.getHeight() * 0.3f), 1.0f);
        }
        g.setColour(Colour(0xffd9d2c4).withAlpha(0.45f));                       // mica spacers
        for (float fx : { 0.22f, 0.49f, 0.76f })
            g.fillRect(Rectangle<float>(1.4f, glass.getHeight() * 0.86f).withCentre({ glass.getX() + glass.getWidth() * fx, glass.getCentreY() }));
        auto dome = glass.withLeft(glass.getRight() - glass.getHeight() * 1.1f);   // silvered getter end
        g.setGradientFill(ColourGradient(Colour(0xffd8dadc).withAlpha(0.95f), dome.getRight(), dome.getCentreY(),
                                         Colour(0x00a0a4a8), dome.getX(), dome.getCentreY(), false));
        g.fillRoundedRectangle(dome, gr);
        auto sock = Rectangle<float>(in.getWidth() * 0.075f, glass.getHeight() * 1.12f).withCentre({ glass.getX() - in.getWidth() * 0.02f, glass.getCentreY() });
        g.setGradientFill(ColourGradient(Colour(0xff3a3530), sock.getX(), sock.getY(), Colour(0xff141210), sock.getRight(), sock.getBottom(), false));
        g.fillRoundedRectangle(sock, 2.0f);
        g.setColour(Colours::white.withAlpha(0.35f));
        g.drawRoundedRectangle(glass, gr, 1.0f);
        g.setColour(Colours::white.withAlpha(0.06f));
        g.fillRoundedRectangle(in.withHeight(in.getHeight() * 0.42f), cr * 0.7f);
    }
    // Upright: base at the bottom, getter dome at the top.
    static void drawUprightTube(juce::Graphics& g, juce::Rectangle<float> w, bool active)
    {
        const auto c = w.getCentre();
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, c.x, c.y));
        drawTubeWindow(g, juce::Rectangle<float>(w.getHeight(), w.getWidth()).withCentre(c), active);
        g.restoreState();
    }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(Colour(0xff1a1a1d), b.getX(), b.getY(), glossDeep, b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        {   // gloss: a soft diagonal reflection band
            Path band;
            band.startNewSubPath(b.getX(), b.getY() + b.getHeight() * 0.10f);
            band.lineTo(b.getX() + b.getWidth() * 0.55f, b.getY());
            band.lineTo(b.getX() + b.getWidth() * 0.80f, b.getY());
            band.lineTo(b.getX(), b.getY() + b.getHeight() * 0.36f);
            band.closeSubPath();
            g.saveState();
            Path clip; clip.addRoundedRectangle(b, 9.0f);
            g.reduceClipRegion(clip);
            g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.07f), b.getX(), b.getY(), Colours::white.withAlpha(0.0f),
                                             b.getX() + b.getWidth() * 0.4f, b.getY() + b.getHeight() * 0.3f, false));
            g.fillPath(band);
            g.restoreState();
        }
        g.setColour(Colours::white.withAlpha(0.14f));
        g.drawRoundedRectangle(b.reduced(0.75f), 9.0f, 1.2f);
        const float sr = 4.0f, inset = 9.0f;
        drawScrew(g, { b.getX() + inset, b.getY() + inset }, sr, 0.3f);
        drawScrew(g, { b.getRight() - inset, b.getY() + inset }, sr, 1.0f);
        drawScrew(g, { b.getX() + inset, b.getBottom() - inset }, sr, 0.6f);
        drawScrew(g, { b.getRight() - inset, b.getBottom() - inset }, sr, 1.4f);

        if (art.isValid())
        {
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.setOpacity(1.0f);
            g.drawImage(art, f.emblem);
        }
        drawUprightTube(g, f.window, active);

        g.setColour(sbOrange);
        g.setFont(Font(FontOptions(f.subtitle.getHeight() * 0.80f, Font::bold).withKerningFactor(0.18f)));
        g.drawFittedText("12AX7 TUBE DRIVE", f.subtitle.toNearestInt(), Justification::centred, 1, 0.8f);
        if (f.hasToggle)
        {
            g.setColour(sbCream);
            g.setFont(Font(FontOptions(f.toggleLabel.getHeight() * 0.80f, Font::bold).withKerningFactor(0.2f)));
            g.drawText("DEEP", f.toggleLabel, Justification::centred);
        }
        drawLed(g, f.led, active);
    }

private:
    juce::Image art, tubeOn, tubeOff, toggleUp, toggleDn;
};

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Gatekeeper (noise gate, based on the Boss NF-1): Michael's full-face artwork (Assets/gatekeeper_art.png:
// the enthroned gatekeeper, the iron gate, two hounds, "GATEKEEPER / NOISE GATE", THRESHOLD / RELEASE,
// HARD / SOFT) printed edge to edge on a worn black enclosure. Knobs, LED, footswitch and the HARD/SOFT
// toggle sit exactly over the ones painted in the art (FaceLayout: 2 knobs + toggle).
class GatekeeperFace : public RenderedFace
{
public:
    static constexpr float togglePad = 1.30f;    // = render_gatekeeper.py

    GatekeeperFace() : RenderedFace("gatekeeper")
    {
        art = juce::ImageCache::getFromMemory(BinaryData::gatekeeper_art_png, BinaryData::gatekeeper_art_pngSize);
        toggleUp = loadImage("gatekeeper_toggle_up_png"); toggleDn = loadImage("gatekeeper_toggle_down_png");
    }
    bool drawsLabels() const override { return true; }                 // all lettering is in the artwork
    juce::Colour labelColour() const override { return juce::Colour(0xffcecbc6); }
    juce::Rectangle<float> toggleBounds(juce::Rectangle<float> r) const override
    {
        return rendered() && toggleUp.isValid() ? r.withSizeKeepingCentre(r.getWidth() * togglePad, r.getHeight() * togglePad) : r;
    }
    const juce::Image* toggleImage(bool up) const override
    {
        if (! rendered()) return nullptr;
        auto& img = up ? toggleUp : toggleDn;
        return img.isValid() ? &img : nullptr;
    }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(Colour(0xff2a2a2c), b.getX(), b.getY(), Colour(0xff0b0b0c), b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        if (art.isValid())
        {
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.setOpacity(1.0f);
            g.drawImage(art, f.emblem);
        }
        drawLed(g, f.led, active);
    }

private:
    juce::Image art, toggleUp, toggleDn;
};

// ---------------------------------------------------------------------------
// Thunderhead (based on the Diezel VH4 pedal): Michael's full-face artwork (Assets/thunderhead_art.png: the storm
// face over the mountains, "THUNDERHEAD", "DISTORTION LIKE A STORM", every control label) printed edge to edge on a
// worn black enclosure. All seven knobs, the LED, the footswitch and the AMP/DIRECT toggle sit where
// Tools/Art/make_thunderhead_art.py placed them (FaceLayout: 7 knobs + toggle).
class ThunderheadFace : public RenderedFace
{
public:
    static constexpr float togglePad = 1.30f;    // = render_thunderhead.py

    ThunderheadFace() : RenderedFace("thunderhead")
    {
        art = juce::ImageCache::getFromMemory(BinaryData::thunderhead_art_png, BinaryData::thunderhead_art_pngSize);
        toggleUp = loadImage("thunderhead_toggle_up_png"); toggleDn = loadImage("thunderhead_toggle_down_png");
    }
    bool drawsLabels() const override { return true; }                 // all lettering is in the artwork
    juce::Colour labelColour() const override { return juce::Colour(0xffdfe3e8); }
    juce::Rectangle<float> toggleBounds(juce::Rectangle<float> r) const override
    {
        return rendered() && toggleUp.isValid() ? r.withSizeKeepingCentre(r.getWidth() * togglePad, r.getHeight() * togglePad) : r;
    }
    const juce::Image* toggleImage(bool up) const override
    {
        if (! rendered()) return nullptr;
        auto& img = up ? toggleUp : toggleDn;
        return img.isValid() ? &img : nullptr;
    }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(Colour(0xff2a2b2e), b.getX(), b.getY(), Colour(0xff0b0b0c), b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        if (art.isValid())
        {
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.setOpacity(1.0f);
            g.drawImage(art, f.emblem);
        }
        drawLed(g, f.led, active);
    }

private:
    juce::Image art, toggleUp, toggleDn;
};

// ---------------------------------------------------------------------------
// Golden Haze (based on the Earthquake Devices Acapulco Gold): Michael's full-face artwork (Assets/goldenhaze_art.png:
// the golden sun and swirling haze over the mountains, "Golden Haze", VOLUME, "POWER AMP - DISTORTION - HIGHER
// GROUND") printed edge to edge on a worn gold enclosure. The VOLUME knob, LED and footswitch sit where
// Tools/Art/make_goldenhaze_art.py left the painted ones (FaceLayout: 1 knob).
class GoldenHazeFace : public RenderedFace
{
public:
    GoldenHazeFace() : RenderedFace("goldenhaze")
    {
        art = juce::ImageCache::getFromMemory(BinaryData::goldenhaze_art_png, BinaryData::goldenhaze_art_pngSize);
    }
    bool drawsLabels() const override { return true; }                 // all lettering is in the artwork
    juce::Colour labelColour() const override { return juce::Colour(0xffdbae5b); }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(Colour(0xffb08a3a), b.getX(), b.getY(), Colour(0xff4a3510), b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        if (art.isValid())
        {
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.setOpacity(1.0f);
            g.drawImage(art, f.emblem);
        }
        drawLed(g, f.led, active);
    }

private:
    juce::Image art;
};

// ---------------------------------------------------------------------------
// Night Courier (based on the EarthQuaker Devices Dispatch Master): Michael's full-face artwork
// (Assets/nightcourier_art.png: the hooded courier with his satchel above the moonlit river city, every control
// label, "NIGHT COURIER", "DELAY / REVERB", "SOUND TRAVELS FURTHER AT NIGHT") on a matte black enclosure. Six
// knobs, the 3-way DELAY / REVERB / BOTH toggle, the BYPASS footswitch and LED sit on the painted ones
// (FaceLayout: pedalId "nc").
class NightCourierFace : public RenderedFace
{
public:
    static constexpr float togglePad = 1.30f;    // = render_nightcourier.py

    NightCourierFace() : RenderedFace("nightcourier")
    {
        art = juce::ImageCache::getFromMemory(BinaryData::nightcourier_art_png, BinaryData::nightcourier_art_pngSize);
        toggle[0] = loadImage("nightcourier_toggle_up_png");
        toggle[1] = loadImage("nightcourier_toggle_centre_png");
        toggle[2] = loadImage("nightcourier_toggle_down_png");
    }
    bool drawsLabels() const override { return true; }
    juce::Colour labelColour() const override { return juce::Colour(0xffe8e2d4); }
    juce::Rectangle<float> toggleBounds(juce::Rectangle<float> r) const override
    {
        return rendered() && toggle[0].isValid() ? r.withSizeKeepingCentre(r.getWidth() * togglePad, r.getHeight() * togglePad) : r;
    }
    const juce::Image* toggleImage3(int pos) const override
    {
        if (! rendered()) return nullptr;
        auto& img = toggle[(size_t) juce::jlimit(0, 2, pos)];
        return img.isValid() ? &img : nullptr;
    }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(Colour(0xff26282b), b.getX(), b.getY(), Colour(0xff0c0c0d), b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        if (art.isValid())
        {
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.setOpacity(1.0f);
            g.drawImage(art, f.emblem);
        }
        drawLed(g, f.led, active);
    }

private:
    juce::Image art;
    std::array<juce::Image, 3> toggle;
};

// ---------------------------------------------------------------------------
// Coral Tide (Bearfoot / BJFE Sea Blue EQ + Baby Pink Booster, double-wide 464 x 372): Michael's full-face artwork
// (Assets/coraltide_art.png from Tools/Art/make_coraltide_art.py: coral reef, sea and sunset, "Coral Tide", BOOST /
// LEVEL / ORDER / LOW / HIGH, "TONE FLOWS FURTHER", BOOST / EQ under the switches) on a cream enclosure. Knobs, toggle,
// switches and LEDs sit on the painted ones (FaceLayout: pedalId "ct"). Main (EQ) LED blue = <prefix>_led_*,
// BOOST LED red = <prefix>_led2_* (render_coraltide.py).
class CoralTideFace : public RenderedFace
{
public:
    static constexpr float togglePad = 1.30f;    // = render_coraltide.py

    CoralTideFace() : RenderedFace("coraltide", 2)
    {
        art = juce::ImageCache::getFromMemory(BinaryData::coraltide_art_png, BinaryData::coraltide_art_pngSize);
        toggleUp = loadImage("coraltide_toggle_up_png");
        toggleDn = loadImage("coraltide_toggle_down_png");
    }
    bool drawsLabels() const override { return true; }                 // all lettering is in the artwork
    juce::Colour labelColour() const override { return juce::Colour(0xff1d4e6b); }
    juce::Rectangle<float> toggleBounds(juce::Rectangle<float> r) const override
    {
        return rendered() && toggleUp.isValid() ? r.withSizeKeepingCentre(r.getWidth() * togglePad, r.getHeight() * togglePad) : r;
    }
    const juce::Image* toggleImage(bool up) const override
    {
        if (! rendered()) return nullptr;
        auto& img = up ? toggleUp : toggleDn;
        return img.isValid() ? &img : nullptr;
    }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(Colour(0xfff1e7d6), b.getX(), b.getY(), Colour(0xffd9ccb6), b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        if (art.isValid())
        {
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.setOpacity(1.0f);
            g.drawImage(art, f.emblem);
        }
        drawLed(g, f.led, active, true);                               // EQ: blue
        drawLed(g, f.led2, f.secondaryOn);                             // BOOST: red
    }

private:
    juce::Image art, toggleUp, toggleDn;
};

// ---------------------------------------------------------------------------
// Frostbite (based on the Artec Cool Drive CDV-1): Michael's full-face artwork (Assets/frostbite_art.png from
// Tools/Art/make_frostbite_art.py: frost-covered blue-grey top, GAIN / PRE / MOD / POST / LEVEL, "FROSTBITE" in icicle
// lettering over a frozen landscape) on a frosted blue-grey enclosure. Black knobs, the chrome MOD bat toggle, the
// blue LED and the footswitch sit on the painted ones (FaceLayout: pedalId "fb").
class FrostbiteFace : public RenderedFace
{
public:
    static constexpr float togglePad = 1.30f;    // = render_frostbite.py

    FrostbiteFace() : RenderedFace("frostbite")
    {
        art = juce::ImageCache::getFromMemory(BinaryData::frostbite_art_png, BinaryData::frostbite_art_pngSize);
        toggleUp = loadImage("frostbite_toggle_up_png");
        toggleDn = loadImage("frostbite_toggle_down_png");
    }
    bool drawsLabels() const override { return true; }                 // all lettering is in the artwork
    juce::Colour labelColour() const override { return juce::Colour(0xffdfe6ee); }
    juce::Rectangle<float> toggleBounds(juce::Rectangle<float> r) const override
    {
        return rendered() && toggleUp.isValid() ? r.withSizeKeepingCentre(r.getWidth() * togglePad, r.getHeight() * togglePad) : r;
    }
    const juce::Image* toggleImage(bool up) const override
    {
        if (! rendered()) return nullptr;
        auto& img = up ? toggleUp : toggleDn;
        return img.isValid() ? &img : nullptr;
    }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(Colour(0xff8a9bb0), b.getX(), b.getY(), Colour(0xff3a4a60), b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        if (art.isValid())
        {
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.setOpacity(1.0f);
            g.drawImage(art, f.emblem);
        }
        drawLed(g, f.led, active, true);                               // blue, like the art
    }

private:
    juce::Image art, toggleUp, toggleDn;
};

// ---------------------------------------------------------------------------
// Twin Vise (based on the Trace Elliot SMX Dual Compressor), double-wide: Michael's full-face artwork
// (Assets/twinvise_art.png from Tools/Art/make_twinvise_art.py: rough gunmetal top, printed orange knob arcs and labels,
// "TWIN VISE" on a rusty bench vise) on a gunmetal enclosure. Knobs, the three LEDs (HI COMP / EFFECT red, INPUT
// bicolour green / red) and both footswitches sit on the painted ones (FaceLayout: pedalId "tv").
class TwinViseFace : public RenderedFace
{
public:
    TwinViseFace() : RenderedFace("twinvise", 2)
    {
        art = juce::ImageCache::getFromMemory(BinaryData::twinvise_art_png, BinaryData::twinvise_art_pngSize);
        meterGreen = loadImage("twinvise_meter_green_png");          // optional rendered meter LED states
        meterRed   = loadImage("twinvise_meter_red_png");
        meterOff   = loadImage("twinvise_meter_off_png");
    }
    bool drawsLabels() const override { return true; }                 // all lettering is in the artwork
    juce::Colour labelColour() const override { return juce::Colour(0xffd8d8d4); }

    void paint(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        RenderedFace::paint(g, f, active);
        // the INPUT meter LED is drawn over either face (rendered images if present, else vector)
        if (rendered() && meterOff.isValid() && meterGreen.isValid() && meterRed.isValid())
        {
            const auto l = f.meterLed.withSizeKeepingCentre(f.meterLed.getWidth() * ledPad, f.meterLed.getHeight() * ledPad);
            g.drawImage(f.meter >= 2 ? meterRed : f.meter == 1 ? meterGreen : meterOff, l);
        }
        else
            drawMeterLed(g, f.meterLed, f.meter);
    }

protected:
    void paintVector(juce::Graphics& g, const FaceLayout& f, bool active) override
    {
        using namespace juce;
        const auto b = f.body;
        g.setColour(Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(b.translated(0, 3), 9.0f);
        g.setGradientFill(ColourGradient(Colour(0xff5a5c5e), b.getX(), b.getY(), Colour(0xff26282a), b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 9.0f);
        if (art.isValid())
        {
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.setOpacity(1.0f);
            g.drawImage(art, f.emblem);
        }
        drawLed(g, f.led, active);                                     // EFFECT: red
        drawLed(g, f.led2, f.secondaryOn);                             // HI COMP: red
    }

private:
    juce::Image art, meterGreen, meterRed, meterOff;

    // bicolour level LED: off / green / red
    static void drawMeterLed(juce::Graphics& g, juce::Rectangle<float> l, int state)
    {
        using namespace juce;
        g.setColour(Colour(0xff6f7378)); g.fillEllipse(l.expanded(2.5f));
        g.setColour(Colour(0xff2a2c2f)); g.fillEllipse(l.expanded(1.0f));
        if (state > 0)
        {
            const bool red = state >= 2;
            const Colour glow = red ? Colour(0xffff2a1a) : Colour(0xff34e04a);
            g.setColour(glow.withAlpha(0.25f)); g.fillEllipse(l.expanded(6.0f));
            g.setGradientFill(ColourGradient(red ? Colour(0xffffd0c0) : Colour(0xffd8ffd0), l.getCentreX() - 1, l.getCentreY() - 2,
                                             red ? Colour(0xffe01a0e) : Colour(0xff13a828), l.getRight(), l.getBottom(), true));
        }
        else
            g.setColour(Colour(0xff2a2a1c));
        g.fillEllipse(l);
    }
};

std::unique_ptr<PedalFace> createFaceFor(const juce::String& pedalId)
{
    if (pedalId == "bm") return std::make_unique<RamhornFace>();
    if (pedalId == "hm") return std::make_unique<BuzzsawFace>();
    if (pedalId == "rb") return std::make_unique<RailBenderFace>();
    if (pedalId == "ts") return std::make_unique<ValveHowlerFace>();
    if (pedalId == "ad") return std::make_unique<FeralHoundFace>();
    if (pedalId == "sb") return std::make_unique<SlowBurnFace>();
    if (pedalId == "ng") return std::make_unique<GatekeeperFace>();
    if (pedalId == "th") return std::make_unique<ThunderheadFace>();
    if (pedalId == "gh") return std::make_unique<GoldenHazeFace>();
    if (pedalId == "nc") return std::make_unique<NightCourierFace>();
    if (pedalId == "ct") return std::make_unique<CoralTideFace>();
    if (pedalId == "fb") return std::make_unique<FrostbiteFace>();
    if (pedalId == "tv") return std::make_unique<TwinViseFace>();
    return nullptr;
}
