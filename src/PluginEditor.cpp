#include "PluginEditor.h"

GambleSynthEditor::GambleSynthEditor (GambleSynthProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&mono);

    auto addButton = [this] (juce::TextButton& b) { addAndMakeVisible (b); };

    rollButton.setLookAndFeel (&bigLNF);
    addButton (rollButton);
    rollButton.onClick = [this] { proc.pullLever(); };

    chaosButton.setClickingTogglesState (true);
    chaosButton.onClick = [this] { proc.setChaos (chaosButton.getToggleState()); };
    addButton (chaosButton);

    addButton (undoButton);
    addButton (redoButton);
    addButton (saveButton);
    addButton (loadButton);
    addButton (goButton);
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.onClick = [this] { proc.redo(); };
    saveButton.onClick = [this] { proc.saveFavourite(); };
    loadButton.onClick = [this] { proc.loadNextFavourite(); };
    goButton.onClick   = [this] { applyTypedSeed(); };

    seedLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (seedLabel);

    seedEditor.setInputRestrictions (6, "0123456789");
    seedEditor.setTextToShowWhenEmpty ("SEED", Theme::dim());
    seedEditor.setJustification (juce::Justification::centred);
    seedEditor.setFont (Theme::mono (17.0f));
    seedEditor.onReturnKey = [this] { applyTypedSeed(); };
    addAndMakeVisible (seedEditor);

    // One lock per reel — lit means ROLL leaves that part of the sound alone.
    for (int reel = 0; reel < NumReels; ++reel)
    {
        auto* b = lockButtons.add (new juce::TextButton (reelName (reel)));
        b->setClickingTogglesState (true);
        b->onClick = [this, reel] { proc.setReelLocked (reel, lockButtons[reel]->getToggleState()); };
        b->setVisible (false);          // dev-mode only (seed 777)
        addChildComponent (b);
    }

    addAndMakeVisible (meter);

    keyboard.setKeyWidth (34.0f);
    keyboard.setLowestVisibleKey (36);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId,        Theme::ink());
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId,        Theme::ground());
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId,           juce::Colours::transparentBlack);
    keyboard.setColour (juce::KeyboardComponentBase::upDownButtonBackgroundColourId, Theme::ground());
    keyboard.setColour (juce::KeyboardComponentBase::upDownButtonArrowColourId,      Theme::ink());
    addAndMakeVisible (keyboard);

    proc.onPatchChanged = [this] { refresh(); };
    refresh();

    setSize (720, 400);
    setResizable (true, true);
    setResizeLimits (540, 380, 1600, 900);
}

