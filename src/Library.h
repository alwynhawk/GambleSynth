#pragma once
#include <JuceHeader.h>
#include "Patch.h"

// The saved-sound library, kept in a file rather than in plugin state.
//
// Plugin state travels with a DAW project, which means favourites saved in one
// project were invisible in the next and a standalone lost them entirely on
// quit. A sound you won should outlive the session it was won in, so the library
// lives in the user's application data and every instance reads the same file.
class Library
{
public:
    struct Entry
    {
        juce::String name;
        Patch patch;
    };

    Library() { load(); }

    static juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("GambleSynth")
                   .getChildFile (testMode() ? "library.test.dat" : "library.dat");
    }

    // Tests save favourites constantly. Without this they write into the real
    // library, and the user opens the panel to find dozens of sounds they never
    // saved — which is exactly what happened.
    static bool& testMode()
    {
        static bool on = false;
        return on;
    }

    int size() const { return entries.size(); }
    const juce::Array<Entry>& all() const { return entries; }

    const Entry* get (int index) const
    {
        return juce::isPositiveAndBelow (index, entries.size()) ? &entries.getReference (index)
                                                                : nullptr;
    }

    // Returns the index it landed at, so the caller can select it.
    int add (const Patch& p, juce::String name = {})
    {
        if (name.isEmpty())
            name = defaultNameFor (p);

        entries.add ({ name, p });
        save();
        return entries.size() - 1;
    }

    void remove (int index)
    {
        if (! juce::isPositiveAndBelow (index, entries.size()))
            return;

        entries.remove (index);
        save();
    }

    void rename (int index, const juce::String& name)
    {
        if (! juce::isPositiveAndBelow (index, entries.size()) || name.isEmpty())
            return;

        entries.getReference (index).name = name;
        save();
    }

    // Re-read from disk. Another instance may have added something since.
    void refresh() { load(); }

    void save() const
    {
        auto f = file();
        f.getParentDirectory().createDirectory();

        juce::MemoryOutputStream os;
        os.writeInt (1);                       // library format version
        os.writeInt (entries.size());
        for (const auto& e : entries)
        {
            os.writeString (e.name);
            writePatch (os, e.patch);
        }

        // Write beside the target and swap, so a crash mid-write cannot leave a
        // truncated library behind.
        auto tmp = f.getSiblingFile (f.getFileName() + ".tmp");
        if (tmp.replaceWithData (os.getData(), os.getDataSize()))
            tmp.moveFileTo (f);
    }

private:
    static juce::String defaultNameFor (const Patch& p)
    {
        juce::String n = p.archetypeName;
        if (p.modifierName.isNotEmpty())
            n << " " << p.modifierName;
        if (p.chaos)
            n = "Chaos " + n;
        return n + " " + juce::String (p.seed).paddedLeft ('0', 6);
    }

    void load()
    {
        entries.clearQuick();

        auto f = file();
        if (! f.existsAsFile())
            return;

        juce::MemoryBlock data;
        if (! f.loadFileAsData (data))
            return;

        juce::MemoryInputStream is (data, false);
        if (is.getTotalLength() < 8)
            return;

        is.readInt();                          // version
        const int n = is.readInt();

        // A corrupt or truncated file should cost the entries it cannot read,
        // never the ones before them.
        for (int k = 0; k < n && ! is.isExhausted(); ++k)
        {
            Entry e;
            e.name  = is.readString();
            e.patch = readPatch (is);
            entries.add (std::move (e));
        }
    }

    juce::Array<Entry> entries;
};
