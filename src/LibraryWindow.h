#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

// The saved-sound library, as a translucent panel that floats over the cabinet.
//
// A DocumentWindow would be a second OS window, which plugin hosts handle badly
// and which cannot be transparent on every platform. This is a child component
// that covers the editor instead: it can be see-through, it travels with the
// plugin window, and it costs the host nothing.
class LibraryWindow : public juce::Component,
                      private juce::ListBoxModel
{
public:
    LibraryWindow (GambleSynthProcessor& p, std::function<void()> onClose)
        : proc (p), closeFn (std::move (onClose))
    {
        list.setModel (this);
        list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId,    juce::Colours::transparentBlack);
        addAndMakeVisible (list);

        auto button = [this] (juce::TextButton& b, const char* text)
        {
            b.setButtonText (text);
            b.setLookAndFeel (&lnf);
            addAndMakeVisible (b);
        };
        button (loadButton,   "LOAD");
        button (renameButton, "RENAME");
        button (deleteButton, "DELETE");
        button (closeButton,  "CLOSE");

        loadButton.onClick   = [this] { loadSelected(); };
        deleteButton.onClick = [this] { deleteSelected(); };
        renameButton.onClick = [this] { renameSelected(); };
        closeButton.onClick  = [this] { if (closeFn) closeFn(); };

        refresh();
    }

    ~LibraryWindow() override
    {
        for (auto* b : { &loadButton, &renameButton, &deleteButton, &closeButton })
            b->setLookAndFeel (nullptr);
    }

    void refresh()
    {
        // Another instance may have saved something since this was last shown.
        proc.getLibrary().refresh();
        list.updateContent();
        list.repaint();
        updateButtons();
    }

    void paint (juce::Graphics& g) override
    {
        // Translucent, so the machine stays visible behind it.
        g.fillAll (juce::Colours::black.withAlpha (0.82f));

        auto r = getLocalBounds();
        g.setColour (juce::Colours::white);
        g.drawRect (r, 2);

        auto header = r.removeFromTop (headerH);
        g.setFont (Theme::mono (16.0f, true));
        Theme::drawOutlined (g, "SAVED SOUNDS", header.reduced (12, 0),
                             juce::Justification::centredLeft, Theme::mono (16.0f, true));

        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.fillRect (r.getX(), header.getBottom(), r.getWidth(), 1);

        if (proc.getLibrary().size() == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.setFont (Theme::mono (13.0f));
            g.drawText ("Nothing saved yet - hit SAVE on a sound you like",
                        getLocalBounds(), juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        r.removeFromTop (headerH);

        auto footer = r.removeFromBottom (40).reduced (8, 6);
        const int w = footer.getWidth() / 4;
        loadButton.setBounds   (footer.removeFromLeft (w).reduced (3, 0));
        renameButton.setBounds (footer.removeFromLeft (w).reduced (3, 0));
        deleteButton.setBounds (footer.removeFromLeft (w).reduced (3, 0));
        closeButton.setBounds  (footer.reduced (3, 0));

        list.setBounds (r.reduced (8, 4));
    }

private:
    // ---- ListBoxModel ----
    int getNumRows() override { return proc.getLibrary().size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                           bool selected) override
    {
        const auto* e = proc.getLibrary().get (row);
        if (e == nullptr) return;

        if (selected)
        {
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillRect (0, 0, width, height);
        }

        g.setColour (juce::Colours::white);
        g.setFont (Theme::mono (13.0f, selected));
        g.drawText (e->name, 8, 0, width - 90, height, juce::Justification::centredLeft, true);

        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (Theme::mono (11.0f));
        g.drawText (juce::String (e->patch.seed).paddedLeft ('0', 6),
                    width - 80, 0, 72, height, juce::Justification::centredRight, false);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        proc.loadFavourite (row);
    }

    void selectedRowsChanged (int) override { updateButtons(); }

    void deleteKeyPressed (int row) override
    {
        proc.getLibrary().remove (row);
        refresh();
    }

    void returnKeyPressed (int row) override { proc.loadFavourite (row); }

    // ---- actions ----
    void loadSelected()
    {
        const int row = list.getSelectedRow();
        if (row >= 0) proc.loadFavourite (row);
    }

    void deleteSelected()
    {
        const int row = list.getSelectedRow();
        if (row < 0) return;

        proc.getLibrary().remove (row);
        refresh();
        list.selectRow (juce::jmin (row, proc.getLibrary().size() - 1));
    }

    void renameSelected()
    {
        const int row = list.getSelectedRow();
        const auto* e = proc.getLibrary().get (row);
        if (e == nullptr) return;

        // Edit in place: an alert window would be a second OS window, which is
        // the thing this panel exists to avoid.
        editor = std::make_unique<juce::TextEditor>();
        editor->setText (e->name);
        editor->selectAll();
        editor->setFont (Theme::mono (13.0f));
        editor->setColour (juce::TextEditor::backgroundColourId, juce::Colours::black);
        editor->setColour (juce::TextEditor::textColourId,       juce::Colours::white);
        editor->setColour (juce::TextEditor::outlineColourId,    juce::Colours::white);
        editor->setBounds (list.getRowPosition (row, true)
                               .withX (list.getX()).withWidth (list.getWidth())
                               .translated (0, list.getY()));

        editor->onReturnKey = [this, row]
        {
            proc.getLibrary().rename (row, editor->getText().trim());
            editor.reset();
            refresh();
        };
        editor->onEscapeKey = [this] { editor.reset(); };
        editor->onFocusLost = [this] { editor.reset(); };

        addAndMakeVisible (*editor);
        editor->grabKeyboardFocus();
    }

    void updateButtons()
    {
        const bool has = list.getSelectedRow() >= 0;
        loadButton.setEnabled (has);
        renameButton.setEnabled (has);
        deleteButton.setEnabled (has);
    }

    static constexpr int headerH = 34;

    GambleSynthProcessor& proc;
    std::function<void()> closeFn;
    juce::ListBox list;
    juce::TextButton loadButton, renameButton, deleteButton, closeButton;
    std::unique_ptr<juce::TextEditor> editor;
    SkinnedLookAndFeel lnf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryWindow)
};
