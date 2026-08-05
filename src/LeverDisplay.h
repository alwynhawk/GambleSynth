#pragma once
#include <JuceHeader.h>

// The lever, as four drawn frames played on a pull.
//
// Each frame is a full-canvas image with the lever in a different position, so
// they line up with the cabinet simply by being drawn over the same rectangle —
// no per-frame offsets to keep in step with the artwork.
//
// Mouse-transparent: the invisible ROLL hotspot sits above this and takes the
// clicks, so the animation never has to care about input.
class LeverDisplay : public juce::Component, private juce::Timer
{
public:
    // Down and back in half a second, which is about how long a real one takes.
    static constexpr int PullMs = 500;

    LeverDisplay()
    {
        setInterceptsMouseClicks (false, false);

       #if GAMBLESYNTH_HAS_ASSETS
        frames[0] = juce::ImageCache::getFromMemory (BinaryData::LEVER1_png, BinaryData::LEVER1_pngSize);
        frames[1] = juce::ImageCache::getFromMemory (BinaryData::LEVER2_png, BinaryData::LEVER2_pngSize);
        frames[2] = juce::ImageCache::getFromMemory (BinaryData::LEVER3_png, BinaryData::LEVER3_pngSize);
        frames[3] = juce::ImageCache::getFromMemory (BinaryData::LEVER4_png, BinaryData::LEVER4_pngSize);
       #endif
    }

    void pull()
    {
        startMs = juce::Time::getMillisecondCounter();
        startTimerHz (60);
    }

    // Land on the resting frame now — used for headless screenshots, where no
    // message loop runs to fire the timer.
    void settle()
    {
        stopTimer();
        frame = 0;
        repaint();
    }

    // Which frame belongs at this point in the pull. Pulled out so the sequence
    // can be checked without a running message loop.
    static int frameAt (int elapsedMs)
    {
        if (elapsedMs < 0 || elapsedMs >= PullMs) return 0;

        // First half pulls down through the frames, second half springs back.
        const float t = (float) elapsedMs / (float) PullMs;
        const int step = (t < 0.5f) ? (int) (t * 2.0f * NumFrames)
                                    : NumFrames - 1 - (int) ((t - 0.5f) * 2.0f * NumFrames);
        return juce::jlimit (0, NumFrames - 1, step);
    }

    static constexpr int frameCount() { return NumFrames; }

    void paint (juce::Graphics& g) override
    {
        const auto& img = frames[frame];
        if (img.isValid())
            g.drawImage (img, getLocalBounds().toFloat(),
                         juce::RectanglePlacement::stretchToFit, false);
    }

private:
    static constexpr int NumFrames = 4;

    void timerCallback() override
    {
        const auto elapsed = (int) (juce::Time::getMillisecondCounter() - startMs);
        if (elapsed >= PullMs)
        {
            settle();
            return;
        }

        const int wanted = frameAt (elapsed);
        if (wanted != frame)
        {
            frame = wanted;
            repaint();
        }
    }

    juce::Image frames[NumFrames];
    int frame = 0;
    juce::uint32 startMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LeverDisplay)
};
