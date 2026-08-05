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

        // The JACKPOT panel carries everything to do with the seed.
        juce::Rectangle<float> seedDisplay { px (215.0f, 800.0f, 425.0f,  86.0f) };
        juce::Rectangle<float> seedEntry   { px (215.0f, 900.0f, 250.0f,  62.0f) };
        juce::Rectangle<float> go          { px (480.0f, 900.0f, 160.0f,  62.0f) };

        // The coin tray at the bottom.
        juce::Rectangle<float> meter { px (272.0f, 1038.0f, 312.0f, 133.0f) };

        // The WINS strip above the reels, left of the 777 graphic.
        juce::Rectangle<float> undo  { px (150.0f, 285.0f,  74.0f, 60.0f) };
        juce::Rectangle<float> redo  { px (232.0f, 285.0f,  74.0f, 60.0f) };
        juce::Rectangle<float> save  { px (150.0f, 668.0f, 150.0f, 48.0f) };
        juce::Rectangle<float> load  { px (310.0f, 668.0f, 150.0f, 48.0f) };
        juce::Rectangle<float> chaos { px (470.0f, 668.0f, 180.0f, 48.0f) };

        // No keyboard on the cabinet, so it lives below it - see keyboardStrip.
        juce::Rectangle<float> roll { px (272.0f, 1038.0f, 312.0f, 133.0f) };
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
