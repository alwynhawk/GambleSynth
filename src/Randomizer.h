#pragma once
#include <JuceHeader.h>
#include "Patch.h"
#include <random>

// The heart of GambleSynth: never rolls raw parameters. It picks a musical
// archetype, then samples every parameter from that archetype's tuned range,
// so every "pull of the lever" sounds intentional but always different.
class Randomizer
{
public:
    enum Archetype { Pad, Pluck, Bass, Lead, Bell, Stab, Keys, NumArchetypes };

    // Public API: a roll is fully determined by its seed, so the same seed
    // always reproduces the same sound (given the same algorithm version).
    //
    // A free roll additionally refuses to land on the last two archetypes or to
    // repeat the last modifier — it does that by *throwing the seed away and
    // drawing another*, never by altering the sound a seed maps to.
    Patch roll()
    {
        Patch p = rollNormal (nextSeed());
        for (int tries = 0; tries < 12 && tooSimilar (p); ++tries)
            p = rollNormal (nextSeed());
        return p;
    }

    Patch roll (unsigned seed)     { return rollNormal (seed); }
    Patch rollChaos()              { return rollChaosSeed (nextSeed()); }
    Patch rollChaos (unsigned seed){ return rollChaosSeed (seed); }

    // Called once the caller commits to a roll. Rolls that get thrown away
    // downstream (the audibility probe rejects some) must NOT count as "recent",
    // or the no-repeat rule ends up comparing against a sound nobody heard.
    void accept (const Patch& p)
    {
        recentArch[1] = recentArch[0];
        recentArch[0] = p.archetypeName;
        if (p.modifierName.isNotEmpty())
            recentMod = p.modifierName;
    }

private:
    juce::String recentArch[2];   // last two archetypes, freshest first
    juce::String recentMod;       // last modifier that actually applied

    bool tooSimilar (const Patch& p) const
    {
        return p.archetypeName == recentArch[0]
            || p.archetypeName == recentArch[1]
            || (p.modifierName.isNotEmpty() && p.modifierName == recentMod);
    }

    std::mt19937 rng;                                   // seeded per roll
    std::mt19937 seedGen { std::random_device{}() };    // picks fresh seeds
    unsigned nextSeed() { return seedGen() % 1000000u; } // 6-digit, easy to share

    Patch rollNormal (unsigned seed)
    {
        rng.seed (seed);
        const int archetype = (int) (rng() % (unsigned) NumArchetypes);

        Patch p;
        setThreeOsc (p, 2, 2, 0);           // sane default
        p.master = f (0.7f, 0.85f);
        p.resonance = f (0.05f, 0.25f);
        p.stereoWidth = f (0.4f, 0.85f);    // never dead-centre mono
        p.chorusMix   = f (0.1f, 0.3f);     // a little ensemble by default

        switch (archetype)
        {
            case Pad:   makePad (p);   p.archetypeName = "Pad";   break;
            case Pluck: makePluck (p); p.archetypeName = "Pluck"; break;
            case Bass:  makeBass (p);  p.archetypeName = "Bass";  break;
            case Lead:  makeLead (p);  p.archetypeName = "Lead";  break;
            case Bell:  makeBell (p);  p.archetypeName = "Bell";  break;
            case Stab:  makeStab (p);  p.archetypeName = "Stab";  break;
            default:    makeKeys (p);  p.archetypeName = "Keys";  break;
        }
        applyModifier (p);

        // Delay on the grid unless the roll wants it loose. Resolved against the
        // host tempo at play time (120 BPM in the standalone).
        if (p.delayMix > 0.001f)
            p.delaySyncDiv = chance (0.8f) ? i (1, 7) : 0;

        p.seed = (int) seed; p.chaos = false;
        return p;
    }

