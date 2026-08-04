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
    inline constexpr float ArtWidth  = 900.0f;
    inline constexpr float ArtHeight = 1200.0f;

    inline constexpr float aspect() { return ArtWidth / ArtHeight; }

    // Artwork pixels -> a 0..1 fraction of the canvas.
    inline juce::Rectangle<float> px (float x, float y, float w, float h)
    {
        return { x / ArtWidth, y / ArtHeight, w / ArtWidth, h / ArtHeight };
    }

    // ---- Control positions -------------------------------------------------
    // These are placeholders laid out on a plain 900x1200 canvas. Replace the
    // numbers as the artwork dictates; the names are what the editor asks for.
    struct Layout
    {
        juce::Rectangle<float> reelWindow { px (110.0f,  250.0f, 680.0f, 260.0f) };
        juce::Rectangle<float> lever      { px (800.0f,  300.0f,  80.0f, 380.0f) };
        juce::Rectangle<float> roll       { px (300.0f,  580.0f, 300.0f, 110.0f) };

        juce::Rectangle<float> seedDisplay { px (110.0f,  120.0f, 480.0f,  70.0f) };
        juce::Rectangle<float> seedEntry   { px (600.0f,  120.0f, 130.0f,  70.0f) };
        juce::Rectangle<float> go          { px (740.0f,  120.0f,  90.0f,  70.0f) };

        juce::Rectangle<float> undo  { px (110.0f, 730.0f,  90.0f, 70.0f) };
        juce::Rectangle<float> redo  { px (210.0f, 730.0f,  90.0f, 70.0f) };
        juce::Rectangle<float> save  { px (330.0f, 730.0f, 150.0f, 70.0f) };
        juce::Rectangle<float> load  { px (490.0f, 730.0f, 150.0f, 70.0f) };
        juce::Rectangle<float> chaos { px (650.0f, 730.0f, 140.0f, 70.0f) };
        juce::Rectangle<float> meter { px (110.0f, 820.0f, 680.0f, 40.0f) };

        juce::Rectangle<float> keyboard { px (60.0f, 900.0f, 780.0f, 250.0f) };

        // The five HOLD reels, evenly spaced under the reel window.
        juce::Rectangle<float> hold (int index) const
        {
            const float w = 120.0f, gap = 16.0f;
            const float total = 5.0f * w + 4.0f * gap;
            const float x0 = (ArtWidth - total) * 0.5f;
            return px (x0 + (float) index * (w + gap), 530.0f, w, 40.0f);
        }
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
