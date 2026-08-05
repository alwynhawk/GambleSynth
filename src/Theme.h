#pragma once
#include <JuceHeader.h>

// Monochrome, hard-edged, no gradients or rounded corners — the look of an
// early hardware-style plugin. Everything is drawn from two colours: black
// ground, white ink. "Selected" means the two swap.
namespace Theme
{
    inline juce::Colour ink()    { return juce::Colour (0xffffffff); }
    inline juce::Colour ground() { return juce::Colour (0xff000000); }
    inline juce::Colour dim()    { return juce::Colour (0xff6e6e6e); }   // disabled / hints
    inline juce::Colour gold()   { return juce::Colour (0xffd6b25e); }   // over artwork

    inline juce::Font mono (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Letter-spaced caps: the cheapest way to make plain text look deliberate.
    inline void drawSpacedText (juce::Graphics& g, const juce::String& text,
                                juce::Rectangle<int> area, float spacing,
                                juce::Justification just = juce::Justification::centred)
    {
        if (text.isEmpty())
            return;

        juce::String spaced;
        for (int i = 0; i < text.length(); ++i)
        {
            spaced << text[i];
            if (i < text.length() - 1)
                spaced << ' ';
        }
        juce::ignoreUnused (spacing);
        g.drawText (spaced, area, just, false);
    }
}

// The stock keyboard shades its keys; this one is strictly two colours.
struct FlatKeyboard : juce::MidiKeyboardComponent
{
    FlatKeyboard (juce::MidiKeyboardState& state, Orientation o)
        : juce::MidiKeyboardComponent (state, o) {}

    void drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour, juce::Colour) override
    {
        g.setColour (isDown ? Theme::dim()
                            : (isOver ? juce::Colour (0xffcfcfcf) : Theme::ink()));
        g.fillRect (area);
        g.setColour (Theme::ground());
        g.drawRect (area, 1.0f);

        if (midiNoteNumber % 12 == 0)                    // label the Cs, nothing else
        {
            g.setFont (Theme::mono (9.0f));
            g.drawText ("C" + juce::String (midiNoteNumber / 12 - 1),
                        area.reduced (1.0f).removeFromBottom (12.0f).toNearestInt(),
                        juce::Justification::centred, false);
        }
    }

    void drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour) override
    {
        g.setColour (isDown ? Theme::dim() : Theme::ground());
        g.fillRect (area);
        g.setColour (isOver ? juce::Colour (0xffcfcfcf) : Theme::ink());
        g.drawRect (area, 1.0f);
    }
};

// Over artwork, a control must not paint a slab of its own — the cabinet is the
// background. Text and a hairline only, so the machine shows through.
struct SkinnedLookAndFeel : juce::LookAndFeel_V4
{
    SkinnedLookAndFeel()
    {
        setColour (juce::Label::textColourId,                 Theme::gold());
        setColour (juce::TextEditor::backgroundColourId,      juce::Colours::black.withAlpha (0.55f));
        setColour (juce::TextEditor::textColourId,            Theme::gold());
        setColour (juce::TextEditor::outlineColourId,         Theme::gold().withAlpha (0.5f));
        setColour (juce::TextEditor::focusedOutlineColourId,  Theme::gold());
        setColour (juce::TextEditor::highlightColourId,       Theme::gold());
        setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::black);
        setColour (juce::CaretComponent::caretColourId,       Theme::gold());
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return Theme::mono (juce::jlimit (10.0f, 26.0f, buttonHeight * 0.42f), true);
    }

    // The seed is the shareable thing, so it gets to be the biggest text here.
    juce::Font getLabelFont (juce::Label& label) override
    {
        return Theme::mono (juce::jmax (12.0f, label.getHeight() * 0.72f), true);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);

        if (b.getToggleState() || down)          // engaged: a dark plate + gold rule
        {
            g.setColour (juce::Colours::black.withAlpha (0.62f));
            g.fillRect (r);
            g.setColour (Theme::gold());
            g.drawRect (r, 1.5f);
        }
        else if (highlighted)
        {
            g.setColour (Theme::gold().withAlpha (0.5f));
            g.drawRect (r, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        const auto text = b.getButtonText();
        if (text.isEmpty()) return;              // invisible hotspots draw nothing

        g.setFont (getTextButtonFont (b, b.getHeight()));

        // A dark outline keeps the label readable over whatever it sits on.
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx || dy)
                    g.drawText (text, b.getLocalBounds().translated (dx, dy),
                                juce::Justification::centred, false);

        g.setColour (b.isEnabled() ? (down ? juce::Colours::white : Theme::gold())
                                   : Theme::gold().withAlpha (0.35f));
        g.drawText (text, b.getLocalBounds(), juce::Justification::centred, false);
    }
};