    // ---- Modifiers: a twist applied *after* the archetype. This is what stops
    // rolls reading as "one of seven presets" — the ear latches onto the twist
    // rather than the family underneath. Rolled from the same seeded stream, so
    // a seed still maps to exactly one sound. ----
    void applyModifier (Patch& p)
    {
        if (! chance (0.60f)) return;

        const int first = i (0, 17);
        p.modifierName = applyOne (p, first);

        // Occasionally a second, non-cancelling twist — the rolls people screenshot.
        if (chance (0.18f))
        {
            const int second = i (0, 17);
            if (second != first && ! cancels (first, second))
                p.modifierName += "+" + applyOne (p, second);
        }
    }

    // Pairs that fight over the same parameter: the second would simply erase
    // the first, so the roll would sound like it only got one twist.
    static bool cancels (int a, int b)
    {
        auto pair = [a, b] (int x, int y) { return (a == x && b == y) || (a == y && b == x); };
        return pair (11, 12)    // cathedral vs dry
            || pair (1, 8)      // gated vs tape (both own the LFO)
            || pair (5, 6)      // vowel vs metal (both own the filter model)
            || pair (9, 10)     // clang vs growl (both own the osc mode)
            || pair (15, 16);   // phase vs flange (two sweeps just smear each other)
    }

    juce::String applyOne (Patch& p, int which)
    {
        switch (which)
        {
            case 0:   // octave stack — a layer an octave (or a twelfth) away
                p.osc[2].semi  = chance (0.6f) ? (chance (0.5f) ? 12 : -12) : 19;
                p.osc[2].level = f (0.35f, 0.7f);
                p.osc[2].fine  = f (-8.0f, 8.0f);
                return "octave";

            case 1:   // gated tremolo — rhythmic chop, locked to the grid so it
                      // lands as a trance gate instead of a wobble against the beat
                p.lfoDest = 2; p.lfoRate = f (6.0f, 14.0f); p.lfoDepth = f (0.6f, 1.0f);
                p.gateSyncDiv = i (1, 3);          // 1/16, 1/8T or 1/8
                return "gated";

            case 2:   // reverse swell — fades in, hangs on; kills any percussive identity
                p.ampA = f (0.6f, 1.6f); p.ampS = f (0.7f, 0.95f); p.ampR = f (0.8f, 2.2f);
                p.modA = f (0.5f, 1.4f); p.modS = f (0.5f, 0.9f);
                return "swell";

            case 3:   // sub drop — serious low weight, filter pulled down to match
                p.subWave = chance (0.5f) ? 3 : 0;
                p.subLevel = f (0.45f, 0.75f);
                p.cutoff *= f (0.5f, 0.75f);
                return "sub";

            case 4:   // air — breathy noise bed
                p.noiseLevel = f (0.12f, 0.3f);
                p.cutoff *= f (1.1f, 1.6f);
                return "air";

            case 5:   // vowel — formant filter, wherever the archetype had landed
                p.filterModel = 3; p.filterMorph = f (0.0f, 1.0f);
                p.resonance = f (0.3f, 0.6f);
                return "vowel";

            case 6:   // metallic — tuned comb, struck / plucked ring
                p.filterModel = 4; p.filterMorph = f (0.25f, 0.85f);
                p.keytrack = f (0.85f, 1.0f); p.filterEnvAmt = f (0.0f, 0.2f);
                return "metal";

            case 7:   // drift — wide, seasick supersaw
                p.unisonVoices = chance (0.5f) ? 5 : 7;
                p.unisonDetune = f (22.0f, 42.0f);
                p.stereoWidth  = f (0.8f, 1.0f);
                p.chorusMix    = f (0.4f, 0.7f);
                return "drift";

            case 8:   // broken tape — slow deep pitch warble
                p.lfoDest = 0; p.lfoRate = f (0.15f, 0.8f); p.lfoDepth = f (0.5f, 1.0f);
                return "tape";

            case 9:   // clang — ring mod against an inharmonic partner
                p.oscMode = 1; p.fmAmount = f (0.5f, 0.85f);
                setRatio (p.osc[1], f (1.3f, 9.0f));
                return "clang";

            case 10:  // growl — hard FM index, inharmonic modulator
                p.oscMode = 3; p.fmAmount = f (0.5f, 0.9f);
                setRatio (p.osc[1], f (1.2f, 5.0f));
                return "growl";

            case 11:  // cathedral — huge space
                p.reverbSize = f (0.85f, 0.98f); p.reverbMix = f (0.45f, 0.6f);
                p.delayMix   = f (0.25f, 0.45f); p.delayFb   = f (0.45f, 0.7f);
                p.delayTime  = f (0.35f, 0.7f);
                return "cathedral";

            case 13:  // crush — lo-fi bit / rate reduction
                p.crushBits = f (3.0f, 7.0f);
                p.crushRate = chance (0.6f) ? f (2.0f, 12.0f) : 1.0f;
                return "crush";

            case 14:  // fold — wavefolder harmonics, not just clipping
                p.foldAmount = f (0.35f, 0.8f);
                p.drive = juce::jmax (p.drive, f (0.2f, 0.5f));
                return "fold";

            case 15:  // phase — deep six-stage sweep
                p.phaserMix = f (0.5f, 0.85f); p.phaserRate = f (0.1f, 0.8f);
                p.phaserDepth = f (0.6f, 1.0f); p.phaserFb = f (0.4f, 0.7f);
                return "phase";

            case 16:  // flange — jet whoosh
                p.flangerMix = f (0.45f, 0.7f); p.flangerRate = f (0.05f, 0.5f);
                p.flangerDepth = f (0.6f, 1.0f); p.flangerFb = f (0.5f, 0.85f);
                return "flange";

            case 17:  // squash — heavy glue, pumping
                p.compAmount = f (0.6f, 0.95f);
                return "squash";

            default:  // dry punch — the opposite: no space, more grit, short tail
                p.reverbMix = f (0.0f, 0.05f); p.delayMix = 0.0f; p.chorusMix = f (0.0f, 0.1f);
                p.drive = f (0.35f, 0.7f); p.ampR = juce::jmin (p.ampR, f (0.08f, 0.2f));
                return "dry";
        }
    }