GambleSynthEditor::~GambleSynthEditor()
{
    proc.onPatchChanged = nullptr;
    rollButton.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void GambleSynthEditor::applyTypedSeed()
{
    const juce::String t = seedEditor.getText().trim();
    if (t.isEmpty())
        return;

    // 777 — jackpot. Toggles the developer panel instead of rolling.
    if (t.getIntValue() == 777)
    {
        setDevMode (! devMode);
        seedEditor.clear();
        return;
    }

    proc.rollSeed ((unsigned) t.getIntValue());
}

void GambleSynthEditor::setDevMode (bool shouldBeOn)
{
    if (devMode == shouldBeOn)
        return;

    devMode = shouldBeOn;

    if (devMode)
    {
        normalHeight = getHeight();
        devPanel = std::make_unique<DevPanel> (proc);
        addAndMakeVisible (*devPanel);
        setSize (getWidth(), juce::jmax (getHeight(), 720));
    }
    else
    {
        devPanel.reset();

        // Leaving dev mode drops the locks too — a lock that still shapes every
        // roll while its button is hidden would be an invisible mode.
        for (int reel = 0; reel < NumReels; ++reel)
            proc.setReelLocked (reel, false);

        setSize (getWidth(), normalHeight);
    }

    for (auto* b : lockButtons)
        b->setVisible (devMode);

    refresh();
    resized();
}

void GambleSynthEditor::refresh()
{
    const auto& p = proc.getPatch();

    // With a reel locked the sound is a hybrid, so the seed alone no longer
    // reproduces it — say so rather than showing a code that won't work.
    seedLabel.setText ("SEED " + juce::String (p.seed).paddedLeft ('0', 6)
                           + (proc.anyReelLocked() ? "*" : "")
                           + (p.chaos ? "  CHAOS" : "")
                           + (devMode ? "  DEV" : ""),
                       juce::dontSendNotification);

    if (devPanel != nullptr)
        devPanel->syncFromPatch();

    for (int reel = 0; reel < lockButtons.size(); ++reel)
        lockButtons[reel]->setToggleState (proc.isReelLocked (reel), juce::dontSendNotification);

    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
    loadButton.setButtonText ("LOAD " + juce::String (proc.getNumFavourites()));
    loadButton.setEnabled (proc.getNumFavourites() > 0);
    chaosButton.setToggleState (proc.isChaos(), juce::dontSendNotification);
}

void GambleSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::ground());

    // Title strip
    g.setColour (Theme::ink());
    g.setFont (Theme::mono (17.0f, true));
    Theme::drawSpacedText (g, "GAMBLESYNTH",
                           juce::Rectangle<int> (12, 0, getWidth() / 2, headerHeight), 2.0f,
                           juce::Justification::centredLeft);

    // Hairlines: under the header, above the keyboard, and around the frame.
    g.setColour (Theme::ink());
    g.fillRect (0, headerHeight, getWidth(), 1);
    g.fillRect (0, getHeight() - keyboardHeight() - 1, getWidth(), 1);
    g.drawRect (getLocalBounds(), 1);
}

int GambleSynthEditor::keyboardHeight() const
{
    return devMode ? 112 : juce::jlimit (110, 190, getHeight() / 4);
}

void GambleSynthEditor::resized()
{
    auto r = getLocalBounds().reduced (1);

    keyboard.setBounds (r.removeFromBottom (keyboardHeight()));

    auto header = r.removeFromTop (headerHeight);
    seedLabel.setBounds (header.removeFromRight (230).withTrimmedRight (10));
    r.removeFromTop (1);                     // the hairline drawn in paint()

    r.reduce (12, 10);

    // Row 1: transport-ish controls
    auto row1 = r.removeFromTop (44);
    undoButton.setBounds (row1.removeFromLeft (48));
    row1.removeFromLeft (6);
    redoButton.setBounds (row1.removeFromLeft (48));
    chaosButton.setBounds (row1.removeFromRight (134));
    row1.removeFromRight (10);
    goButton.setBounds (row1.removeFromRight (62));
    row1.removeFromRight (6);
    seedEditor.setBounds (row1.removeFromRight (122));

    r.removeFromTop (8);

    // Row 2: save / load / meter
    auto row2 = r.removeFromTop (40);
    meter.setBounds (row2.removeFromRight (134).reduced (0, 4));
    row2.removeFromRight (10);
    saveButton.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 4));
    row2.removeFromLeft (8);
    loadButton.setBounds (row2);

    // Row 3: the locks — dev mode only, so ROLL keeps the space otherwise
    if (devMode)
    {
        r.removeFromTop (8);
        auto row3 = r.removeFromTop (30);
        const int gap = 5;
        const int lockW = (row3.getWidth() - gap * (lockButtons.size() - 1)) / juce::jmax (1, lockButtons.size());
        for (auto* b : lockButtons)
        {
            b->setBounds (row3.removeFromLeft (lockW));
            row3.removeFromLeft (gap);
        }
    }

    r.removeFromTop (12);

    if (devPanel != nullptr)
    {
        rollButton.setBounds (r.removeFromTop (46).withSizeKeepingCentre (
                                  juce::jmin (r.getWidth(), 240), 46));
        r.removeFromTop (10);
        devPanel->setBounds (r);
        return;
    }

    // ROLL: a modest block centred in what's left, rather than filling it.
    rollButton.setBounds (r.withSizeKeepingCentre (juce::jlimit (170, 300, r.getWidth() / 3), 74));
}
