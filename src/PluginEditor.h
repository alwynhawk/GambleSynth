#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "DevPanel.h"
#include "Theme.h"
#include "Skin.h"
#include "ReelDisplay.h"
#include "LeverDisplay.h"
#include "LibraryWindow.h"

// Stereo output meter with peak decay and a clip flag. Click it to clear.
class OutputMeter : public juce::Component, private juce::Timer
{
public:
    explicit OutputMeter (GambleSynthProcessor& p) : proc (p) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto outer = getLocalBounds();
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRect (outer, 1);

        auto r = outer.reduced (2).toFloat();
        const float barH = (r.getHeight() - 1.0f) * 0.5f;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (r.getX(), r.getY() + ch * (barH + 1.0f),
                                               r.getWidth(), barH);
            const float level = juce::jlimit (0.0f, 1.0f, proc.getMeterLevel (ch));

            // Compress the low end so quiet material is still visible.
            g.setColour (juce::Colours::black);
            g.fillRect (bar.withWidth (bar.getWidth() * std::sqrt (level)));
        }

        // Clipping inverts the whole meter — there is no colour to spare.
        if (proc.isClipping())
        {
            g.setColour (juce::Colours::black);
            g.fillRect (outer);
            g.setColour (juce::Colours::white);
            g.setFont (Theme::mono ((float) juce::jmin (12, outer.getHeight() - 4), true));
            g.drawText ("CLIP", outer, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent&) override { proc.clearClip(); repaint(); }

private:
    void timerCallback() override { repaint(); }
    GambleSynthProcessor& proc;
};

// The nudge balance: a number and a gold G, drawn together so the G stays the
// accent colour while the count matches every other readout on the cabinet.
struct CreditsDisplay : juce::Component
{
    int   credits   = 0;
    float fontScale = 0.42f;   // matches the button text height
    bool  dimmed    = false;   // the price tag reads quieter than the balance
    bool  leftAlign = false;   // the price hugs the button it belongs to

    void set (int c)
    {
        if (c == credits) return;
        credits = c;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto font = Theme::mono (juce::jlimit (9.0f, 26.0f, getHeight() * fontScale), true);
        const juce::String number (credits);

        // Lay the number and the G out as one line, then split the space so the
        // pair stays centred whatever the count is.
        const int gapW = juce::roundToInt (font.getHeight() * 0.35f);
        const int numW = juce::GlyphArrangement::getStringWidthInt (font, number);
        const int gW   = juce::GlyphArrangement::getStringWidthInt (font, "G");
        const int total = numW + gapW + gW;

        auto r = getLocalBounds();
        auto line = leftAlign ? r.withWidth (total)
                              : r.withWidth (total).withCentre (r.getCentre());

        {
            juce::Graphics::ScopedSaveState keep (g);
            if (dimmed) g.setOpacity (0.75f);
            Theme::drawOutlined (g, number, line.removeFromLeft (numW),
                                 juce::Justification::centred, font);
        }
        line.removeFromLeft (gapW);

        juce::GlyphArrangement ga;
        ga.addFittedText (font, "G", (float) line.getX(), (float) line.getY(),
                          (float) line.getWidth(), (float) line.getHeight(),
                          juce::Justification::centred, 1);
        juce::Path path;
        ga.createPath (path);
        g.setColour (juce::Colours::black);
        g.strokePath (path, juce::PathStrokeType (juce::jmax (2.5f, font.getHeight() * 0.22f),
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        g.setColour (Theme::gold().withAlpha (dimmed ? 0.8f : 1.0f));
        g.fillPath (path);
    }
};

// Catches clicks anywhere outside the library panel and closes it. Sits behind
// the panel and covers the whole editor, which is the only reliable way to get
// click-outside on a child component — the alternative is watching global mouse
// events, which misbehaves inside plugin hosts.
struct ClickCatcher : juce::Component
{
    explicit ClickCatcher (std::function<void()> fn) : onClick (std::move (fn)) {}
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    std::function<void()> onClick;
};

// Draws the cabinet on top of the keyboard, so the keys show through the
// JACKPOT cut-out with the frame overlapping them. Mouse-transparent, so
// clicking a key still reaches the keyboard underneath.
struct MachineOverlay : juce::Component
{
    explicit MachineOverlay (const juce::Image& art) : image (art)
    {
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        if (image.isValid())
            g.drawImage (image, getLocalBounds().toFloat(),
                         juce::RectanglePlacement::stretchToFit, false);
    }

    const juce::Image& image;
};

class GambleSynthEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit GambleSynthEditor (GambleSynthProcessor&);
    ~GambleSynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Normally reached by typing seed 777; public so it can be driven directly.
    void setDevMode (bool shouldBeOn);

    // Land the reels now, for headless screenshots.
    // Open the library panel, for headless screenshots.
    void showLibraryForShot() { if (libraryWindow == nullptr) toggleLibrary(); }

    void settleReels()
    {
        for (auto* r : reels) r->settle();
        if (lever != nullptr) lever->settle();
    }

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

    void timerCallback() override;   // re-enables the lever after the cooldown
    void refresh();          // sync labels / button states to the processor
    void applyTypedSeed();

public:
    void toggleLibrary();
private:
    void closeLibrary();
    int  keyboardHeight() const;
    void layoutFromSkin (juce::Rectangle<int> artArea);
    void layoutPlain();      // the drawn UI, used when there's no artwork

    static constexpr int headerHeight = 36;
    static constexpr int DefaultHeight = 900;   // the size it always opens at

    GambleSynthProcessor& proc;
    MonoLookAndFeel mono;
    SkinnedLookAndFeel skinned;
    BigButtonLNF bigLNF;

    juce::TextButton rollButton  { "ROLL" };
    juce::TextButton chaosButton { "CHAOS" };
    juce::TextButton undoButton  { "<" };
    juce::TextButton redoButton  { ">" };
    juce::TextButton saveButton  { "SAVE" };
    juce::TextButton loadButton  { "LOAD" };
    juce::TextButton goButton    { "GO" };
    juce::Label      seedLabel;
    juce::TextButton nudgeButton;
    juce::TextButton shopButton;
    CreditsDisplay   creditsDisplay;
    CreditsDisplay   costDisplay;
    juce::TextEditor seedEditor;
    FlatKeyboard keyboard;

    // Lockable reels: keep part of a sound, re-roll the rest.
    juce::OwnedArray<juce::TextButton> lockButtons;
    OutputMeter meter { proc };

    // Skin artwork, embedded at build time from assets/. Null when there is
    // none, in which case the plain drawn UI is used instead.
    juce::Image skin;
    juce::Image backdrop;              // fills the window behind the cabinet
    std::unique_ptr<MachineOverlay> overlay;
    std::unique_ptr<LeverDisplay>   lever;
    std::unique_ptr<LibraryWindow>  libraryWindow;
    std::unique_ptr<ClickCatcher>   libraryCatcher;
    juce::Rectangle<int> artArea;      // where the artwork is actually drawn
    juce::OwnedArray<ReelDisplay> reels;

    // Easter egg: seed 777 toggles the parameter panel (session-only).
    std::unique_ptr<DevPanel> devPanel;
    bool devMode = false;
    int  lastShownSeed = -1;
    int  lastShownSpin = -1;
    int  normalWidth  = 0;
    int  normalHeight = 400;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GambleSynthEditor)
};