struct MonoLookAndFeel : juce::LookAndFeel_V4
{
    MonoLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Theme::ground());
        setColour (juce::Label::textColourId,                 Theme::ink());
        setColour (juce::TextEditor::backgroundColourId,      Theme::ground());
        setColour (juce::TextEditor::textColourId,            Theme::ink());
        setColour (juce::TextEditor::outlineColourId,         Theme::ink());
        setColour (juce::TextEditor::focusedOutlineColourId,  Theme::ink());
        setColour (juce::TextEditor::highlightColourId,       Theme::ink());
        setColour (juce::TextEditor::highlightedTextColourId, Theme::ground());
        setColour (juce::CaretComponent::caretColourId,       Theme::ink());
        setColour (juce::ScrollBar::thumbColourId,            Theme::dim());
        setColour (juce::ScrollBar::trackColourId,            Theme::ground());
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return Theme::mono (juce::jlimit (11.0f, 30.0f, buttonHeight * 0.34f), true);
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        return Theme::mono (juce::jmax (11.0f, label.getHeight() * 0.55f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const bool filled = on || down;

        g.setColour (filled ? Theme::ink() : Theme::ground());
        g.fillRect (r);

        g.setColour (b.isEnabled() ? Theme::ink() : Theme::dim());
        g.drawRect (r, 1.0f);

        if (highlighted && ! filled)          // hover: a second inset rule, no colour
        {
            g.setColour (Theme::dim());
            g.drawRect (r.reduced (2.0f), 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                         bool /*highlighted*/, bool down) override
    {
        const bool filled = b.getToggleState() || down;
        g.setColour (! b.isEnabled() ? Theme::dim()
                                     : (filled ? Theme::ground() : Theme::ink()));
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }

    // Flat slider: a rule for the track, a solid block for the thumb.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& s) override
    {
        auto track = juce::Rectangle<float> ((float) x, y + height * 0.5f - 0.5f,
                                             (float) width, 1.0f);
        g.setColour (Theme::dim());
        g.fillRect (track);

        g.setColour (Theme::ink());
        g.fillRect (juce::Rectangle<float> ((float) x, track.getY(),
                                            sliderPos - (float) x, 1.0f));

        const float thumbW = 3.0f;
        g.fillRect (juce::Rectangle<float> (sliderPos - thumbW * 0.5f, (float) y + 2.0f,
                                            thumbW, (float) height - 4.0f));
        juce::ignoreUnused (s);
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
        l->setColour (juce::Label::textColourId,            Theme::ink());
        l->setColour (juce::Label::backgroundColourId,      Theme::ground());
        l->setColour (juce::Label::outlineColourId,         juce::Colours::transparentBlack);
        l->setColour (juce::TextEditor::backgroundColourId, Theme::ground());
        l->setColour (juce::TextEditor::textColourId,       Theme::ink());
        l->setJustificationType (juce::Justification::centredRight);
        return l;
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isVertical, int thumbStart, int thumbSize,
                        bool /*mouseOver*/, bool /*mouseDown*/) override
    {
        g.setColour (Theme::dim());
        if (isVertical) g.fillRect (x + width / 2, y, 1, height);
        else            g.fillRect (x, y + height / 2, width, 1);

        g.setColour (Theme::ink());
        if (isVertical) g.fillRect (x + 1, thumbStart, width - 2, thumbSize);
        else            g.fillRect (thumbStart, y + 1, thumbSize, height - 2);
    }
};
