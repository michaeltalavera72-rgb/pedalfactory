#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Where everything sits on a stompbox face. Computed from the body rectangle so the
// face painter (graphics) and PedalStrip::resized (knob/footswitch components) agree.
struct FaceLayout
{
    // Pedal body proportions (226 x 372 px since v28; was 226 x 298). Blender faces are rendered at this shape;
    // a face image with a different shape is ignored (the vector face is shown) until it is re-rendered.
    static constexpr float bodyAspect = 372.0f / 226.0f;
    // Double-wide pedals (PedalBase::widthUnits() == 2) span two slots plus the gap between them: body 464 x 372.
    // Body width for n units = 238 n - 12 px (slot 230 + gap 8, body = slot - 4).
    static constexpr float bodyWidthFor(int units) { return 238.0f * (float) units - 12.0f; }
    static constexpr float aspectFor(int units) { return 372.0f / bodyWidthFor(units); }
    // Gatekeeper's printed artwork is inset 2.6 mm on the 91 mm-wide body.
    static constexpr float artInset = 2.6f / 91.0f;
    static constexpr float artInset2 = 2.6f * 226.0f / (91.0f * 464.0f);   // the same 2.6 mm on the double-wide body (of W)

    juce::Rectangle<float> body;               // the whole enclosure
    juce::Array<juce::Rectangle<float>> knobs; // one square per knob, left to right
    juce::Array<juce::Rectangle<float>> labels;// text under each knob
    juce::Rectangle<float> name, subtitle, emblem, led, footswitch;
    // Pedals with two footswitches (e.g. drive + boost): switches at 29% / 71% of the width,
    // an LED above each and a small printed label above each LED. Single-switch layout unchanged.
    bool twoSwitches = false;
    juce::Rectangle<float> led2, footswitch2, switchLabel1, switchLabel2;
    bool secondaryOn = false;            // state of the second LED, filled in by the strip
    // Optional bicolour level LED (PedalBase::ledMeter): -1 none, 0 off, 1 green, 2 red; filled in by the strip.
    int meter = -1;
    juce::Rectangle<float> meterLed, meterLabel;
    // Optional mini toggle (PedalBase::toggleId), with its printed label above it.
    bool hasToggle = false;
    juce::Rectangle<float> toggle, toggleLabel;
    juce::Rectangle<float> window;           // optional second artwork area (Slow Burn: the valve window)