    // CHAOS: no archetype rails. Every parameter rolls wide-open. Most results
    // are ugly/experimental, some are gold — that's the point. Still bounded
    // just enough to avoid NaNs, DC, or blowing the speakers.
    Patch rollChaosSeed (unsigned seed)
    {
        rng.seed (seed);
        Patch p;
        p.oscMode  = i (0, 3);
        p.fmAmount = f (0.0f, 1.0f);

        for (int k = 0; k < 3; ++k)
        {
            p.osc[k].wave  = i (0, 4);
            p.osc[k].semi  = i (-24, 24);
            p.osc[k].fine  = f (-50.0f, 50.0f);
            p.osc[k].level = f (0.3f, 1.0f);
        }

        p.filterType   = i (0, 2);
        p.filterModel  = i (0, 4);          // any flavour, no archetype gate
        p.filterPoles  = chance (0.5f) ? 4 : 2;
        p.filterMorph  = f (0.0f, 1.0f);
        p.cutoff       = std::pow (2.0f, f (std::log2 (80.0f), std::log2 (11000.0f))); // log-uniform
        p.resonance    = f (0.0f, 0.9f);
        p.filterEnvAmt = f (-1.0f, 1.0f);
        p.keytrack     = f (0.0f, 1.0f);

        p.ampA = f (0.001f, 1.5f); p.ampD = f (0.05f, 2.0f); p.ampS = f (0.0f, 1.0f); p.ampR = f (0.05f, 3.0f);
        p.modA = f (0.001f, 1.5f); p.modD = f (0.05f, 2.0f); p.modS = f (0.0f, 1.0f); p.modR = f (0.05f, 2.0f);

        p.lfoDest = i (0, 2); p.lfoRate = f (0.05f, 15.0f); p.lfoDepth = f (0.0f, 1.0f);

        p.drive       = f (0.0f, 1.0f);
        p.delayMix    = chance (0.6f) ? f (0.0f, 0.5f) : 0.0f;
        p.delayTime   = f (0.05f, 0.8f); p.delayFb = f (0.0f, 0.85f);
        p.reverbSize  = f (0.0f, 0.95f); p.reverbMix = f (0.0f, 0.6f);
        p.chorusMix   = f (0.0f, 0.7f);
        p.stereoWidth = f (0.0f, 1.0f);
        p.unisonVoices = i (1, 7); p.unisonDetune = f (0.0f, 40.0f);
        p.subWave = chance (0.5f) ? 3 : 0; p.subLevel = f (0.0f, 0.6f); p.noiseLevel = f (0.0f, 0.4f);
        p.pulseWidth = f (0.1f, 0.9f); p.pwmDepth = f (0.0f, 0.8f);
        p.velToFilter = f (0.0f, 1.0f); p.velToAmp = f (0.0f, 1.0f);
        p.voiceMode = i (0, 2); p.glideTime = chance (0.5f) ? f (0.0f, 0.3f) : 0.0f;
        p.foldAmount = chance (0.4f) ? f (0.0f, 0.9f) : 0.0f;
        p.crushBits  = chance (0.4f) ? f (2.0f, 12.0f) : 16.0f;
        p.crushRate  = chance (0.4f) ? f (1.0f, 20.0f) : 1.0f;
        p.phaserMix  = chance (0.4f) ? f (0.2f, 0.65f) : 0.0f;   // full-wet can null the dry
        p.phaserRate = f (0.05f, 4.0f); p.phaserDepth = f (0.2f, 1.0f); p.phaserFb = f (0.0f, 0.8f);
        p.flangerMix = chance (0.4f) ? f (0.2f, 0.6f) : 0.0f;
        p.flangerRate= f (0.03f, 3.0f); p.flangerDepth= f (0.2f, 1.0f); p.flangerFb= f (0.0f, 0.85f);
        p.compAmount = chance (0.5f) ? f (0.0f, 0.9f) : 0.0f;
        p.delaySyncDiv = chance (0.5f) ? i (1, 7) : 0;
        p.master      = f (0.6f, 0.85f); // master limiter still protects output

        p.archetypeName = "Chaos";
        p.seed = (int) seed; p.chaos = true;
        return p;
    }

