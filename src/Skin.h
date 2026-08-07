#pragma once
#include <JuceHeader.h>

// Where every control sits on the artwork.
//
// Positions are written in the *artwork's own pixel coordinates*, so numbers can
// be read straight off the Photoshop canvas and typed in here — no converting.
// The editor scales them to whatever size the window happens to be.
//
// Workflow:
//   1. Draw the machine at ArtWidth x ArtHeight (change those if you prefer
//      another canvas; everything below follows).
//   2. In Photoshop, marquee-select the hole you carved for a control and read
//      X / Y / W / H off the Info panel.
//   3. Put those four numbers in the matching line below.
//   4. Run with dev mode on (seed 777) to see every hotspot outlined on top of
//      the art, and nudge until they sit right.
namespace Skin
{
    // The canvas the artwork is authored at. Only the *ratio* matters at
    // runtime, but keeping it equal to the real file keeps the numbers honest.
    inline constexpr float ArtWidth  = 908.0f;
    inline constexpr float ArtHeight = 1206.0f;

    inline constexpr float aspect() { return ArtWidth / ArtHeight; }

    // Artwork pixels -> a 0..1 fraction of the canvas.
    inline juce::Rectangle<float> px (float x, float y, float w, float h)
    {
        return { x / ArtWidth, y / ArtHeight, w / ArtWidth, h / ArtHeight };
    }

    // ---- Control positions -------------------------------------------------
    // These are placeholders laid out on a plain 900x1200 canvas. Replace the
    // numbers as the artwork dictates; the names are what the editor asks for.
    // Measured from the artwork's own transparent cut-outs, so these match the
    // holes exactly rather than being eyeballed.
    struct Layout
    {
        // The three windows, left to right.
        juce::Rectangle<float> reel (int index) const
        {
            switch (index)
            {
                case 0:  return px (214.0f, 399.0f, 132.0f, 240.0f);
                case 1:  return px (360.0f, 399.0f, 135.0f, 242.0f);
                default: return px (510.0f, 399.0f, 137.0f, 244.0f);
            }
        }

        // HOLD sits directly under its own window.
        juce::Rectangle<float> hold (int index) const
        {
            auto r = reel (index);
            return { r.getX(), r.getY() + r.getHeight() + 0.006f, r.getWidth(), 0.026f };
        }

        // The lever: knob measured at 798,319; the stem runs down to y=851.
        juce::Rectangle<float> lever { px (758.0f, 300.0f, 112.0f, 560.0f) };

        // The JACKPOT window is a real cut-out, and the keyboard shows through
        // it — drawn beneath the cabinet so the frame overlaps the keys. The
        // banner decal is opaque down to y=856, so the keys start below it and
        // the whole window gets a dark backing rather than raw backdrop.
        juce::Rectangle<float> jackpotWindow { px (152.0f, 741.0f, 545.0f, 251.0f) };
        // Keys fill the window top to bottom and overhang it slightly. They
        // stretch vertically, which is fine — a tall key is still a key.
        juce::Rectangle<float> keyboard      { px (144.0f, 741.0f, 561.0f, 249.0f) };
        juce::Rectangle<float> coinTray      { px (272.0f, 1038.0f, 312.0f, 133.0f) };

        // The middle row is 306..341, and the WINS/777 graphic occupies
        // x=335..522 of it, so the controls live in the clear spans either
        // side: transport and seed on the left, meter on the right.
        juce::Rectangle<float> undo      { px (158.0f, 310.0f,  32.0f, 27.0f) };
        juce::Rectangle<float> redo      { px (194.0f, 310.0f,  32.0f, 27.0f) };
        juce::Rectangle<float> seedEntry { px (232.0f, 310.0f, 100.0f, 27.0f) };
        juce::Rectangle<float> go        { px (  0.0f,   0.0f,   0.0f,  0.0f) };  // folded into the box
        juce::Rectangle<float> meter     { px (525.0f, 310.0f, 182.0f, 27.0f) };

        // The coin tray holds what winnings buy. Button heights match the
        // SAVE/LOAD/CHAOS row above so the text is the same size as the rest.
        juce::Rectangle<float> nudge   { px (282.0f, 1046.0f, 172.0f, 52.0f) };
        juce::Rectangle<float> credits { px (462.0f, 1046.0f, 110.0f, 52.0f) };
        juce::Rectangle<float> shop    { px (282.0f, 1104.0f, 290.0f, 50.0f) };

        // The band between the reels and the window.
        juce::Rectangle<float> save  { px (152.0f, 664.0f, 170.0f, 52.0f) };
        juce::Rectangle<float> load  { px (334.0f, 664.0f, 170.0f, 52.0f) };
        juce::Rectangle<float> chaos { px (516.0f, 664.0f, 180.0f, 52.0f) };
    };

    inline const Layout& layout()
    {
        static const Layout l;
        return l;
    }

    // Scale a normalised rect onto the area the artwork is actually drawn in.
    inline juce::Rectangle<int> place (juce::Rectangle<float> norm,
                                       juce::Rectangle<int> artArea)
    {
        return juce::Rectangle<float> (
                   artArea.getX() + norm.getX() * artArea.getWidth(),
                   artArea.getY() + norm.getY() * artArea.getHeight(),
                   norm.getWidth()  * artArea.getWidth(),
                   norm.getHeight() * artArea.getHeight()).toNearestInt();
    }

    // The largest rectangle of the artwork's aspect ratio that fits `bounds`,
    // centred — so the machine never stretches.
    inline juce::Rectangle<int> fitArtwork (juce::Rectangle<int> bounds)
    {
        const float target = aspect();
        const float have   = (float) bounds.getWidth() / (float) juce::jmax (1, bounds.getHeight());

        int w = bounds.getWidth(), h = bounds.getHeight();
        if (have > target) w = juce::roundToInt ((float) h * target);
        else               h = juce::roundToInt ((float) w / target);

        return juce::Rectangle<int> (w, h).withCentre (bounds.getCentre());
    }
}
