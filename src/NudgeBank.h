#pragma once
#include <JuceHeader.h>
#include "Fruit.h"

// Nudges are won, not given. Three matching fruit pay 2, three sevens pay 20,
// and a nudge costs one — so the reels finally decide something instead of just
// spinning.
//
// Kept in a file beside the saved-sound library rather than in plugin state: a
// balance that reset every time you opened a different project would not feel
// like a balance at all.
class NudgeBank
{
public:
    // Three sevens land about once in 240 pulls, three fruit about once in 48.
    static constexpr int FruitPayout   = 2;
    static constexpr int JackpotPayout = 20;

    // Starting with nothing means the average player is 48 pulls from finding
    // out the button exists, so it opens with enough to be discovered.
    static constexpr int StartingBalance = 5;

    NudgeBank() { load(); }

    static juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("GambleSynth")
                   .getChildFile (testMode() ? "nudges.test.dat" : "nudges.dat");
    }

    // Tests roll constantly; they must not spend or bank a real balance.
    static bool& testMode()
    {
        static bool on = false;
        return on;
    }

    int balance() const { return credits; }
    bool canSpend() const { return credits > 0; }

    bool spend()
    {
        if (credits <= 0)
            return false;

        --credits;
        save();
        return true;
    }

    // Returns what this spin paid, so the UI can say so.
    int award (const FruitSpin& spin)
    {
        if (! spin.isJackpot())
            return 0;

        const int paid = (spin.symbol[0] == Fruit::Seven) ? JackpotPayout : FruitPayout;
        credits += paid;
        save();
        return paid;
    }

    void save() const
    {
        auto f = file();
        f.getParentDirectory().createDirectory();

        juce::MemoryOutputStream os;
        os.writeInt (1);            // format version
        os.writeInt (credits);

        auto tmp = f.getSiblingFile (f.getFileName() + ".tmp");
        if (tmp.replaceWithData (os.getData(), os.getDataSize()))
            tmp.moveFileTo (f);
    }

private:
    void load()
    {
        auto f = file();
        if (! f.existsAsFile())
        {
            credits = StartingBalance;
            return;
        }

        juce::MemoryBlock data;
        if (! f.loadFileAsData (data) || data.getSize() < 8)
        {
            credits = StartingBalance;
            return;
        }

        juce::MemoryInputStream is (data, false);
        is.readInt();               // version
        credits = juce::jmax (0, is.readInt());
    }

    int credits = StartingBalance;
};