    float f (float lo, float hi)
    {
        std::uniform_real_distribution<float> d (lo, hi);
        return d (rng);
    }
    int   i (int lo, int hi) { std::uniform_int_distribution<int> d (lo, hi); return d (rng); }
    bool  chance (float p)   { return f (0.0f, 1.0f) < p; }

    // Tier-B filter flavours: each archetype approves its own set of models with
    // a probability; whatever probability is left over keeps the clean SVF, so
    // most rolls stay familiar and the flavours land as a surprise.
    void pickFilter (Patch& p, float ladder, float diode, float formant, float comb)
    {
        const float r = f (0.0f, 1.0f);
        float acc = ladder;
        if (r < acc)
        {
            p.filterModel = 1; p.filterPoles = chance (0.7f) ? 4 : 2;   // creamy Moog-ish
            return;
        }
        acc += diode;
        if (r < acc)
        {
            p.filterModel = 2; p.filterPoles = 4;                        // acid squelch
            p.resonance = juce::jmax (p.resonance, f (0.4f, 0.7f));
            p.filterEnvAmt = juce::jmax (p.filterEnvAmt, f (0.4f, 0.8f));
            return;
        }
        acc += formant;
        if (r < acc)
        {
            p.filterModel = 3; p.filterMorph = f (0.0f, 1.0f);           // vowel
            p.filterType = 0;                                            // type is unused here
            return;
        }
        acc += comb;
        if (r < acc)
        {
            p.filterModel = 4; p.filterMorph = f (0.25f, 0.85f);         // metallic / plucked
            p.keytrack = f (0.85f, 1.0f);                                // comb must track pitch
            p.filterEnvAmt = f (0.0f, 0.2f);
            return;
        }
        p.filterModel = 0;
        p.filterPoles = chance (0.3f) ? 4 : 2;                           // occasional steeper SVF
    }

