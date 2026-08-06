#pragma once
#include <JuceHeader.h>

// Arpeggiator. It never invents notes — it re-times the ones being held, so it
// cannot play a wrong one. Only the *pattern* is rolled: order, rate, gate
// length and octave span.
//
// Rewrites a MIDI buffer in place: held notes go in, stepped notes come out.
// Positions are base-rate samples, so this runs before the oversampled path
// doubles them.
class Arpeggiator
{
public:
    enum Mode { Off = 0, Up, Down, UpDown, Random, NumModes };

    void reset()
    {
        held.clear();
        pool.clear();
        stepCountdown = 0.0;
        gateRemaining = -1.0;
        sounding = -1;
        stepIndex = 0;
        rising = true;
    }

    void process (juce::MidiBuffer& midi, int numSamples,
                  double samplesPerStep, int mode, float gate, int octaves)
    {
        samplesPerStep = juce::jmax (16.0, samplesPerStep);
        juce::MidiBuffer out;

        auto iter = midi.begin();
        const auto last = midi.end();
        int pos = 0;

        while (pos < numSamples)
        {
            const int nextEvent = (iter != last)
                                ? juce::jlimit (0, numSamples, (*iter).samplePosition)
                                : numSamples;

            // Walk forward to the next thing that happens: an incoming event,
            // the current note's gate closing, or the next step firing.
            while (pos < nextEvent)
            {
                double until = (double) (nextEvent - pos);
                if (! pool.isEmpty())               until = juce::jmin (until, stepCountdown);
                if (sounding >= 0 && gateRemaining >= 0.0)
                    until = juce::jmin (until, gateRemaining);

                const int adv = juce::jmax (1, (int) std::floor (until));
                const int step = juce::jmin (adv, nextEvent - pos);

                pos += step;
                stepCountdown -= step;
                if (gateRemaining >= 0.0) gateRemaining -= step;

                if (sounding >= 0 && gateRemaining <= 0.0)
                {
                    out.addEvent (juce::MidiMessage::noteOff (1, sounding), pos);
                    sounding = -1;
                    gateRemaining = -1.0;
                }

                if (! pool.isEmpty() && stepCountdown <= 0.0)
                {
                    fireStep (out, pos, samplesPerStep, mode, gate);
                    stepCountdown += samplesPerStep;
                }
            }

            if (iter != last && pos >= nextEvent)
            {
                applyInput ((*iter).getMessage(), octaves);
                ++iter;
            }
        }

        midi.swapWith (out);
    }

    // Silence whatever the arp is holding open, for a roll cut or a mode change.
    void releaseInto (juce::MidiBuffer& out, int samplePos)
    {
        if (sounding >= 0)
        {
            out.addEvent (juce::MidiMessage::noteOff (1, sounding), samplePos);
            sounding = -1;
        }
        gateRemaining = -1.0;
    }

private:
    void applyInput (const juce::MidiMessage& m, int octaves)
    {
        if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            if (! held.contains (n))
            {
                held.add (n);
                held.sort();
            }
            velocity = m.getVelocity();

            // A fresh chord starts on the first step rather than wherever the
            // clock happened to be, so patterns begin where you expect.
            if (held.size() == 1)
            {
                stepIndex = 0;
                rising = true;
                stepCountdown = 0.0;
            }
        }
        else if (m.isNoteOff())
        {
            held.removeFirstMatchingValue (m.getNoteNumber());
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            held.clear();
        }

        rebuildPool (octaves);
    }

    void rebuildPool (int octaves)
    {
        pool.clearQuick();
        const int span = juce::jlimit (1, 3, octaves);
        for (int o = 0; o < span; ++o)
            for (int n : held)
                if (n + o * 12 <= 127)
                    pool.add (n + o * 12);

        if (pool.isEmpty())
            stepIndex = 0;
        else
            stepIndex = juce::jlimit (0, pool.size() - 1, stepIndex);
    }

    void fireStep (juce::MidiBuffer& out, int pos, double samplesPerStep,
                   int mode, float gate)
    {
        if (sounding >= 0)                      // never overlap two arp notes
        {
            out.addEvent (juce::MidiMessage::noteOff (1, sounding), pos);
            sounding = -1;
        }

        const int n = pool.size();
        int idx = juce::jlimit (0, n - 1, stepIndex);

        out.addEvent (juce::MidiMessage::noteOn (1, pool[idx], velocity), pos);
        sounding = pool[idx];
        gateRemaining = samplesPerStep * (double) juce::jlimit (0.05f, 0.98f, gate);

        switch (mode)
        {
            case Down:   stepIndex = (idx == 0) ? n - 1 : idx - 1; break;
            case UpDown:
                if (n <= 1) { stepIndex = 0; break; }
                if (rising) { if (++stepIndex >= n) { stepIndex = n - 2; rising = false; } }
                else        { if (--stepIndex < 0)  { stepIndex = juce::jmin (1, n - 1); rising = true; } }
                break;
            case Random: stepIndex = rng.nextInt (n); break;
            default:     stepIndex = (idx + 1) % n; break;   // Up
        }
    }

    juce::Array<int> held, pool;
    juce::Random rng;
    double stepCountdown = 0.0;
    double gateRemaining = -1.0;
    int    sounding  = -1;
    int    stepIndex = 0;
    bool   rising    = true;
    juce::uint8 velocity = 100;
};
