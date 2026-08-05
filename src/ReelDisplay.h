#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Fruit.h"

// One window of the machine. Each reel shows a symbol standing for one third of
// the sound, so the three of them read as "what did I just land on" without ever
// naming an archetype.
//
//   TONE   what is making the sound   (wave, or the osc mode if one is engaged)
//   SHAPE  what is shaping it         (filter model / response)
//   SPACE  where it is                (the loudest effect on the chain)
//
// Symbols are drawn rather than pictured, so they stay sharp at any size and
// match the cabinet's own line art.
class ReelDisplay : public juce::Component, private juce::Timer
{
public:
    enum Kind { Tone = 0, Shape, Space };

    ReelDisplay (GambleSynthProcessor& p, Kind k) : proc (p), kind (k) {}

    // Called on every roll. Reels stop left to right, like a real machine —
    // the sound has already changed, this is just the flourish catching up.
    void startSpin (int reelIndex)
    {
        spinning   = true;
        stopAtMs   = juce::Time::getMillisecondCounter() + 220 + reelIndex * 130;
        startTimerHz (30);
    }

    // Land immediately. Used for screenshots, where no message loop is running
    // to fire the timer.
    void settle()
    {
        spinning = false;
        stopTimer();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // No background: the cabinet artwork carries its own reel backing, and
        // this component sits over it. Anything painted here would cover it.
        auto r = getLocalBounds().toFloat();

        // A spinning reel is a blur of passing symbols, not a symbol.
        if (spinning)
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            const float band = r.getHeight() / 5.0f;
            for (int i = -1; i < 6; ++i)
            {
                const float y = r.getY() + std::fmod (blurPhase + (float) i * band, r.getHeight() + band) - band;
                g.fillRect (r.getX() + 6.0f, y, r.getWidth() - 12.0f, 2.0f);
            }
            return;
        }

        drawFruit (g, r);
    }