    // Character FX, same weighted-set idea as pickFilter: leftover probability
    // means the roll keeps a clean chain. Drive and glue are rolled separately
    // because every patch wants *some* of those.
    void pickFx (Patch& p, float phaser, float flanger, float crush, float fold)
    {
        const float r = f (0.0f, 1.0f);
        float acc = phaser;
        if (r < acc)
        {
            p.phaserMix   = f (0.3f, 0.7f);  p.phaserRate = f (0.15f, 1.2f);
            p.phaserDepth = f (0.4f, 1.0f);  p.phaserFb   = f (0.2f, 0.6f);
            return;
        }
        acc += flanger;
        if (r < acc)
        {
            p.flangerMix   = f (0.3f, 0.6f); p.flangerRate = f (0.08f, 0.6f);
            p.flangerDepth = f (0.4f, 1.0f); p.flangerFb   = f (0.3f, 0.7f);
            return;
        }
        acc += crush;
        if (r < acc)
        {
            p.crushBits = f (4.0f, 10.0f);
            p.crushRate = chance (0.5f) ? f (2.0f, 8.0f) : 1.0f;
            return;
        }
        acc += fold;
        if (r < acc)
            p.foldAmount = f (0.2f, 0.6f);
    }

    // FM, ring and sync timbre is decided by the *frequency ratio* between the
    // two oscillators — so it has to be continuous. Picking from a few integer
    // semitones gives you a few recognisable sounds and nothing in between.
    // semi + fine encodes any ratio exactly (fine is cents).
    static void setRatio (Patch::Osc& o, float ratio)
    {
        const float semis = 12.0f * std::log2 (juce::jmax (0.05f, ratio));
        o.semi = juce::roundToInt (semis);
        o.fine = (semis - (float) o.semi) * 100.0f;
    }

    void setThreeOsc (Patch& p, int w0, int w1, int w2)
    {
        // Wider detune spread = lusher, more beating between oscillators.
        p.osc[0] = { w0, 0,  f (-6.0f,  6.0f),  f (0.6f, 0.9f) };
        p.osc[1] = { w1, 0,  f (-14.0f, 14.0f), f (0.5f, 0.8f) };
        p.osc[2] = { w2, 0,  f (-10.0f, 10.0f), f (0.4f, 0.7f) };
    }

    // ---- archetypes -------------------------------------------------------
    void makePad (Patch& p)
    {
        setThreeOsc (p, 2, 2, chance (0.5f) ? 1 : 2);
        p.osc[2].semi = chance (0.5f) ? 12 : -12;
        p.ampA = f (0.4f, 1.4f); p.ampD = f (0.3f, 0.8f); p.ampS = f (0.7f, 0.95f); p.ampR = f (0.6f, 2.0f);
        p.filterType = 0;
        p.cutoff = f (500.f, 2500.f);
        p.filterEnvAmt = f (0.1f, 0.5f);
        p.modA = f (0.4f, 1.2f); p.modD = f (0.5f, 1.5f); p.modS = f (0.4f, 0.8f);
        p.lfoDest = 1; p.lfoRate = f (0.1f, 1.5f); p.lfoDepth = f (0.1f, 0.4f);
        p.reverbSize = f (0.6f, 0.9f); p.reverbMix = f (0.3f, 0.5f);
        p.delayMix = chance (0.4f) ? f (0.1f, 0.25f) : 0.0f; p.delayTime = f (0.3f, 0.6f); p.delayFb = f (0.2f, 0.4f);
        p.stereoWidth = f (0.7f, 0.95f); p.chorusMix = f (0.35f, 0.6f);      // wide & lush
        if (chance (0.35f)) { p.oscMode = 1; p.fmAmount = f (0.2f, 0.5f); }  // slow ring shimmer
        p.unisonVoices = chance (0.5f) ? 5 : 7; p.unisonDetune = f (8.0f, 22.0f);
        p.subLevel = f (0.0f, 0.2f); p.noiseLevel = f (0.0f, 0.05f);
        p.pulseWidth = f (0.3f, 0.7f); p.pwmDepth = f (0.05f, 0.35f);
        p.velToAmp = f (0.15f, 0.45f); p.velToFilter = f (0.2f, 0.5f);
        pickFilter (p, 0.20f, 0.0f, 0.18f, 0.0f);
        p.drive = f (0.0f, 0.15f); p.compAmount = f (0.0f, 0.25f);
        pickFx (p, 0.28f, 0.12f, 0.0f, 0.05f);   // vowel pads are a highlight
    }