    // pedalId: for layouts fitted to one pedal's artwork that the knob/switch counts alone can't tell apart.
    static FaceLayout make(juce::Rectangle<float> body, int numKnobs, int numSwitches = 1, bool withToggle = false,
                           const juce::String& pedalId = {})
    {
        FaceLayout f; f.body = body;
        const float W = body.getWidth(), H = body.getHeight(), x0 = body.getX(), y0 = body.getY();
        // 1-3 knobs: big knobs spread over 19%..81% of the width (Ramhorn renders depend on this).
        // 4+ knobs: smaller knobs spread over 15%..85%.
        const bool wide = numKnobs >= 4;
        const float d = W * (wide ? 0.19f : 0.255f);         // knob diameter
        const float span0 = wide ? 0.15f : 0.19f, span = wide ? 0.70f : 0.62f;
        const float knobTop = y0 + H * 0.055f;
        for (int i = 0; i < numKnobs; ++i)
        {
            const float cx = x0 + W * (numKnobs == 1 ? 0.5f : span0 + span * (float) i / (float) (numKnobs - 1));
            f.knobs.add({ cx - d * 0.5f, knobTop, d, d });
            f.labels.add({ cx - d * 0.75f, knobTop + d + 3.0f, d * 1.5f, 13.0f });
        }
        f.name     = { x0 + W * 0.06f, y0 + H * 0.355f, W * 0.88f, H * 0.135f };
        f.subtitle = { x0 + W * 0.06f, y0 + H * 0.49f,  W * 0.88f, H * 0.065f };
        f.emblem   = { x0 + W * 0.26f, y0 + H * 0.565f, W * 0.48f, H * 0.135f };
        const float sw = W * 0.19f;
        f.footswitch = { x0 + (W - sw) * 0.5f, y0 + H * 0.865f - sw * 0.5f, sw, sw };
        f.led = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ x0 + W * 0.5f, y0 + H * 0.74f });
        if (numKnobs == 6)
        {
            // Two rows of three: big knobs on top, small knobs below (like the 16 mm / 9 mm pots of the
            // real thing). Below them (Feral Hound): a big square artwork as the "emblem" with the name
            // printed across its top, the footswitch a little lower, the LED to its right and the
            // subtitle to its left.
            f.knobs.clear(); f.labels.clear();
            const float d1 = W * 0.215f, d2 = W * 0.15f, top1 = y0 + H * 0.045f, top2 = y0 + H * 0.262f;
            for (int i = 0; i < 6; ++i)
            {
                const bool big = i < 3;
                const float d = big ? d1 : d2, top = big ? top1 : top2;
                const float cx = x0 + W * (0.22f + 0.28f * (float) (i % 3));
                f.knobs.add({ cx - d * 0.5f, top, d, d });
                f.labels.add({ cx - W * 0.14f, top + d + 1.0f, W * 0.28f, 13.0f });
            }
            f.name     = { x0 + W * 0.06f, y0 + H * 0.425f, W * 0.88f, H * 0.10f };
            f.emblem   = juce::Rectangle<float>(W * 0.66f, W * 0.66f).withCentre({ x0 + W * 0.5f, y0 + H * 0.64f });
            f.footswitch = juce::Rectangle<float>(sw, sw).withCentre({ x0 + W * 0.5f, y0 + H * 0.905f });
            f.led      = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ x0 + W * 0.80f, y0 + H * 0.905f });
            f.subtitle = juce::Rectangle<float>(W * 0.30f, H * 0.04f).withCentre({ x0 + W * 0.205f, y0 + H * 0.905f });
        }
        if (numKnobs == 6 && withToggle)
        {
            // Slow Burn: same two knob rows; below them the printed artwork (left, 2:3, carries the name) and
            // the valve standing upright in a tall window (right), the subtitle under the window, and a bottom
            // row of DEEP toggle / footswitch / LED.
            f.emblem   = juce::Rectangle<float>(W * 0.40f, W * 0.60f).withCentre({ x0 + W * 0.29f, y0 + H * 0.60f });   // 2:3 art
            f.name     = f.emblem;                                       // the artwork's lettering is the name
            f.window   = juce::Rectangle<float>(W * 0.22f, W * 0.56f).withCentre({ x0 + W * 0.73f, y0 + H * 0.60f });
            f.subtitle = juce::Rectangle<float>(W * 0.40f, H * 0.030f).withCentre({ x0 + W * 0.73f, y0 + H * 0.795f });
            f.footswitch = juce::Rectangle<float>(sw, sw).withCentre({ x0 + W * 0.5f, y0 + H * 0.895f });
            f.led      = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ x0 + W * 0.80f, y0 + H * 0.895f });
            f.hasToggle = true;
            f.toggle      = juce::Rectangle<float>(W * 0.10f, W * 0.16f).withCentre({ x0 + W * 0.20f, y0 + H * 0.905f });
            f.toggleLabel = juce::Rectangle<float>(W * 0.24f, H * 0.032f).withCentre({ x0 + W * 0.20f, y0 + H * 0.812f });
        }
        if (numKnobs == 2 && withToggle)
        {
            // Gatekeeper: Michael's full-face artwork, printed 2.6 mm (artInset of W) inside the enclosure edge.
            // Every control sits exactly over the one painted in the art (= render_gatekeeper.py / make_gatekeeper_art.py).
            f.knobs.clear(); f.labels.clear();
            const float dk = W * 0.235f;
            for (float cxf : { 0.1787f, 0.8246f })
            {
                const auto k = juce::Rectangle<float>(dk, dk).withCentre({ x0 + W * cxf, y0 + H * 0.1223f });
                f.knobs.add(k);
                f.labels.add({ k.getX(), k.getBottom(), dk, 13.0f });      // printed in the art; unused
            }
            f.emblem   = body.reduced(W * artInset);
            f.name     = f.emblem;                                         // the artwork carries name + labels
            f.subtitle = {};
            f.led      = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ x0 + W * 0.50f, y0 + H * 0.7217f });
            f.footswitch = juce::Rectangle<float>(sw, sw).withCentre({ x0 + W * 0.50f, y0 + H * 0.8148f });
            f.hasToggle = true;
            f.toggle      = juce::Rectangle<float>(W * 0.10f, W * 0.16f).withCentre({ x0 + W * 0.23f, y0 + H * 0.8148f });
            f.toggleLabel = juce::Rectangle<float>(W * 0.20f, H * 0.03f).withCentre({ x0 + W * 0.23f, y0 + H * 0.752f });
        }
        if (numKnobs == 7)
        {
            // Thunderhead: Michael's full-face artwork (Assets/thunderhead_art.png), printed artInset inside the edge
            // like Gatekeeper. Big knobs GAIN / DEEP / VOLUME sit on the art's three painted knobs; the small EQ knobs
            // flank the storm face (BASS / MIDDLE left, TREBLE / PRESENCE right); LED and footswitch on the painted
            // ones; AMP/DIRECT toggle bottom-left. Fractions from Tools/Art/make_thunderhead_art.py (= render_thunderhead.py).
            f.knobs.clear(); f.labels.clear();
            const float d1 = W * 0.23f, d2 = W * 0.13f;
            const float pos[7][2] = { { 0.1900f, 0.1584f }, { 0.5022f, 0.1584f }, { 0.8143f, 0.1584f },
                                      { 0.1039f, 0.3149f }, { 0.2632f, 0.3149f }, { 0.7389f, 0.3149f }, { 0.8961f, 0.3149f } };
            for (int i = 0; i < 7; ++i)
            {
                const float d = i < 3 ? d1 : d2;
                const auto k = juce::Rectangle<float>(d, d).withCentre({ x0 + W * pos[i][0], y0 + H * pos[i][1] });
                f.knobs.add(k);
                f.labels.add({ k.getX(), k.getBottom(), d, 13.0f });      // printed in the art; unused
            }
            f.emblem   = body.reduced(W * artInset);
            f.name     = f.emblem;                                         // the artwork carries name + labels
            f.subtitle = {};
            f.led      = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ x0 + W * 0.5011f, y0 + H * 0.7424f });
            f.footswitch = juce::Rectangle<float>(sw, sw).withCentre({ x0 + W * 0.4978f, y0 + H * 0.8513f });
            f.hasToggle = withToggle;
            f.toggle      = juce::Rectangle<float>(W * 0.10f, W * 0.16f).withCentre({ x0 + W * 0.2202f, y0 + H * 0.8485f });
            f.toggleLabel = {};                                            // AMP / DIRECT printed in the art
        }
        if (numKnobs == 1)
        {
            // Golden Haze: Michael's full-face artwork (Assets/goldenhaze_art.png), printed artInset inside the edge like
            // Thunderhead. VOLUME sits on the art's painted centre knob (the template's two other knobs were painted out),
            // LED right of the footswitch as painted. Fractions from Tools/Art/make_goldenhaze_art.py (= render_goldenhaze.py).
            // (Only one-knob pedal so far; give the next one its own flag if it needs a different layout.)
            f.knobs.clear(); f.labels.clear();
            const float dk = W * 0.2261f;
            const auto k = juce::Rectangle<float>(dk, dk).withCentre({ x0 + W * 0.5000f, y0 + H * 0.1573f });
            f.knobs.add(k);
            f.labels.add({ k.getX(), k.getBottom(), dk, 13.0f });          // printed in the art; unused
            f.emblem   = body.reduced(W * artInset);
            f.name     = f.emblem;                                         // the artwork carries name + labels
            f.subtitle = {};
            f.led      = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ x0 + W * 0.6963f, y0 + H * 0.8413f });
            f.footswitch = juce::Rectangle<float>(sw, sw).withCentre({ x0 + W * 0.4979f, y0 + H * 0.8385f });
        }
        if (pedalId == "nc")
        {
            // Night Courier: Michael's full-face artwork (Assets/nightcourier_art.png), printed artInset inside the
            // edge. TIME / REPEATS / MIX / REVERB on the top row, MOD / TONE below, the DELAY / REVERB / BOTH 3-way
            // toggle between them, BYPASS footswitch + LED bottom-left - all on the controls painted in the art
            // (fractions from Tools/Art/make_nightcourier_art.py = render_nightcourier.py). The art's second
            // (PRESET) footswitch was painted out.
            f.knobs.clear(); f.labels.clear();
            const float d1 = W * 0.1537f, d2 = W * 0.1353f;
            const float pos[6][2] = { { 0.1546f, 0.1837f }, { 0.3801f, 0.1825f }, { 0.6209f, 0.1825f }, { 0.8515f, 0.1825f },
                                      { 0.2366f, 0.3456f }, { 0.7644f, 0.3456f } };
            for (int i = 0; i < 6; ++i)
            {
                const float d = i < 4 ? d1 : d2;
                const auto k = juce::Rectangle<float>(d, d).withCentre({ x0 + W * pos[i][0], y0 + H * pos[i][1] });
                f.knobs.add(k);
                f.labels.add({ k.getX(), k.getBottom(), d, 13.0f });      // printed in the art; unused
            }
            f.emblem   = body.reduced(W * artInset);
            f.name     = f.emblem;
            f.subtitle = {};
            f.led      = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ x0 + W * 0.1618f, y0 + H * 0.7553f });
            const float swn = W * 0.165f;                                   // smaller than standard: BYPASS is printed just below
            f.footswitch = juce::Rectangle<float>(swn, swn).withCentre({ x0 + W * 0.1598f, y0 + H * 0.8332f });
            f.hasToggle = withToggle;
            f.toggle      = juce::Rectangle<float>(W * 0.10f, W * 0.16f).withCentre({ x0 + W * 0.5010f, y0 + H * 0.3817f });
            f.toggleLabel = {};
            return f;
        }
        if (pedalId == "ct")
        {
            // Coral Tide (double-wide, 464 x 372): Michael's full-face artwork (Assets/coraltide_art.png), printed artInset2
            // inside the edge. BOOST / LEVEL knobs left, ORDER toggle between, LOW / HIGH right; footswitches BOOST (left,
            // red LED = the secondary switch) and EQ (right, blue LED = the main switch). Every control sits where the
            // art's painted one was (fractions printed by Tools/Art/make_coraltide_art.py = render_coraltide.py).
            const float d = W * 0.1280f;
            const float pos[4][2] = { { 0.1458f, 0.2207f }, { 0.3440f, 0.2194f }, { 0.6527f, 0.2194f }, { 0.8516f, 0.2194f } };
            f.knobs.clear(); f.labels.clear();
            for (int i = 0; i < juce::jmin(numKnobs, 4); ++i)
            {
                const auto k = juce::Rectangle<float>(d, d).withCentre({ x0 + W * pos[i][0], y0 + H * pos[i][1] });
                f.knobs.add(k);
                f.labels.add({ k.getX(), k.getBottom(), d, 13.0f });          // printed in the art; unused
            }
            f.emblem   = body.reduced(W * artInset2, W * artInset2);
            f.name     = f.emblem;                                         // the artwork carries name + labels
            f.subtitle = {};
            const float sws = W * 0.1072f;
            f.twoSwitches = true;
            f.footswitch  = juce::Rectangle<float>(sws, sws).withCentre({ x0 + W * 0.6293f, y0 + H * 0.8146f });   // EQ
            f.footswitch2 = juce::Rectangle<float>(sws, sws).withCentre({ x0 + W * 0.3668f, y0 + H * 0.8146f });   // BOOST
            f.led  = juce::Rectangle<float>(13.0f, 13.0f).withCentre({ x0 + W * 0.6300f, y0 + H * 0.7008f });
            f.led2 = juce::Rectangle<float>(13.0f, 13.0f).withCentre({ x0 + W * 0.3681f, y0 + H * 0.7008f });
            f.switchLabel1 = f.switchLabel2 = {};                          // EQ / BOOST printed in the art
            const float Wu = 226.0f * W / bodyWidthFor(2);
            f.hasToggle = withToggle;
            f.toggle      = juce::Rectangle<float>(Wu * 0.10f, Wu * 0.16f).withCentre({ x0 + W * 0.5000f, y0 + H * 0.2045f });
            f.toggleLabel = {};                                            // ORDER / B > EQ / EQ > B printed in the art
            return f;
        }
        if (pedalId == "tv")
        {
            // Twin Vise (double-wide, 464 x 372): Michael's full-face artwork (Assets/twinvise_art.png), printed artInset2
            // inside the edge. Five knobs inside the printed orange scale arcs (INPUT GAIN / LOW COMP / HIGH COMP /
            // BALANCE / LEVEL), the bicolour INPUT LED under the vise's name plate, footswitches HI COMP (left, secondary)
            // and EFFECT (right, main) with their LEDs - all on the controls painted in the art (fractions printed by
            // Tools/Art/make_twinvise_art.py).
            f.knobs.clear(); f.labels.clear();
            const float d = W * 0.1092f;
            const float kx[5] = { 0.1225f, 0.3102f, 0.4986f, 0.6884f, 0.8769f };
            for (int i = 0; i < juce::jmin(numKnobs, 5); ++i)
            {
                const auto k = juce::Rectangle<float>(d, d).withCentre({ x0 + W * kx[i], y0 + H * 0.1578f });
                f.knobs.add(k);
                f.labels.add({ k.getX(), k.getBottom(), d, 13.0f });          // printed in the art; unused
            }
            f.emblem   = body.reduced(W * artInset2, W * artInset2);
            f.name     = f.emblem;                                         // the artwork carries name + labels
            f.subtitle = {};
            f.meterLed = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ x0 + W * 0.4993f, y0 + H * 0.6328f });
            f.meterLabel = {};                                             // INPUT printed in the art
            const float sws = W * 0.0969f;
            f.twoSwitches = true;
            f.footswitch  = juce::Rectangle<float>(sws, sws).withCentre({ x0 + W * 0.7062f, y0 + H * 0.8132f });   // EFFECT
            f.footswitch2 = juce::Rectangle<float>(sws, sws).withCentre({ x0 + W * 0.2931f, y0 + H * 0.8132f });   // HI COMP
            f.led  = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ x0 + W * 0.7048f, y0 + H * 0.6932f });
            f.led2 = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ x0 + W * 0.2945f, y0 + H * 0.6941f });
            f.switchLabel1 = f.switchLabel2 = {};                          // HI COMP / EFFECT printed in the art
            return f;
        }
        if (pedalId == "fb")
        {
            // Frostbite: Michael's full-face artwork (Assets/frostbite_art.png), printed artInset inside the edge. Knobs in
            // two rows (GAIN / PRE over POST / LEVEL), MOD toggle between the top knobs, LED under it, footswitch on the
            // frozen lake - each on the art's painted one. Fractions from Tools/Art/make_frostbite_art.py
            // (= render_frostbite.py).
            f.knobs.clear(); f.labels.clear();
            const float dk = W * 0.2248f;
            const float kc[4][2] = { { 0.2054f, 0.1167f }, { 0.7848f, 0.1167f }, { 0.2075f, 0.3210f }, { 0.7848f, 0.3210f } };
            for (int i = 0; i < juce::jmin(numKnobs, 4); ++i)
            {
                const auto k = juce::Rectangle<float>(dk, dk).withCentre({ x0 + W * kc[i][0], y0 + H * kc[i][1] });
                f.knobs.add(k);
                f.labels.add({ k.getX(), k.getBottom(), dk, 13.0f });      // printed in the art; unused
            }
            f.emblem   = body.reduced(W * artInset);
            f.name     = f.emblem;                                         // the artwork carries name + labels
            f.subtitle = {};
            f.led      = juce::Rectangle<float>(13.0f, 13.0f).withCentre({ x0 + W * 0.4967f, y0 + H * 0.3693f });
            const float swd = W * 0.194f;
            f.footswitch = juce::Rectangle<float>(swd, swd).withCentre({ x0 + W * 0.4935f, y0 + H * 0.7961f });
            f.hasToggle = withToggle;
            f.toggle      = juce::Rectangle<float>(W * 0.14f, W * 0.20f).withCentre({ x0 + W * 0.4956f, y0 + H * 0.2202f });
            f.toggleLabel = {};                                            // MOD printed in the art
            return f;
        }
        if (numSwitches >= 2)
        {
            f.twoSwitches = true;
            const float cx1 = x0 + W * 0.29f, cx2 = x0 + W * 0.71f;
            f.footswitch  = juce::Rectangle<float>(sw, sw).withCentre({ cx1, y0 + H * 0.865f });
            f.footswitch2 = juce::Rectangle<float>(sw, sw).withCentre({ cx2, y0 + H * 0.865f });
            f.led  = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ cx1, y0 + H * 0.745f });
            f.led2 = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ cx2, y0 + H * 0.745f });
            f.switchLabel1 = { cx1 - W * 0.13f, y0 + H * 0.675f, W * 0.26f, H * 0.04f };
            f.switchLabel2 = { cx2 - W * 0.13f, y0 + H * 0.675f, W * 0.26f, H * 0.04f };
            f.emblem = { x0 + W * 0.38f, y0 + H * 0.57f, W * 0.24f, H * 0.12f };   // narrower: labels sit beside it
        }
        return f;
    }
};
