#pragma once
#include <JuceHeader.h>
#include "Patch.h"
#include "Wavetables.h"
#include <random>

// The heart of GambleSynth: never rolls raw parameters. It picks a musical
// archetype, then samples every parameter from that archetype's tuned range,
// so every "pull of the lever" sounds intentional but always different.
class Randomizer
{
public:
    enum Archetype { Pad, Pluck, Bass, Lead, Bell, Stab, Keys,
                     Organ, Drone, Vox, Perc, Brass, NumArchetypes };

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

    // Force a particular archetype. Only for measurement: asking how varied one
    // archetype is means holding it fixed, and filtering rolls by name gives a
    // different sample every time anything upstream shifts the random stream.
    Patch rollAs (unsigned seed, int archetype) { return rollNormal (seed, archetype); }

    static const char* archetypeName (int index)
    {
        switch (index)
        {
            case Pad:   return "Pad";    case Pluck: return "Pluck";
            case Bass:  return "Bass";   case Lead:  return "Lead";
            case Bell:  return "Bell";   case Stab:  return "Stab";
            case Keys:  return "Keys";   case Organ: return "Organ";
            case Drone: return "Drone";  case Vox:   return "Vox";
            case Perc:  return "Perc";   default:    return "Brass";
        }
    }
    Patch rollChaos()              { return rollChaosSeed (nextSeed()); }
    Patch rollChaos (unsigned seed){ return rollChaosSeed (seed); }

    // Called once the caller commits to a roll. Rolls that get thrown away
    // downstream (the audibility probe rejects some) must NOT count as "recent",
    // or the no-repeat rule ends up comparing against a sound nobody heard.
    void accept (const Patch& p)
    {
        recentArch[1] = recentArch[0];
        recentArch[0] = p.archetypeName;

        const juce::String mod = primaryModifier (p);
        if (mod.isNotEmpty())
        {
            recentMod[1] = recentMod[0];
            recentMod[0] = mod;
        }
    }

    // Nudge: the same sound, played slightly differently. Only continuous values
    // move, and only a little — every discrete choice (waveform, filter model,
    // oscillator mode, archetype, arp) is what makes a patch *that* patch, so
    // changing any of them would be a new roll rather than a variation.
    //
    // Amount is 0..1; at 1 the drift is obvious but the sound is still related.
    void nudge (Patch& p, float amount = 1.0f)
    {
        const float a = juce::jlimit (0.0f, 1.0f, amount);

        // Multiplicative for anything measured in Hz or seconds, so a 20 ms
        // attack and a 2 s attack drift by the same *proportion* rather than the
        // same absolute amount.
        auto scale = [this, a] (float v, float spread)
        {
            return v * std::exp2 (f (-spread, spread) * a);
        };
        auto shift = [this, a] (float v, float spread, float lo, float hi)
        {
            return juce::jlimit (lo, hi, v + f (-spread, spread) * a);
        };

        p.cutoff    = juce::jlimit (30.0f, 18000.0f, scale (p.cutoff, 1.1f));
        p.resonance = shift (p.resonance, 0.22f, 0.0f, 0.95f);
        p.filterEnvAmt = shift (p.filterEnvAmt, 0.3f, -1.0f, 1.0f);
        p.keytrack  = shift (p.keytrack, 0.1f, 0.0f, 1.0f);

        p.ampA = juce::jlimit (0.001f, 4.0f, scale (p.ampA, 0.9f));
        p.ampD = juce::jlimit (0.005f, 6.0f, scale (p.ampD, 0.9f));
        p.ampS = shift (p.ampS, 0.2f, 0.0f, 1.0f);
        p.ampR = juce::jlimit (0.01f, 8.0f, scale (p.ampR, 0.9f));

        p.modA = juce::jlimit (0.001f, 4.0f, scale (p.modA, 0.9f));
        p.modD = juce::jlimit (0.005f, 6.0f, scale (p.modD, 0.9f));
        p.modS = shift (p.modS, 0.2f, 0.0f, 1.0f);
        p.modR = juce::jlimit (0.01f, 8.0f, scale (p.modR, 0.9f));

        for (auto& o : p.osc)
        {
            o.level = shift (o.level, 0.22f, 0.0f, 1.0f);
            o.fine  = shift (o.fine, 14.0f, -50.0f, 50.0f);
            o.decay = juce::jlimit (0.0f, 4.0f, o.decay * std::exp2 (f (-0.4f, 0.4f) * a));
        }

        p.unisonDetune = juce::jlimit (0.0f, 50.0f, scale (p.unisonDetune, 0.8f));
        p.stereoWidth  = shift (p.stereoWidth, 0.15f, 0.0f, 1.0f);
        p.fmAmount     = juce::jlimit (0.0f, 1.0f, scale (p.fmAmount, 0.7f));
        p.subLevel     = shift (p.subLevel, 0.1f, 0.0f, 1.0f);
        p.noiseLevel   = shift (p.noiseLevel, 0.08f, 0.0f, 1.0f);
        p.wtPos        = shift (p.wtPos, 0.35f, 0.0f, 1.0f);

        // Modulators: rate and depth drift, shape and destination do not.
        for (int k = 0; k < Patch::NumMods; ++k)
        {
            auto& m = p.mod[k];
            if (m.dest == ModNone) continue;
            m.rate  = juce::jlimit (0.01f, 20.0f, scale (m.rate, 1.0f));
            m.depth = shift (m.depth, 0.3f, 0.0f, 1.0f);
        }
        p.envAmount = shift (p.envAmount, 0.3f, -1.0f, 1.0f);

        // Effects move least. A nudge that swings the reverb from dry to
        // cathedral does not read as the same sound.
        p.drive     = shift (p.drive, 0.2f, 0.0f, 1.0f);
        p.foldAmount= shift (p.foldAmount, 0.18f, 0.0f, 1.0f);
        p.chorusMix = shift (p.chorusMix, 0.08f, 0.0f, 1.0f);
        p.phaserMix = shift (p.phaserMix, 0.08f, 0.0f, 1.0f);
        p.flangerMix= shift (p.flangerMix, 0.08f, 0.0f, 1.0f);
        p.delayMix  = shift (p.delayMix, 0.07f, 0.0f, 0.9f);
        p.reverbMix = shift (p.reverbMix, 0.07f, 0.0f, 0.9f);

        if (p.pluckLevel > 0.001f)
        {
            p.pluckDamping    = shift (p.pluckDamping, 0.12f, 0.0f, 1.0f);
            p.pluckDecay      = shift (p.pluckDecay, 0.12f, 0.0f, 1.0f);
            p.pluckBrightness = shift (p.pluckBrightness, 0.12f, 0.0f, 1.0f);
        }

        // The seed no longer describes this sound, so it stops claiming to.
        p.seed = (unsigned) i (0, 999999);
    }

private:
    // How far a normal roll strays from its archetype's comfort zone. 1.0 was
    // the original tuning. Raising it widens every spread and makes every
    // structural rule-break likelier, without touching CHAOS — which has no
    // rails at all and is unaffected by this.
    static constexpr float Wildness = 1.2f;