    void makePluck (Patch& p)
    {
        setThreeOsc (p, 2, 3, 2);
        p.ampA = f (0.001f, 0.01f); p.ampD = f (0.15f, 0.5f); p.ampS = f (0.0f, 0.2f); p.ampR = f (0.1f, 0.3f);
        p.filterType = 0;
        p.cutoff = f (400.f, 1400.f);
        p.filterEnvAmt = f (0.4f, 0.8f);
        p.modA = 0.002f; p.modD = f (0.1f, 0.4f); p.modS = f (0.0f, 0.2f);
        p.resonance = f (0.2f, 0.5f);
        p.reverbMix = f (0.15f, 0.35f); p.reverbSize = f (0.4f, 0.7f);
        p.delayMix = chance (0.6f) ? f (0.15f, 0.35f) : 0.0f; p.delayTime = f (0.2f, 0.45f); p.delayFb = f (0.25f, 0.45f);
        if (chance (0.3f))  p.filterType = 2;                               // sometimes HP = thinner
        if (chance (0.35f)) { p.oscMode = 2; p.fmAmount = f (0.2f, 0.6f); } // sync zap
        p.unisonVoices = chance (0.4f) ? 3 : 1; p.unisonDetune = f (5.0f, 15.0f);
        p.subLevel = f (0.05f, 0.25f); p.noiseLevel = f (0.02f, 0.12f);
        p.velToAmp = f (0.4f, 0.8f); p.velToFilter = f (0.4f, 0.8f);
        pickFilter (p, 0.20f, 0.10f, 0.0f, 0.18f);
        p.drive = f (0.05f, 0.3f); p.compAmount = f (0.1f, 0.4f);
        pickFx (p, 0.10f, 0.12f, 0.15f, 0.05f);  // comb = karplus-ish pluck
    }

    void makeBass (Patch& p)
    {
        setThreeOsc (p, 2, chance (0.5f) ? 3 : 2, 0);
        p.osc[2].semi = -12;                 // sub
        p.osc[0].fine = f (-2.0f, 2.0f);
        p.ampA = f (0.001f, 0.01f); p.ampD = f (0.1f, 0.4f); p.ampS = f (0.5f, 0.9f); p.ampR = f (0.05f, 0.2f);
        p.filterType = 0;
        p.cutoff = f (200.f, 700.f);
        p.filterEnvAmt = f (0.2f, 0.6f);
        p.modA = 0.002f; p.modD = f (0.08f, 0.3f); p.modS = f (0.1f, 0.4f);
        p.resonance = f (0.15f, 0.4f);
        p.reverbMix = f (0.0f, 0.12f); p.reverbSize = 0.3f;
        p.delayMix = 0.0f;
        p.stereoWidth = f (0.1f, 0.35f); p.chorusMix = f (0.0f, 0.15f);      // tight & centred = punch
        if (chance (0.3f)) { p.oscMode = 3; p.fmAmount = f (0.15f, 0.45f);
                            setRatio (p.osc[1], f (1.4f, 3.2f)); }                  // FM growl
        else if (chance (0.3f)) { p.oscMode = 1; p.fmAmount = f (0.3f, 0.6f); }                 // ring dirt
        p.unisonVoices = 1;                                                    // tight = punch
        p.subWave = chance (0.4f) ? 3 : 0; p.subLevel = f (0.3f, 0.6f); p.noiseLevel = f (0.0f, 0.04f);
        p.velToAmp = f (0.2f, 0.5f); p.velToFilter = f (0.3f, 0.6f);
        if (chance (0.6f)) { p.voiceMode = chance (0.5f) ? 1 : 2; p.glideTime = chance (0.6f) ? f (0.02f, 0.12f) : 0.0f; }
        pickFilter (p, 0.35f, 0.25f, 0.0f, 0.0f);
        p.drive = f (0.15f, 0.45f); p.compAmount = f (0.2f, 0.5f);
        pickFx (p, 0.05f, 0.05f, 0.15f, 0.15f);   // ladder weight + acid
    }

