#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "DevPanel.h"
#include "Theme.h"
#include "Skin.h"

// Stereo output meter with peak decay and a clip flag. Click it to clear.
class OutputMeter : public juce::Component, private juce::Timer
{
public:
    explicit OutputMeter (GambleSynthProcessor& p) : proc (p) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto outer = getLocalBounds();
        g.setColour (Theme::ink());
        g.drawRect (outer, 1);

        auto r = outer.reduced (2).toFloat();
        const float barH = (r.getHeight() - 1.0f) * 0.5f;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (r.getX(), r.getY() + ch * (barH + 1.0f),
                                               r.getWidth(), barH);
            const float level = juce::jlimit (0.0f, 1.0f, proc.getMeterLevel (ch));

            // Compress the low end so quiet material is still visible.
            g.setColour (Theme::ink());
            g.fillRect (bar.withWidth (bar.getWidth() * std::sqrt (level)));
        }

        // So an idle meter doesn't read as a broken button.
        g.setColour (Theme::dim());
        g.setFont (Theme::mono (9.0f));
        g.drawText ("OUT", outer.reduced (4, 0), juce::Justification::centredRight, false);

        // No colour to spare, so clipping is shown by inverting the whole meter.
        if (proc.isClipping())
        {
            g.setColour (Theme::ink());
            g.fillRect (outer);
            g.setColour (Theme::ground());
            g.setFont (Theme::mono ((float) juce::jmin (12, outer.getHeight() - 4), true));
            g.drawText ("CLIP", outer, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent&) override { proc.clearClip(); repaint(); }

private:
    void timerCallback() override { repaint(); }
    GambleSynthProcessor& proc;
};

class GambleSynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit GambleSynthEditor (GambleSynthProcessor&);
    ~GambleSynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Normally reached by typing seed 777; public so it can be driven directly.
    void setDevMode (bool shouldBeOn);

private:
    // ROLL wears the same skin, just much larger type.
    struct BigButtonLNF : MonoLookAndFeel
    {
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return Theme::mono (juce::jlimit (16.0f, 30.0f, buttonHeight * 0.34f), true);
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
        {
            g.setColour (down ? Theme::ground() : Theme::ink());
            g.setFont (getTextButtonFont (b, b.getHeight()));
            Theme::drawSpacedText (g, b.getButtonText(), b.getLocalBounds(), 6.0f);
        }
    };

    void refresh();          // sync labels / button states to the processor
    void applyTypedSeed();
    int  keyboardHeight() const;
    void layoutFromSkin (juce::Rectangle<int> artArea);
    void layoutPlain();      // the drawn UI, used when there's no artwork

    static constexpr int headerHeight = 36;

    GambleSynthProcessor& proc;
    MonoLookAndFeel mono;
    BigButtonLNF bigLNF;

    juce::TextButton rollButton  { "ROLL" };
    juce::TextButton chaosButton { "CHAOS" };
    juce::TextButton undoButton  { "<" };
    juce::TextButton redoButton  { ">" };
    juce::TextButton saveButton  { "SAVE" };
    juce::TextButton loadButton  { "LOAD" };
    juce::TextButton goButton    { "GO" };
    juce::Label      seedLabel;
    juce::TextEditor seedEditor;
    FlatKeyboard keyboard;

    // Lockable reels: keep part of a sound, re-roll the rest.
    juce::OwnedArray<juce::TextButton> lockButtons;
    OutputMeter meter { proc };

    // Skin artwork, embedded at build time from assets/. Null when there is
    // none, in which case the plain drawn UI is used instead.
    juce::Image skin;
    juce::Rectangle<int> artArea;      // where the artwork is actually drawn

    // Easter egg: seed 777 toggles the parameter panel (session-only).
    std::unique_ptr<DevPanel> devPanel;
    bool devMode = false;
    int  normalWidth  = 0;
    int  normalHeight = 400;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GambleSynthEditor)
};