private:
    void timerCallback() override
    {
        blurPhase += 26.0f;
        if (blurPhase > 10000.0f) blurPhase = 0.0f;

        if (juce::Time::getMillisecondCounter() >= stopAtMs)
        {
            spinning = false;
            stopTimer();
        }
        repaint();
    }

    // Which symbol this reel is showing, derived from the sound itself.
    int symbolIndex() const
    {
        const auto& p = proc.getPatch();

        if (kind == Tone)
        {
            if (p.oscMode == 1) return 5;                 // ring
            if (p.oscMode == 2) return 6;                 // sync
            if (p.oscMode == 3) return 7;                 // fm
            return juce::jlimit (0, 4, p.osc[0].wave);    // sine/tri/saw/square/noise
        }

        if (kind == Shape)
        {
            if (p.filterModel == 3) return 11;            // vowel
            if (p.filterModel == 4) return 12;            // comb
            if (p.filterModel == 1 || p.filterModel == 2) return 10;   // ladder
            return 8 + juce::jlimit (0, 2, p.filterType); // lp / bp / hp -> 8,9,10
        }

        // Space: whichever effect is doing the most.
        struct Fx { float amount; int symbol; };
        const Fx fx[] = {
            { p.crushBits < 15.0f ? 1.0f - p.crushBits / 16.0f : 0.0f, 13 },  // crush
            { p.foldAmount,  14 },
            { p.phaserMix,   15 },
            { p.flangerMix,  16 },
            { p.delayMix,    17 },
            { p.reverbMix,   18 },
        };
        int best = 19;                                    // dry
        float loudest = 0.18f;                            // must be audible to show
        for (const auto& f : fx)
            if (f.amount > loudest) { loudest = f.amount; best = f.symbol; }
        return best;
    }

   #if GAMBLESYNTH_HAS_ASSETS
    static juce::Image fruitImage (Fruit f)
    {
        switch (f)
        {
            case Fruit::Cherry: return juce::ImageCache::getFromMemory (BinaryData::cherry_png, BinaryData::cherry_pngSize);
            case Fruit::Lemon:  return juce::ImageCache::getFromMemory (BinaryData::lemon_png,  BinaryData::lemon_pngSize);
            case Fruit::Orange: return juce::ImageCache::getFromMemory (BinaryData::orange_png, BinaryData::orange_pngSize);
            case Fruit::Apple:  return juce::ImageCache::getFromMemory (BinaryData::apple_png,  BinaryData::apple_pngSize);
            case Fruit::Grape:  return juce::ImageCache::getFromMemory (BinaryData::grape_png,  BinaryData::grape_pngSize);
            default:            return juce::ImageCache::getFromMemory (BinaryData::seven_png,  BinaryData::seven_pngSize);
        }
    }
   #endif

    void drawFruit (juce::Graphics& g, juce::Rectangle<float> r) const
    {
       #if GAMBLESYNTH_HAS_ASSETS
        const auto img = fruitImage (proc.getFruitSpin().symbol[(int) kind]);
        if (! img.isValid()) return;

        // drawImage inherits the current brush's opacity, and the strip above
        // leaves it part-transparent from drawing its divider lines.
        g.setColour (juce::Colours::white);

        // Square, centred, filling most of the window — it is the backdrop the
        // sound glyph sits on, so it wants to be big.
        const float side = juce::jmin (r.getWidth(), r.getHeight()) * 0.86f;
        g.drawImage (img, juce::Rectangle<float> (side, side).withCentre (r.getCentre()),
                     juce::RectanglePlacement::centred, false);
       #else
        juce::ignoreUnused (g, r);
       #endif
    }

    // Symbols are built as paths so they can be stroked black and filled white,
    // which keeps them readable over any part of the cabinet.
    void drawSymbol (juce::Graphics& g, juce::Rectangle<float> b) const
    {
        juce::Path strokes, fills;
        buildSymbol (strokes, fills, b);

        const float th = juce::jmax (2.0f, b.getWidth() * 0.05f);
        const juce::PathStrokeType outline (th * 2.8f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded);

        g.setColour (juce::Colours::black);
        if (! strokes.isEmpty()) g.strokePath (strokes, outline);
        if (! fills.isEmpty())   { g.strokePath (fills, outline); g.fillPath (fills); }

        g.setColour (juce::Colours::white);
        if (! strokes.isEmpty())
            g.strokePath (strokes, juce::PathStrokeType (th, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
        if (! fills.isEmpty()) g.fillPath (fills);
    }

    void buildSymbol (juce::Path& strokes, juce::Path& fills, juce::Rectangle<float> b) const
    {
        const float cx = b.getCentreX(), cy = b.getCentreY();
        const float w = b.getWidth(), h = b.getHeight();
        const float th = juce::jmax (2.0f, w * 0.05f);

        auto wave = [&] (int shape)
        {
            const int steps = 96;
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float) i / steps;
                float v = 0.0f;
                switch (shape)
                {
                    case 0: v = std::sin (t * juce::MathConstants<float>::twoPi); break;
                    case 1: v = 1.0f - 4.0f * std::abs (std::fmod (t + 0.25f, 1.0f) - 0.5f); break;
                    case 2: v = 1.0f - 2.0f * t; break;
                    case 3: v = t < 0.5f ? 1.0f : -1.0f; break;
                    default: break;
                }
                const float x = b.getX() + t * w;
                const float y = cy - v * h * 0.28f;
                if (i == 0) strokes.startNewSubPath (x, y); else strokes.lineTo (x, y);
            }
        };

        switch (symbolIndex())
        {
            case 0: case 1: case 2: case 3:
                wave (symbolIndex());
                break;

            case 4:   // noise
            {
                juce::Random rng (7);
                for (int i = 0; i < 22; ++i)
                {
                    const float x = b.getX() + w * (float) i / 21.0f;
                    const float v = std::abs (rng.nextFloat() * 2.0f - 1.0f);
                    fills.addRectangle (x, cy - v * h * 0.3f, th, v * h * 0.6f);
                }
                break;
            }

            case 5:   // ring: two interlocking circles
                strokes.addEllipse (cx - w * 0.30f, cy - h * 0.18f, w * 0.38f, h * 0.36f);
                strokes.addEllipse (cx - w * 0.08f, cy - h * 0.18f, w * 0.38f, h * 0.36f);
                break;

            case 6:   // sync: a wave cut off mid-cycle
                wave (2);
                fills.addRectangle (cx - th * 0.5f, cy - h * 0.34f, th, h * 0.68f);
                break;

            case 7:   // fm: a small circle orbiting a large one
                strokes.addEllipse (cx - w * 0.26f, cy - h * 0.24f, w * 0.52f, h * 0.48f);
                fills.addEllipse (cx + w * 0.16f, cy - h * 0.36f, w * 0.16f, h * 0.15f);
                break;

            case 8:   // low pass
                strokes.startNewSubPath (b.getX(), cy - h * 0.2f);
                strokes.lineTo (cx, cy - h * 0.2f);
                strokes.quadraticTo (cx + w * 0.18f, cy - h * 0.2f, b.getRight(), cy + h * 0.3f);
                break;

            case 9:   // band pass
                strokes.startNewSubPath (b.getX(), cy + h * 0.3f);
                strokes.quadraticTo (cx, cy - h * 0.45f, b.getRight(), cy + h * 0.3f);
                break;

            case 10:  // high pass / ladder
                strokes.startNewSubPath (b.getX(), cy + h * 0.3f);
                strokes.quadraticTo (cx - w * 0.18f, cy - h * 0.2f, cx, cy - h * 0.2f);
                strokes.lineTo (b.getRight(), cy - h * 0.2f);
                break;

            case 11:  // vowel: nested arcs
                for (int i = 0; i < 3; ++i)
                {
                    const float sc = 0.5f + (float) i * 0.22f;
                    strokes.addEllipse (cx - w * 0.5f * sc, cy - h * 0.42f * sc,
                                        w * sc, h * 0.84f * sc);
                }
                break;

            case 12:  // comb: teeth
                for (int i = 0; i < 6; ++i)
                {
                    const float x = b.getX() + w * (0.08f + 0.17f * (float) i);
                    const float hh = h * (i % 2 == 0 ? 0.42f : 0.24f);
                    fills.addRectangle (x, cy - hh * 0.5f, th, hh);
                }
                break;

            case 13:  // crush: stair-stepped blocks
                for (int i = 0; i < 5; ++i)
                {
                    const float x = b.getX() + w * 0.1f + (float) i * w * 0.17f;
                    const float hh = h * (0.12f + 0.08f * (float) ((i * 3) % 4));
                    strokes.addRectangle (x, cy - hh, w * 0.13f, hh * 2.0f);
                }
                break;

            case 14:  // fold: sharp zigzag
                strokes.startNewSubPath (b.getX(), cy);
                for (int i = 1; i <= 6; ++i)
                    strokes.lineTo (b.getX() + w * (float) i / 6.0f,
                                    cy + ((i % 2) ? -h * 0.32f : h * 0.32f));
                break;

            case 15:  // phaser: swirl
                for (int i = 0; i < 3; ++i)
                {
                    const float sc = 0.35f + (float) i * 0.2f;
                    strokes.addEllipse (cx - w * 0.5f * sc,
                                        cy - h * 0.45f * sc + (float) i * h * 0.06f,
                                        w * sc, h * 0.9f * sc);
                }
                break;

            case 16:  // flanger: converging lines
                for (int i = 0; i < 5; ++i)
                {
                    const float y = cy - h * 0.3f + (float) i * h * 0.15f;
                    fills.addRectangle (b.getX() + (float) i * w * 0.06f, y,
                                        w - (float) i * w * 0.12f, th * 0.8f);
                }
                break;

            case 17:  // delay: decaying repeats
                for (int i = 0; i < 4; ++i)
                {
                    const float x = b.getX() + w * (0.12f + 0.24f * (float) i);
                    const float hh = h * 0.42f / (1.0f + (float) i * 0.7f);
                    fills.addRectangle (x, cy - hh * 0.5f, th * 1.2f, hh);
                }
                break;

            case 18:  // reverb: expanding arcs
                for (int i = 1; i <= 3; ++i)
                {
                    const float sc = (float) i * 0.3f;
                    strokes.addCentredArc (b.getX() + w * 0.15f, cy, w * sc, h * sc * 0.9f,
                                           0.0f, -1.1f, 1.1f, true);
                }
                break;

            default:  // dry: a plain bar
                fills.addRectangle (b.getX() + w * 0.1f, cy - th * 0.5f, w * 0.8f, th);
                break;
        }
    }

    GambleSynthProcessor& proc;
    Kind  kind;
    bool  spinning   = false;
    float blurPhase  = 0.0f;
    juce::uint32 stopAtMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelDisplay)
};