    void makeLead (Patch& p)
    {
        setThreeOsc (p, 2, 2, chance (0.5f) ? 3 : 2);
        p.ampA = f (0.01f, 0.15f); p.ampD = f (0.1f, 0.3f); p.ampS = f (0.6f, 0.9f); p.ampR = f (0.15f, 0.5f);
        p.filterType = 0;
        p.cutoff = f (1200.f, 4500.f);
        p.filterEnvAmt = f (0.1f, 0.4f);
        p.lfoDest = 0; p.lfoRate = f (4.0f, 7.0f); p.lfoDepth = f (0.15f, 0.45f); // vibrato
        p.reverbMix = f (0.15f, 0.35f); p.reverbSize = f (0.4f, 0.7f);
        p.delayMix = chance (0.7f) ? f (0.2f, 0.4f) : 0.0f; p.delayTime = f (0.25f, 0.5f); p.delayFb = f (0.3f, 0.5f);
        if (chance (0.4f)) { p.oscMode = 2; p.fmAmount = f (0.15f, 0.5f); } // sync lead
        p.unisonVoices = chance (0.5f) ? 3 : 5; p.unisonDetune = f (6.0f, 16.0f);
        p.subLevel = f (0.0f, 0.15f); p.noiseLevel = 0.0f;
        p.pulseWidth = f (0.35f, 0.65f); p.pwmDepth = f (0.1f, 0.4f);
        p.velToAmp = f (0.2f, 0.5f); p.velToFilter = f (0.2f, 0.5f);
        if (chance (0.5f)) { p.voiceMode = chance (0.4f) ? 1 : 2; p.glideTime = chance (0.7f) ? f (0.03f, 0.15f) : 0.0f; }
        pickFilter (p, 0.30f, 0.20f, 0.08f, 0.0f);
        p.drive = f (0.1f, 0.4f); p.compAmount = f (0.1f, 0.35f);
        pickFx (p, 0.20f, 0.18f, 0.08f, 0.12f);
    }

    void makeBell (Patch& p)
    {
        // A bell is inharmonic partials under a struck envelope, and the
        // character is the carrier:modulator ratio — so that is rolled
        // continuously. Integer ratios sound like an organ; the space between
        // them is where the interesting metal lives.
        setThreeOsc (p, chance (0.25f) ? 1 : 0, 0, chance (0.3f) ? 1 : 0);
        setRatio (p.osc[1], f (1.15f, 7.5f));
        setRatio (p.osc[2], f (2.0f, 9.0f));
        p.osc[2].level = f (0.05f, 0.3f);

        p.oscMode = chance (0.25f) ? 1 : 3;      // ring mod is a different metal
        p.fmAmount = f (0.25f, 0.85f);

        // Struck-and-ringing, but the ring is anything from a tap to a gong.
        p.ampA = f (0.001f, 0.02f); p.ampD = f (0.35f, 2.6f);
        p.ampS = f (0.0f, 0.12f);   p.ampR = f (0.35f, 2.0f);
        p.filterType = 0;
        p.cutoff = f (3000.f, 7000.f);
        p.filterEnvAmt = f (0.0f, 0.2f);
        p.reverbSize = f (0.7f, 0.95f); p.reverbMix = f (0.35f, 0.55f);
        p.delayMix = chance (0.5f) ? f (0.15f, 0.3f) : 0.0f; p.delayTime = f (0.3f, 0.55f); p.delayFb = f (0.25f, 0.45f);
        p.unisonVoices = 1; p.subLevel = 0.0f; p.noiseLevel = 0.0f;          // pure FM
        p.velToAmp = f (0.4f, 0.8f); p.velToFilter = f (0.0f, 0.2f);
        pickFilter (p, 0.10f, 0.0f, 0.0f, 0.15f);
        p.drive = f (0.0f, 0.12f); p.compAmount = f (0.0f, 0.2f);
        pickFx (p, 0.12f, 0.10f, 0.08f, 0.05f);   // comb adds struck-metal ring
    }