    // A probability scaled by Wildness, kept sane at the top end.
    bool chanceW (float base) { return chance (juce::jmin (0.95f, base * Wildness)); }

    juce::String recentArch[2];   // last two archetypes, freshest first
    juce::String recentMod[2];    // last two modifiers that actually applied

    // A double twist is named "tape+clang", so comparing whole names let
    // "tape+swell" follow "tape+clang" unchallenged — and tape is exactly the
    // kind of modifier you notice twice in a row. Compare the leading one.
    static juce::String primaryModifier (const Patch& p)
    {
        return p.modifierName.upToFirstOccurrenceOf ("+", false, false);
    }

    bool tooSimilar (const Patch& p) const
    {
        const juce::String mod = primaryModifier (p);
        return p.archetypeName == recentArch[0]
            || p.archetypeName == recentArch[1]
            || (mod.isNotEmpty() && (mod == recentMod[0] || mod == recentMod[1]));
    }

    std::mt19937 rng;                                   // seeded per roll
    std::mt19937 seedGen { std::random_device{}() };    // picks fresh seeds
    unsigned nextSeed() { return seedGen() % 1000000u; } // 6-digit, easy to share

    Patch rollNormal (unsigned seed, int forceArchetype = -1)
    {
        rng.seed (seed);
        const int rolled = (int) (rng() % (unsigned) NumArchetypes);
        const int archetype = (forceArchetype >= 0) ? forceArchetype : rolled;

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
            case Keys:  makeKeys (p);  p.archetypeName = "Keys";  break;
            case Organ: makeOrgan (p); p.archetypeName = "Organ"; break;
            case Drone: makeDrone (p); p.archetypeName = "Drone"; break;
            case Vox:   makeVox (p);   p.archetypeName = "Vox";   break;
            case Perc:  makePerc (p);  p.archetypeName = "Perc";  break;
            default:    makeBrass (p); p.archetypeName = "Brass"; break;
        }
        borrowFromAnother (p, archetype);
        applyModifier (p);
        loosen (p);
        finaliseModulation (p, false);

        // After everything that can create it: loosen() re-randomises the very
        // parameters this softens, so running it earlier let the zap come back.
        thinOutTheZap (p);

        rollWavetable (p);
        rollString (p);
        rollChord (p);
        rollNoiseColour (p);
        rollArp (p);

        // Delay on the grid unless the roll wants it loose. Resolved against the
        // host tempo at play time (120 BPM in the standalone).
        if (p.delayMix > 0.001f)
            p.delaySyncDiv = chance (0.8f) ? i (1, 7) : 0;

