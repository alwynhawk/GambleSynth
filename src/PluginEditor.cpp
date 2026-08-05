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

   #if GAMBLESYNTH_HAS_ASSETS
    skin     = juce::ImageCache::getFromMemory (BinaryData::machine_png, BinaryData::machine_pngSize);
    backdrop = juce::ImageCache::getFromMemory (BinaryData::background_png, BinaryData::background_pngSize);
   #endif

    if (skin.isValid())
    {
        for (int k = 0; k < 3; ++k)
            addAndMakeVisible (reels.add (new ReelDisplay (proc, (ReelDisplay::Kind) k)));

        // Cabinet sits above the keyboard but below every control, so the keys
        // show through the JACKPOT window while buttons stay clickable.
        overlay = std::make_unique<MachineOverlay> (skin);
        addAndMakeVisible (*overlay);
        keyboard.toBack();      // bottom of the stack
        overlay->toBack();      // then the cabinet, just above it
        keyboard.toBack();

        // Over artwork every control is a hotspot, not a slab. The lever becomes
        // the roll trigger and the ROLL button disappears into it.
        setLookAndFeel (&skinned);
        rollButton.setLookAndFeel (&skinned);
        rollButton.setButtonText ({});
        seedLabel.setColour (juce::Label::textColourId, Theme::gold());
        seedLabel.setJustificationType (juce::Justification::centred);
    }

    proc.onPatchChanged = [this] { refresh(); };
    refresh();

    if (skin.isValid())
    {
        // Always opens at the same size, and can only be resized along the
        // artwork's own diagonal — the cabinet never stretches.
        setResizable (true, true);
        setResizeLimits (juce::roundToInt (DefaultHeight * 0.45 * Skin::aspect()),
                         juce::roundToInt (DefaultHeight * 0.45),
                         juce::roundToInt (DefaultHeight * 2.0 * Skin::aspect()),
                         juce::roundToInt (DefaultHeight * 2.0));
        if (auto* c = getConstrainer())
            c->setFixedAspectRatio (Skin::aspect());

        setSize (juce::roundToInt (DefaultHeight * Skin::aspect()), DefaultHeight);
    }
    else
    {
        setSize (720, 400);
        setResizable (true, true);
        setResizeLimits (540, 380, 1600, 900);
    }
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
        normalWidth  = getWidth();
        normalHeight = getHeight();
        devPanel = std::make_unique<DevPanel> (proc);
        addAndMakeVisible (*devPanel);

        if (skin.isValid())
        {
            // Widen to make room for the panel rather than squashing the
            // machine into half the window.
            if (auto* c = getConstrainer()) c->setFixedAspectRatio (0.0);
            setSize (getWidth() * 2, getHeight());
        }
        else
        {
            setSize (getWidth(), juce::jmax (getHeight(), 720));
        }
    }
    else
    {
        devPanel.reset();

        // Leaving dev mode drops the locks too — a lock that still shapes every
        // roll while its button is hidden would be an invisible mode.
        for (int reel = 0; reel < NumReels; ++reel)
            proc.setReelLocked (reel, false);

        setSize (normalWidth > 0 ? normalWidth : getWidth(), normalHeight);
        if (skin.isValid())
            if (auto* c = getConstrainer()) c->setFixedAspectRatio (Skin::aspect());
    }

    for (auto* b : lockButtons)
        b->setVisible (devMode);

    refresh();
    resized();
}

void GambleSynthEditor::refresh()
{
    const auto& p = proc.getPatch();

    // Spin the reels when the sound actually changed. The audio has already
    // switched — this is the flourish catching up, never a thing to wait for.
    if (p.seed != lastShownSeed)
    {
        lastShownSeed = p.seed;
        for (int k = 0; k < reels.size(); ++k)
            reels[k]->startSpin (k);
    }

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

    if (skin.isValid())
    {
        if (backdrop.isValid())
        {
            // Cover the window without distorting: scale to the larger of the
            // two ratios and centre, so the edges crop rather than stretch.
            const float sx = (float) getWidth()  / (float) backdrop.getWidth();
            const float sy = (float) getHeight() / (float) backdrop.getHeight();
            const float scale = juce::jmax (sx, sy);
            const float w = backdrop.getWidth() * scale, h = backdrop.getHeight() * scale;

            g.drawImage (backdrop,
                         juce::Rectangle<float> ((getWidth() - w) * 0.5f,
                                                 (getHeight() - h) * 0.5f, w, h),
                         juce::RectanglePlacement::stretchToFit, false);

        }

        // The cut-outs are holes in the artwork, so without a backing they show
        // raw backdrop and nothing on them is readable.
        {
            const auto& L = Skin::layout();
            g.setColour (juce::Colour (0xff0a0a0a));
            g.fillRect (Skin::place (L.jackpotWindow, artArea));
            g.fillRect (Skin::place (L.coinTray, artArea));
        }

        // The cabinet itself is drawn by the overlay, above the keyboard.

        // Dev mode outlines every hotspot over the art, so a control that has
        // drifted from its hole is obvious instead of subtly wrong.
        if (devMode)
        {
            g.setColour (juce::Colours::lime.withAlpha (0.75f));
            for (auto* c : getChildren())
                if (c->isVisible() && c != devPanel.get())
                    g.drawRect (c->getBounds(), 1);
        }
        return;
    }

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
    if (skin.isValid())
    {
        auto bounds = getLocalBounds();

        // The dev panel takes the right-hand half; the machine keeps the rest.
        if (devPanel != nullptr)
        {
            devPanel->setBounds (bounds.removeFromRight (bounds.getWidth() / 2));
            bounds.removeFromRight (6);
        }

        artArea = Skin::fitArtwork (bounds);
        if (overlay != nullptr) overlay->setBounds (artArea);
        layoutFromSkin (artArea);
        return;
    }

    layoutPlain();
}

void GambleSynthEditor::layoutFromSkin (juce::Rectangle<int> art)
{
    const auto& L = Skin::layout();
    auto at = [art] (juce::Rectangle<float> norm) { return Skin::place (norm, art); };

    seedLabel.setBounds   (at (L.seedDisplay));
    seedEditor.setBounds  (at (L.seedEntry));
    goButton.setBounds    (at (L.go));
    undoButton.setBounds  (at (L.undo));
    redoButton.setBounds  (at (L.redo));
    saveButton.setBounds  (at (L.save));
    loadButton.setBounds  (at (L.load));
    chaosButton.setBounds (at (L.chaos));
    meter.setBounds       (at (L.meter));
    rollButton.setBounds  (at (L.lever));      // the lever *is* the roll button
    keyboard.setBounds    (at (L.keyboard));   // shows through the JACKPOT window

    for (int k = 0; k < reels.size(); ++k)
        reels[k]->setBounds (at (L.reel (k)));

    // HOLD sits under its own window, and only exists in dev mode for now.
    for (int k = 0; k < lockButtons.size() && k < 3; ++k)
        lockButtons[k]->setBounds (at (L.hold (k)));
    for (int k = 3; k < lockButtons.size(); ++k)
        lockButtons[k]->setBounds ({});          // reels 4 and 5 have no window

    // Three octaves across the window, whatever size it is drawn at.
    keyboard.setKeyWidth (juce::jmax (10.0f, (float) keyboard.getWidth() / 21.0f));
}

void GambleSynthEditor::layoutPlain()
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