    void makeStab (Patch& p)
    {
        setThreeOsc (p, 2, 2, 3);
        p.osc[1].fine = f (-10.0f, 10.0f);   // fat detune
        p.ampA = f (0.001f, 0.02f); p.ampD = f (0.1f, 0.3f); p.ampS = f (0.1f, 0.4f); p.ampR = f (0.1f, 0.3f);
        p.filterType = chance (0.3f) ? 1 : 0;
        p.cutoff = f (700.f, 2500.f);
        p.filterEnvAmt = f (0.3f, 0.7f);
        p.resonance = f (0.2f, 0.45f);
        p.reverbMix = f (0.2f, 0.4f); p.reverbSize = f (0.5f, 0.8f);
        p.delayMix = f (0.2f, 0.4f); p.delayTime = f (0.25f, 0.5f); p.delayFb = f (0.3f, 0.5f);
        if (chance (0.4f)) { p.oscMode = 1; p.fmAmount = f (0.3f, 0.7f); }  // ring-mod stab
        p.unisonVoices = chance (0.5f) ? 5 : 7; p.unisonDetune = f (10.0f, 25.0f);
        p.subLevel = f (0.1f, 0.3f); p.noiseLevel = f (0.02f, 0.1f);
        p.velToAmp = f (0.3f, 0.6f); p.velToFilter = f (0.3f, 0.6f);
        pickFilter (p, 0.25f, 0.10f, 0.12f, 0.0f);
        p.drive = f (0.1f, 0.4f); p.compAmount = f (0.15f, 0.45f);
        pickFx (p, 0.15f, 0.20f, 0.15f, 0.08f);
    }

    void makeKeys (Patch& p)
    {
        setThreeOsc (p, 2, 1, chance (0.5f) ? 0 : 2);
        p.ampA = f (0.005f, 0.05f); p.ampD = f (0.3f, 0.8f); p.ampS = f (0.3f, 0.6f); p.ampR = f (0.2f, 0.5f);
        p.filterType = 0;
        p.cutoff = f (900.f, 3000.f);
        p.filterEnvAmt = f (0.2f, 0.5f);
        p.modD = f (0.3f, 0.7f); p.modS = f (0.1f, 0.4f);
        p.reverbMix = f (0.15f, 0.35f); p.reverbSize = f (0.4f, 0.7f);
        p.delayMix = chance (0.4f) ? f (0.1f, 0.25f) : 0.0f; p.delayTime = f (0.3f, 0.5f); p.delayFb = f (0.2f, 0.4f);
        p.chorusMix = f (0.25f, 0.5f);                                        // e-piano shimmer
        if (chance (0.45f)) { p.oscMode = 3; p.fmAmount = f (0.1f, 0.35f);
                              // near 2:1 is the e-piano; the rest is other keys
                              setRatio (p.osc[1], chance (0.5f) ? f (1.96f, 2.04f) : f (1.2f, 4.5f)); }
        p.unisonVoices = chance (0.4f) ? 3 : 1; p.unisonDetune = f (5.0f, 12.0f);
        p.subLevel = f (0.0f, 0.15f); p.noiseLevel = 0.0f;
        p.velToAmp = f (0.4f, 0.8f); p.velToFilter = f (0.3f, 0.6f);
        pickFilter (p, 0.25f, 0.0f, 0.0f, 0.08f);
        p.drive = f (0.05f, 0.25f); p.compAmount = f (0.1f, 0.35f);
        pickFx (p, 0.25f, 0.10f, 0.05f, 0.05f);
    }
};