        p.seed = (int) seed; p.chaos = false;
        return p;
    }

    // An archetype's primary movement goes straight into slot 0. Shape is chosen
    // later, once we know the destination.
    void motion (Patch& p, int dest, float rate, float depth)
    {
        p.mod[0].dest  = dest;
        p.mod[0].rate  = rate;
        p.mod[0].depth = depth;
    }

    // ---- Modulation. Archetypes set their primary movement in slot 0 via
    // motion(); the other two slots are rolled here. This is where most of a
    // patch's *motion* variety comes from — shape matters more than rate, and a
    // destination nobody expects (pan, FM index, detune) is what stops two
    // patches sharing a silhouette. ----
    void finaliseModulation (Patch& p, bool wild)
    {
        auto& m0 = p.mod[0];
        if (m0.depth < 0.001f) m0.dest = ModNone;
        m0.phase = f (0.0f, 1.0f);

        // Vibrato and tremolo want a smooth shape; anything else can be strange.
        if (m0.dest == ModPitch)
            m0.shape = chance (0.75f) ? ModSine : ModTri;
        else if (m0.dest == ModAmp)
            m0.shape = chance (0.45f) ? ModSquare : (chance (0.5f) ? ModSine : ModRampDown);
        else
            m0.shape = pickShape (wild);

        // PWM used to be its own hardwired thing; it is just a modulator.
        int next = 1;
        if (p.pwmDepth > 0.01f && next < Patch::NumMods)
        {
            auto& m = p.mod[next++];
            m.dest  = ModPulseWidth;
            m.depth = p.pwmDepth;
            m.rate  = f (0.08f, 3.0f);
            m.shape = chance (0.6f) ? ModSine : ModTri;
            m.phase = f (0.0f, 1.0f);
        }

        // Fill whatever is left. Wild rolls take any destination; tame ones stay
        // away from the two that most easily wreck a sound.
        const float chanceOfMore = wild ? 0.85f : juce::jmin (0.9f, 0.55f * Wildness);
        for (; next < Patch::NumMods; ++next)
        {
            if (! chance (chanceOfMore)) break;

            auto& m = p.mod[next];
            m.dest  = pickDest (wild);
            m.shape = pickShape (wild);
            m.phase = f (0.0f, 1.0f);

            // Slow drift and fast flutter are different instruments; pick a lane
            // rather than always landing in the middle.
            m.rate = chance (0.45f) ? f (0.03f, 0.9f) : f (1.5f, wild ? 18.0f : 9.0f);

            // Pitch is the one destination where a big depth is just out of tune.
            m.depth = (m.dest == ModPitch) ? f (0.02f, wild ? 0.5f : 0.18f * Wildness)
                                           : f (0.15f, juce::jmin (1.0f, wild ? 1.0f : 0.7f * Wildness));

            // A stepped or square modulator on the grid reads as rhythm.
            if ((m.shape == ModSampleHold || m.shape == ModSquare) && chance (0.5f))
                m.syncDiv = i (1, 5);
        }

        // The mod envelope's second destination: pitch drops, FM sweeps, filters
        // that do something other than open.
        if (wild ? chance (0.6f) : chanceW (0.35f))
        {
            p.envDest   = pickDest (wild);
            p.envAmount = f (-1.0f, 1.0f);
        }

        // Transient layers: a fast-decaying oscillator over a sustained one is
        // how a struck instrument is built.
        if (wild ? chance (0.5f) : chanceW (0.3f))
        {
            const int which = chance (0.6f) ? 1 : 2;
            p.osc[which].decay = f (0.01f, 0.35f);
        }
    }

    // A resonant filter driven hard by a fast envelope is a laser zap, and it
    // was landing on one roll in seven — often enough to become the sound the
    // engine is known for rather than one of the things it can do. The three
    // ingredients are high resonance, a strong filter envelope and a short mod
    // decay; this breaks up one of them most of the time it sees all three, so
    // the zap still happens, just not constantly.
    void thinOutTheZap (Patch& p)
    {
        const bool zap = p.resonance > 0.45f
                      && std::abs (p.filterEnvAmt) > 0.4f
                      && p.modD < 0.4f;

        if (! zap || chance (0.28f))     // left alone about a quarter of the time
            return;

        switch (i (0, 2))
        {
            case 0:  p.resonance = f (0.08f, 0.4f);            break;  // less whistle
            case 1:  p.filterEnvAmt *= f (0.15f, 0.5f);        break;  // less sweep
            default: p.modD = f (0.45f, 1.6f);                 break;  // slower, so it opens rather than snaps
        }
    }

    // Occasionally build a second archetype and transplant one section of it.
    // A bell's envelope on a bass, an organ's effects on a pluck — combinations
    // no single recipe produces, and the cheap way to get what a trait-slot
    // rewrite was supposed to deliver.
    //
    // Only one section moves, so the host archetype still decides most of the
    // sound. The sections are the same five the HOLD reels use, which already
    // know how to copy one part of a patch without disturbing the rest.
    void borrowFromAnother (Patch& p, int hostArchetype)
    {
        if (! chanceW (0.12f))
            return;

        int other = i (0, NumArchetypes - 2);
        if (other >= hostArchetype) ++other;      // anything but the host

        Patch donor;
        setThreeOsc (donor, 2, 2, 0);
        donor.master = f (0.7f, 0.85f);

        switch (other)
        {
            case Pad:   makePad (donor);   break;
            case Pluck: makePluck (donor); break;
            case Bass:  makeBass (donor);  break;
            case Lead:  makeLead (donor);  break;
            case Bell:  makeBell (donor);  break;
            case Stab:  makeStab (donor);  break;
            case Keys:  makeKeys (donor);  break;
            case Organ: makeOrgan (donor); break;
            case Drone: makeDrone (donor); break;
            case Vox:   makeVox (donor);   break;
            case Perc:  makePerc (donor);  break;
            default:    makeBrass (donor); break;
        }

        // Weighted away from the oscillators: borrowing those replaces what the
        // sound is made of rather than colouring it, and the host archetype
        // stops meaning anything. Envelope and effects transplant best.
        const float r = f (0.0f, 1.0f);
        const int reel = r < 0.30f ? ReelEnv
                       : r < 0.55f ? ReelFx
                       : r < 0.78f ? ReelFilter
                       : r < 0.92f ? ReelMod
                                   : ReelOsc;

        copyReel (p, donor, reel);
    }

    // A plucked string is its own instrument rather than a layer, so when one
    // turns up it takes the lead and the oscillators drop back to being its
    // body. Gated to archetypes that are already struck or plucked — a string
    // under a sustained pad is just a click at the start.
    void rollString (Patch& p)
    {
        const bool struck = p.archetypeName == "Pluck" || p.archetypeName == "Perc"
                         || p.archetypeName == "Bell"  || p.archetypeName == "Keys"
                         || p.archetypeName == "Stab";

        if (! struck || ! chanceW (0.28f))
            return;

        p.pluckLevel      = f (0.35f, 0.8f);
        p.pluckDamping    = f (0.15f, 0.85f);    // nylon .. steel
        p.pluckDecay      = f (0.35f, 0.98f);
        p.pluckBrightness = f (0.15f, 0.9f);

        // The string carries the note, so pull the oscillators back and let the
        // amp envelope stay out of its way.
        for (auto& o : p.osc) o.level *= f (0.2f, 0.5f);
        p.ampS = juce::jmin (p.ampS, 0.25f);
        p.ampR = juce::jmax (p.ampR, 0.3f);

        // A wide-open filter lets the string's own decay be the tone.
        p.cutoff = juce::jmax (p.cutoff, f (2500.0f, 7000.0f));
    }

    // One key press giving a chord. Kept away from the archetypes where it would
    // be muddy: a chord in the bass is a mess, and a chord under an arp is two
    // patterns fighting.
    void rollChord (Patch& p)
    {
        const bool suits = p.archetypeName != "Bass" && p.archetypeName != "Drone"
                        && p.voiceMode == 0 && p.pluckLevel < 0.001f;

        if (! suits || ! chanceW (0.12f))
            return;

        p.chordType    = i (1, 5);
        p.unisonVoices = juce::jmax (3, p.unisonVoices);
        p.unisonDetune = f (2.0f, 12.0f);        // chords want much less detune
    }

    // Colour costs nothing and pink/brown are far more useful than white for
    // breath and rumble, so most noise gets one.
    void rollNoiseColour (Patch& p)
    {
        if (p.noiseLevel < 0.001f)
            return;

        p.noiseColour = chance (0.3f) ? 0
                      : (chance (0.55f) ? 1 : (chance (0.6f) ? 2 : 3));
    }

    // A static wavetable is just an unusual waveform — the payoff is entirely in
    // moving the position, so any roll that takes one also gets something moving
    // it. Applied after modulation is settled so it can claim a free slot, or
    // repoint an existing one if all three are busy.
    void rollWavetable (Patch& p)
    {
        if (! chanceW (0.22f))
            return;

        p.wtTable = i (0, Wavetables::NumTables - 1);
        p.wtPos   = f (0.0f, 1.0f);

        // Osc 1 always, so the table is audible rather than buried; sometimes
        // osc 2 as well, which makes the morph much wider.
        p.osc[0].wave = 5;
        if (chance (0.35f)) p.osc[1].wave = 5;

        // Find a slot to move it with: a free one first, else the least
        // interesting of what is there.
        int slot = -1;
        for (int k = 0; k < Patch::NumMods; ++k)
            if (p.mod[k].dest == ModNone || p.mod[k].depth < 0.02f) { slot = k; break; }

        if (slot < 0)
        {
            // Never steal slot 0 — that is the archetype's own character.
            slot = i (1, Patch::NumMods - 1);
        }

        auto& m = p.mod[slot];
        m.dest  = ModWtPos;
        m.phase = f (0.0f, 1.0f);
        m.depth = f (0.25f, 0.9f);

        // Slow sweeps evolve, fast ones flutter, stepped ones stutter through
        // shapes. All three are worth having.
        if (chance (0.55f))
        {
            m.shape = chance (0.6f) ? ModSine : ModTri;
            m.rate  = f (0.04f, 0.5f);
        }
        else if (chance (0.5f))
        {
            m.shape = chance (0.5f) ? ModSampleHold : ModSquare;
            m.rate  = f (2.0f, 10.0f);
            if (chance (0.6f)) m.syncDiv = i (1, 5);
        }
        else
        {
            m.shape = ModRandomWalk;
            m.rate  = f (0.1f, 1.2f);
        }

        // Half the time the envelope shapes it too, so the attack has a
        // different timbre from the sustain.
        if (chance (0.5f))
        {
            p.envDest   = ModWtPos;
            p.envAmount = f (-0.9f, 0.9f);
        }
    }

    // An arp only suits a patch that can articulate: a slow attack smears every
    // step into the next, and a long tail turns a pattern into porridge. Gate on
    // that rather than rolling one onto anything.
    void rollArp (Patch& p)
    {
        const bool articulate = p.ampA < 0.12f
                             && p.reverbMix < 0.45f
                             && p.delayMix  < 0.40f
                             && p.voiceMode == 0;       // needs held notes

        if (! articulate || ! chance (0.10f))
            return;

        p.arpMode    = i (1, 4);
        p.arpDiv     = i (1, 3);          // 1/16, 1/8 triplet, 1/8 — musical by construction
        p.arpGate    = f (0.35f, 0.85f);
        p.arpOctaves = chance (0.55f) ? 1 : (chance (0.6f) ? 2 : 3);

        // A pattern wants its notes to end before the next one starts.
        p.ampR = juce::jmin (p.ampR, f (0.08f, 0.35f));
    }

    int pickShape (bool wild)
    {
        // The smooth shapes are the safe ones; a higher Wildness reaches for the
        // stepped and stuttering ones more often.
        if (! wild && chance (0.45f / Wildness)) return chance (0.6f) ? ModSine : ModTri;
        return i (0, NumModShapes - 1);
    }

    int pickDest (bool wild)
    {
        for (int tries = 0; tries < 8; ++tries)
        {
            const int d = i (1, NumModDests - 1);
            // Amp and pitch are the two that turn a good sound bad fastest, so
            // tame rolls only take them occasionally.
            if (! wild && (d == ModAmp || d == ModPitch) && ! chanceW (0.3f))
                continue;
            return d;
        }
        return ModCutoff;
    }

    // ---- Guard rails, loosened. Archetypes used to keep every roll well inside
    // its comfort zone, which is why two rolls of the same family shared a
    // silhouette. This pushes the continuous parameters out toward the edges and
    // occasionally breaks the archetype's own structural rules outright. Not
    // CHAOS — the audibility probe still rejects anything that lands mute — but
    // enough that normal mode stops feeling supervised. ----
    void loosen (Patch& p)
    {
        // Multiply by up to `oct` octaves in either direction.
        auto spread = [this] (float& v, float lo, float hi, float oct)
        {
            v = juce::jlimit (lo, hi, v * std::exp2 (f (-oct, oct)));
        };

        spread (p.cutoff, 60.0f,  12000.0f, 0.9f * Wildness);
        spread (p.ampD,   0.01f,  3.0f,     0.7f * Wildness);
        spread (p.ampR,   0.02f,  3.5f,     0.7f * Wildness);
        spread (p.modD,   0.01f,  3.0f,     0.7f * Wildness);
        spread (p.mod[0].rate, 0.03f, 18.0f, 0.6f * Wildness);

        p.resonance    = juce::jlimit (0.0f,  0.85f, p.resonance + f (-0.12f, 0.28f * Wildness));
        p.filterEnvAmt = juce::jlimit (-1.0f, 1.0f,  p.filterEnvAmt + f (-0.25f, 0.25f) * Wildness);
        p.stereoWidth  = juce::jlimit (0.0f,  1.0f,  p.stereoWidth + f (-0.2f, 0.2f) * Wildness);

        // Structural rule-breaking, rare enough to stay a surprise.
        if (chanceW (0.15f)) { p.oscMode = i (0, 3); p.fmAmount = f (0.15f, 0.8f); }
        if (chanceW (0.12f)) { p.unisonVoices = i (1, 7); p.unisonDetune = f (5.0f, 35.0f); }
        if (chanceW (0.10f)) p.osc[2].semi += chance (0.5f) ? 12 : -12;
        if (chanceW (0.08f)) p.filterType = i (0, 2);
        if (chanceW (0.10f)) p.subLevel = f (0.2f, 0.6f);
        if (chanceW (0.08f)) p.osc[i (0, 2)].wave = i (0, 3);   // swap a waveform outright
    }

    // ---- Modifiers: a twist applied *after* the archetype. This is what stops
    // rolls reading as "one of seven presets" — the ear latches onto the twist
    // rather than the family underneath. Rolled from the same seeded stream, so
    // a seed still maps to exactly one sound. ----
    void applyModifier (Patch& p)
    {
        if (! chanceW (0.60f)) return;

        const int first = i (0, 17);
        p.modifierName = applyOne (p, first);

        // Occasionally a second, non-cancelling twist — the rolls people screenshot.
        if (chanceW (0.18f))
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
                motion (p, ModAmp, f (6.0f, 14.0f), f (0.6f, 1.0f));
                p.mod[0].syncDiv = i (1, 3);       // 1/16, 1/8T or 1/8
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
                motion (p, ModPitch, f (0.15f, 0.8f), f (0.5f, 1.0f));
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
            p.osc[k].wave  = i (0, 5);
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

        motion (p, i (ModPitch, ModAmp), f (0.05f, 15.0f), f (0.0f, 1.0f));

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
        p.voiceMode = i (0, 2); p.glideTime = chance (0.5f) ? glideAmount() : 0.0f;
        p.foldAmount = chance (0.4f) ? f (0.0f, 0.9f) : 0.0f;
        p.crushBits  = chance (0.4f) ? f (2.0f, 12.0f) : 16.0f;
        p.crushRate  = chance (0.4f) ? f (1.0f, 20.0f) : 1.0f;
        p.phaserMix  = chance (0.4f) ? f (0.2f, 0.65f) : 0.0f;   // full-wet can null the dry
        p.phaserRate = f (0.05f, 4.0f); p.phaserDepth = f (0.2f, 1.0f); p.phaserFb = f (0.0f, 0.8f);
        p.flangerMix = chance (0.4f) ? f (0.2f, 0.6f) : 0.0f;
        p.flangerRate= f (0.03f, 3.0f); p.flangerDepth= f (0.2f, 1.0f); p.flangerFb= f (0.0f, 0.85f);
        p.compAmount = chance (0.5f) ? f (0.0f, 0.9f) : 0.0f;
        p.delaySyncDiv = chance (0.5f) ? i (1, 7) : 0;
        p.wtTable = i (0, Wavetables::NumTables - 1);
        p.wtPos   = f (0.0f, 1.0f);
        if (chance (0.35f)) p.osc[i (0, 2)].wave = 5;
        p.noiseColour = i (0, 3);
        if (chance (0.3f))
        {
            p.pluckLevel      = f (0.2f, 0.9f);
            p.pluckDamping    = f (0.0f, 1.0f);
            p.pluckDecay      = f (0.2f, 1.0f);
            p.pluckBrightness = f (0.0f, 1.0f);
        }
        if (chance (0.2f)) p.chordType = i (1, 5);
        p.master      = f (0.6f, 0.85f); // master limiter still protects output

        finaliseModulation (p, true);
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

    // Portamento length. A flat range made every glide patch sound like the same
    // slow bend, so this is cubed: most slides are a quick smear you barely
    // notice, a few are a real slide.
    float glideAmount()
    {
        const float u = f (0.0f, 1.0f);
        return 0.004f + u * u * u * 0.34f;
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
        // Every pad used to be saw against saw, which fixed the timbre before
        // any of the ranges below got a say. A pad wants harmonics to detune
        // against each other, and square and triangle do that too.
        {
            const int lead = chance (0.55f) ? 2 : (chance (0.6f) ? 3 : 1);
            const int pair = chance (0.5f) ? lead : (chance (0.5f) ? 2 : 3);
            setThreeOsc (p, lead, pair, chance (0.5f) ? 1 : 2);
        }
        p.osc[2].semi = chance (0.5f) ? 12 : -12;

        // Pads measured tightest of all (0.372 against 0.504 across the engine),
        // and the reason was the space rather than the tone: every one of them
        // was wide, chorused, reverbed and slow. What makes a pad a pad is that
        // it sustains and does not attack — so the envelope stays soft and the
        // sustain stays high, and everything else gets to vary.
        p.ampA = f (0.15f, 2.2f); p.ampD = f (0.2f, 1.6f);
        p.ampS = f (0.55f, 0.98f); p.ampR = f (0.4f, 3.0f);
        p.filterType = chance (0.18f) ? (chance (0.5f) ? 1 : 2) : 0;
        p.cutoff = f (250.f, 6000.f);
        p.filterEnvAmt = f (-0.3f, 0.8f);
        p.modA = f (0.2f, 2.0f); p.modD = f (0.3f, 2.5f); p.modS = f (0.2f, 0.9f);

        // Usually a slow filter sweep, sometimes something else doing the
        // moving — a pad that breathes on its level or detune is still a pad.
        {
            const float r = f (0.0f, 1.0f);
            if      (r < 0.55f) motion (p, ModCutoff, f (0.05f, 1.8f), f (0.1f, 0.6f));
            else if (r < 0.75f) motion (p, ModAmp,    f (0.08f, 0.9f), f (0.1f, 0.4f));
            else if (r < 0.9f)  motion (p, ModDetune, f (0.05f, 0.6f), f (0.15f, 0.6f));
            else                motion (p, ModPulseWidth, f (0.08f, 1.2f), f (0.2f, 0.7f));
        }

        // Dry pads exist and are a different instrument from washed ones.
        p.reverbSize = f (0.3f, 0.95f);
        p.reverbMix  = chance (0.2f) ? f (0.0f, 0.15f) : f (0.25f, 0.6f);
        p.delayMix = chance (0.4f) ? f (0.08f, 0.4f) : 0.0f;
        p.delayTime = f (0.2f, 0.7f); p.delayFb = f (0.15f, 0.55f);
        p.stereoWidth = f (0.35f, 0.98f);
        p.chorusMix   = chance (0.25f) ? f (0.0f, 0.15f) : f (0.25f, 0.7f);
        if (chance (0.35f)) { p.oscMode = 1; p.fmAmount = f (0.2f, 0.5f); }  // slow ring shimmer

        // Narrow unison is a thinner, colder pad rather than a lush one, and it
        // costs a great deal less to play.
        p.unisonVoices = chance (0.3f) ? i (2, 4) : (chance (0.5f) ? 5 : 7);
        p.unisonDetune = f (4.0f, 30.0f);
        p.subLevel = f (0.0f, 0.35f); p.noiseLevel = chance (0.25f) ? f (0.02f, 0.14f) : f (0.0f, 0.05f);
        p.pulseWidth = f (0.2f, 0.8f); p.pwmDepth = f (0.0f, 0.6f);
        p.velToAmp = f (0.1f, 0.6f); p.velToFilter = f (0.1f, 0.7f);
        pickFilter (p, 0.20f, 0.06f, 0.18f, 0.08f);
        p.drive = f (0.0f, 0.4f); p.compAmount = f (0.0f, 0.4f);
        pickFx (p, 0.28f, 0.16f, 0.05f, 0.1f);   // vowel pads are a highlight
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
        // Two rolls of Bass were closer to each other than two rolls of
        // anything else (0.387 against 0.504 across the whole engine), so the
        // ranges here are wider than a bass strictly needs. What keeps it a
        // bass is the sub, the short attack and the low fundamental — none of
        // which the width below touches.
        p.ampA = f (0.001f, 0.03f); p.ampD = f (0.06f, 0.9f);
        p.ampS = f (0.25f, 0.95f);  p.ampR = f (0.04f, 0.5f);
        p.filterType = chance (0.15f) ? 1 : 0;          // occasionally band-pass
        p.cutoff = f (140.f, 1500.f);
        p.filterEnvAmt = f (0.1f, 0.9f);
        p.modA = f (0.001f, 0.02f); p.modD = f (0.05f, 0.6f); p.modS = f (0.05f, 0.6f);
        p.resonance = f (0.05f, 0.65f);
        p.reverbMix = chance (0.25f) ? f (0.1f, 0.3f) : f (0.0f, 0.12f);
        p.reverbSize = f (0.2f, 0.5f);
        p.delayMix = chance (0.2f) ? f (0.08f, 0.25f) : 0.0f;
        p.delayTime = f (0.12f, 0.3f); p.delayFb = f (0.15f, 0.4f);
        p.stereoWidth = f (0.05f, 0.45f); p.chorusMix = f (0.0f, 0.3f);
        if (chance (0.3f)) { p.oscMode = 3; p.fmAmount = f (0.15f, 0.45f);
                            setRatio (p.osc[1], f (1.4f, 3.2f)); }                  // FM growl
        else if (chance (0.3f)) { p.oscMode = 1; p.fmAmount = f (0.3f, 0.6f); }                 // ring dirt

        // Mostly tight and centred, because that is what makes a bass punch,
        // but a detuned one is a different animal and worth landing on.
        p.unisonVoices = chance (0.2f) ? i (2, 3) : 1;
        p.unisonDetune = f (3.0f, 12.0f);

        p.subWave = chance (0.4f) ? 3 : 0; p.subLevel = f (0.2f, 0.75f);
        p.noiseLevel = chance (0.2f) ? f (0.02f, 0.12f) : f (0.0f, 0.04f);
        p.velToAmp = f (0.1f, 0.6f); p.velToFilter = f (0.2f, 0.8f);
        if (chance (0.6f)) { p.voiceMode = chance (0.5f) ? 1 : 2; p.glideTime = chance (0.6f) ? glideAmount() : 0.0f; }
        pickFilter (p, 0.35f, 0.25f, 0.05f, 0.08f);
        p.drive = f (0.05f, 0.7f); p.compAmount = f (0.1f, 0.6f);
        pickFx (p, 0.08f, 0.08f, 0.2f, 0.2f);   // ladder weight + acid
    }

    void makeLead (Patch& p)
    {
        setThreeOsc (p, 2, 2, chance (0.5f) ? 3 : 2);
        // Same story as Bass: 0.430 against 0.504, so the ranges open up. A
        // lead is defined by sitting on top and being played one note at a
        // time, not by a narrow filter range or a fixed amount of vibrato.
        p.ampA = f (0.005f, 0.35f); p.ampD = f (0.06f, 0.7f);
        p.ampS = f (0.35f, 0.95f);  p.ampR = f (0.1f, 1.2f);
        p.filterType = chance (0.15f) ? 1 : 0;
        p.cutoff = f (700.f, 9000.f);
        p.filterEnvAmt = f (-0.2f, 0.8f);

        // Vibrato most of the time; sometimes something else is moving instead.
        if (chance (0.7f))  motion (p, ModPitch, f (3.0f, 8.0f), f (0.08f, 0.6f));
        else                motion (p, chance (0.5f) ? ModCutoff : ModPulseWidth,
                                    f (0.15f, 6.0f), f (0.2f, 0.7f));

        p.reverbMix = f (0.05f, 0.55f); p.reverbSize = f (0.25f, 0.85f);
        p.delayMix = chance (0.7f) ? f (0.12f, 0.5f) : 0.0f;
        p.delayTime = f (0.12f, 0.6f); p.delayFb = f (0.2f, 0.6f);
        if (chance (0.4f)) { p.oscMode = 2; p.fmAmount = f (0.15f, 0.5f); } // sync lead
        p.unisonVoices = chance (0.25f) ? 1 : i (2, 6);
        p.unisonDetune = f (2.0f, 26.0f);
        p.subLevel = f (0.0f, 0.35f); p.noiseLevel = chance (0.2f) ? f (0.01f, 0.08f) : 0.0f;
        p.pulseWidth = f (0.2f, 0.8f); p.pwmDepth = f (0.0f, 0.7f);
        p.velToAmp = f (0.1f, 0.6f); p.velToFilter = f (0.1f, 0.7f);
        if (chance (0.5f)) { p.voiceMode = chance (0.4f) ? 1 : 2; p.glideTime = chance (0.7f) ? glideAmount() : 0.0f; }
        pickFilter (p, 0.30f, 0.20f, 0.10f, 0.05f);
        p.drive = f (0.03f, 0.65f); p.compAmount = f (0.05f, 0.5f);
        pickFx (p, 0.25f, 0.22f, 0.12f, 0.16f);
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
        // "Keys" is a family, not a sound. Rolling one recipe made every keys
        // patch an electric piano wearing the same chorus — so each branch here
        // is a different instrument, not a different setting.
        p.filterType = 0;
        p.modD = f (0.3f, 0.7f); p.modS = f (0.1f, 0.4f);
        p.reverbMix = f (0.12f, 0.35f); p.reverbSize = f (0.35f, 0.7f);
        p.delayMix = chance (0.35f) ? f (0.1f, 0.25f) : 0.0f;
        p.delayTime = f (0.25f, 0.5f); p.delayFb = f (0.2f, 0.4f);
        p.unisonVoices = 1; p.noiseLevel = 0.0f;

        switch (i (0, 4))
        {
            case 0:     // electric piano — FM at 2:1, soft, chorused
                setThreeOsc (p, 0, 0, chance (0.5f) ? 0 : 1);
                p.oscMode = 3; p.fmAmount = f (0.12f, 0.4f);
                setRatio (p.osc[1], f (1.97f, 2.03f));
                p.ampA = f (0.002f, 0.012f); p.ampD = f (0.5f, 1.4f);
                p.ampS = f (0.15f, 0.4f);    p.ampR = f (0.3f, 0.8f);
                p.cutoff = f (1500.f, 4500.f); p.filterEnvAmt = f (0.1f, 0.4f);
                p.chorusMix = f (0.3f, 0.6f);
                p.velToAmp = f (0.5f, 0.85f); p.velToFilter = f (0.3f, 0.6f);
                p.drive = f (0.02f, 0.18f);
                break;

            case 1:     // clav — short, bright, resonant, bone dry
                setThreeOsc (p, 2, 3, 2);
                p.ampA = f (0.001f, 0.006f); p.ampD = f (0.12f, 0.35f);
                p.ampS = f (0.0f, 0.15f);    p.ampR = f (0.08f, 0.2f);
                p.cutoff = f (800.f, 2500.f); p.resonance = f (0.3f, 0.6f);
                p.filterEnvAmt = f (0.4f, 0.85f);
                p.chorusMix = f (0.0f, 0.12f);
                p.velToAmp = f (0.4f, 0.75f); p.velToFilter = f (0.5f, 0.9f);
                p.drive = f (0.2f, 0.5f);
                break;

            case 2:     // music box — high inharmonic partial, tiny and bright
                setThreeOsc (p, 0, 0, 0);
                p.oscMode = 3; p.fmAmount = f (0.08f, 0.25f);
                setRatio (p.osc[1], f (3.5f, 8.0f));
                p.ampA = f (0.001f, 0.004f); p.ampD = f (0.25f, 0.8f);
                p.ampS = 0.0f;               p.ampR = f (0.3f, 0.9f);
                p.cutoff = f (3000.f, 8000.f); p.filterEnvAmt = f (0.0f, 0.2f);
                p.chorusMix = f (0.0f, 0.15f);
                p.reverbMix = f (0.3f, 0.5f);
                p.velToAmp = f (0.5f, 0.9f);
                p.drive = f (0.0f, 0.1f);
                break;

            case 3:     // wurly — FM off the octave, tremolo, a bit of grit
                setThreeOsc (p, 0, 3, 0);
                p.oscMode = 3; p.fmAmount = f (0.25f, 0.6f);
                setRatio (p.osc[1], f (1.4f, 2.6f));
                p.ampA = f (0.002f, 0.015f); p.ampD = f (0.4f, 1.0f);
                p.ampS = f (0.2f, 0.45f);    p.ampR = f (0.2f, 0.6f);
                p.cutoff = f (1200.f, 3500.f); p.filterEnvAmt = f (0.15f, 0.45f);
                motion (p, ModAmp, f (4.0f, 7.0f), f (0.2f, 0.5f));
                p.chorusMix = f (0.1f, 0.35f);
                p.velToAmp = f (0.45f, 0.8f);
                p.drive = f (0.15f, 0.45f);
                break;

            default:    // harpsichord / plucked — no sustain at all, sometimes sync
                setThreeOsc (p, 2, 2, 3);
                if (chance (0.35f)) { p.oscMode = 2; p.fmAmount = f (0.15f, 0.45f); }
                p.ampA = f (0.001f, 0.005f); p.ampD = f (0.15f, 0.5f);
                p.ampS = f (0.0f, 0.1f);     p.ampR = f (0.1f, 0.3f);
                p.cutoff = f (1500.f, 5000.f); p.resonance = f (0.1f, 0.35f);
                p.filterEnvAmt = f (0.3f, 0.7f);
                p.chorusMix = f (0.0f, 0.1f);
                p.velToAmp = f (0.45f, 0.85f); p.velToFilter = f (0.4f, 0.8f);
                p.drive = f (0.05f, 0.3f);
                break;
        }

        p.subLevel = f (0.0f, 0.15f);
        pickFilter (p, 0.25f, 0.05f, 0.05f, 0.10f);
        p.compAmount = f (0.1f, 0.35f);
        pickFx (p, 0.20f, 0.10f, 0.08f, 0.05f);
    }

    // Drawbars: harmonic partials, instant on, instant off, no filter theatre.
    void makeOrgan (Patch& p)
    {
        setThreeOsc (p, chance (0.5f) ? 0 : 3, 0, chance (0.5f) ? 0 : 3);
        setRatio (p.osc[1], chance (0.5f) ? 2.0f : 3.0f);
        setRatio (p.osc[2], chance (0.4f) ? 4.0f : (chance (0.5f) ? 6.0f : 8.0f));
        p.osc[1].level = f (0.3f, 0.7f); p.osc[2].level = f (0.15f, 0.5f);
        p.ampA = f (0.001f, 0.008f); p.ampD = f (0.05f, 0.2f);
        p.ampS = f (0.85f, 1.0f);     p.ampR = f (0.02f, 0.12f);
        p.filterType = 0; p.cutoff = f (2500.f, 9000.f); p.filterEnvAmt = f (0.0f, 0.1f);
        motion (p, ModCutoff, f (4.0f, 7.5f), f (0.0f, 0.25f));   // leslie-ish
        p.chorusMix = f (0.3f, 0.7f);
        p.reverbMix = f (0.15f, 0.4f); p.reverbSize = f (0.4f, 0.8f);
        p.velToAmp = f (0.0f, 0.2f); p.velToFilter = f (0.0f, 0.2f);
        p.unisonVoices = 1;
        pickFilter (p, 0.15f, 0.0f, 0.10f, 0.0f);
        p.drive = f (0.05f, 0.35f); p.compAmount = f (0.1f, 0.4f);
        pickFx (p, 0.30f, 0.12f, 0.05f, 0.08f);
    }

    // Everything slow: takes seconds to arrive and seconds to leave.
    void makeDrone (Patch& p)
    {
        setThreeOsc (p, 2, chance (0.5f) ? 1 : 2, 0);
        setRatio (p.osc[1], chance (0.5f) ? 0.5f : f (1.002f, 1.02f));   // octave down or beating
        p.osc[2].semi = chance (0.5f) ? -12 : 0;
        p.ampA = f (1.0f, 2.8f); p.ampD = f (0.8f, 2.0f);
        p.ampS = f (0.8f, 1.0f); p.ampR = f (1.5f, 3.5f);
        p.modA = f (1.0f, 2.5f); p.modD = f (1.0f, 2.5f); p.modS = f (0.5f, 0.9f);
        p.filterType = 0; p.cutoff = f (300.f, 2000.f); p.filterEnvAmt = f (0.2f, 0.6f);
        motion (p, ModCutoff, f (0.03f, 0.4f), f (0.3f, 0.8f));
        p.unisonVoices = chance (0.5f) ? 5 : 7; p.unisonDetune = f (10.0f, 30.0f);
        p.subLevel = f (0.1f, 0.4f); p.noiseLevel = f (0.0f, 0.08f);
        p.stereoWidth = f (0.8f, 1.0f); p.chorusMix = f (0.3f, 0.6f);
        p.reverbSize = f (0.85f, 0.98f); p.reverbMix = f (0.4f, 0.6f);
        p.velToAmp = f (0.0f, 0.25f);
        pickFilter (p, 0.25f, 0.0f, 0.20f, 0.0f);
        p.drive = f (0.0f, 0.2f); p.compAmount = f (0.0f, 0.3f);
        pickFx (p, 0.30f, 0.15f, 0.0f, 0.05f);
    }

    // Formant-forward and breathy — the filter is the instrument here.
    void makeVox (Patch& p)
    {
        setThreeOsc (p, 2, 2, chance (0.5f) ? 1 : 2);
        p.osc[1].fine = f (-12.0f, 12.0f);
        p.ampA = f (0.03f, 0.25f); p.ampD = f (0.2f, 0.6f);
        p.ampS = f (0.6f, 0.9f);   p.ampR = f (0.2f, 0.7f);
        p.filterModel = 3;                                  // formant, always
        p.filterMorph = f (0.0f, 1.0f);
        p.cutoff = f (500.f, 1600.f); p.resonance = f (0.3f, 0.6f);
        p.filterEnvAmt = f (0.0f, 0.3f); p.keytrack = f (0.2f, 0.6f);
        motion (p, ModPitch, f (4.5f, 6.5f), f (0.1f, 0.35f));    // vibrato
        p.noiseLevel = f (0.03f, 0.15f);                     // breath
        p.unisonVoices = chance (0.5f) ? 3 : 1; p.unisonDetune = f (5.0f, 14.0f);
        p.reverbMix = f (0.25f, 0.5f); p.reverbSize = f (0.5f, 0.85f);
        p.velToAmp = f (0.3f, 0.7f);
        p.drive = f (0.0f, 0.2f); p.compAmount = f (0.1f, 0.4f);
        pickFx (p, 0.20f, 0.15f, 0.05f, 0.05f);
    }

    // Struck and gone: no sustain, inharmonic, bright.
    void makePerc (Patch& p)
    {
        setThreeOsc (p, chance (0.5f) ? 0 : 2, 0, 4);        // osc3 is noise
        setRatio (p.osc[1], f (1.3f, 11.0f));
        p.osc[2].level = f (0.1f, 0.45f);                    // the hit's noise transient
        p.oscMode = chance (0.5f) ? 3 : 1;
        p.fmAmount = f (0.3f, 0.9f);
        p.ampA = f (0.0005f, 0.004f); p.ampD = f (0.05f, 0.45f);
        p.ampS = 0.0f;                p.ampR = f (0.05f, 0.35f);
        p.modA = 0.001f; p.modD = f (0.02f, 0.2f); p.modS = 0.0f;
        p.filterType = chance (0.3f) ? 2 : 0;
        p.cutoff = f (1200.f, 9000.f); p.filterEnvAmt = f (0.3f, 0.9f);
        p.resonance = f (0.1f, 0.5f);
        p.unisonVoices = 1; p.subLevel = f (0.0f, 0.35f);
        p.reverbMix = f (0.1f, 0.35f); p.reverbSize = f (0.3f, 0.7f);
        p.velToAmp = f (0.5f, 0.9f); p.velToFilter = f (0.4f, 0.8f);
        pickFilter (p, 0.15f, 0.10f, 0.0f, 0.30f);           // comb = struck metal
        p.drive = f (0.1f, 0.45f); p.compAmount = f (0.2f, 0.55f);
        pickFx (p, 0.08f, 0.10f, 0.20f, 0.12f);
    }

    // Saw stack with a filter that swells into the note.
    void makeBrass (Patch& p)
    {
        setThreeOsc (p, 2, 2, chance (0.5f) ? 2 : 3);
        p.osc[1].fine = f (-14.0f, 14.0f);
        p.ampA = f (0.04f, 0.2f); p.ampD = f (0.15f, 0.5f);
        p.ampS = f (0.6f, 0.9f);  p.ampR = f (0.1f, 0.4f);
        p.modA = f (0.03f, 0.18f); p.modD = f (0.2f, 0.7f); p.modS = f (0.3f, 0.7f);
        p.filterType = 0; p.cutoff = f (600.f, 2200.f);
        p.filterEnvAmt = f (0.5f, 0.95f);                    // the swell
        p.resonance = f (0.15f, 0.45f); p.keytrack = f (0.3f, 0.8f);
        p.unisonVoices = chance (0.5f) ? 3 : 5; p.unisonDetune = f (6.0f, 18.0f);
        p.subLevel = f (0.0f, 0.2f);
        p.stereoWidth = f (0.5f, 0.9f);
        p.reverbMix = f (0.2f, 0.4f); p.reverbSize = f (0.4f, 0.75f);
        p.velToAmp = f (0.35f, 0.75f); p.velToFilter = f (0.4f, 0.8f);
        pickFilter (p, 0.35f, 0.15f, 0.0f, 0.0f);
        p.drive = f (0.15f, 0.5f); p.compAmount = f (0.2f, 0.5f);
        pickFx (p, 0.15f, 0.15f, 0.05f, 0.10f);
    }
};
