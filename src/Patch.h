#pragma once
#include <JuceHeader.h>

// All synth parameters for one sound. Plain data; the Randomizer fills it,
// the voices read it. Kept as simple values so a "patch" is trivial to
// copy, save, and (later) serialise.
// What a modulator can move. Every one of these is voice-local on purpose, so
// modulation is per-note rather than one global wobble across the whole chord.
enum ModDest
{
    ModNone = 0,
    ModPitch,        // +/- semitones
    ModCutoff,       // +/- octaves
    ModAmp,          // tremolo, or a gate at full depth
    ModPulseWidth,
    ModFmAmount,     // moves ring / sync / FM depth
    ModPan,
    ModDetune,       // unison spread
    ModResonance,
    ModOsc2Level,    // layer fades in and out
    ModSubLevel,
    ModNoise,
    ModWtPos,        // wavetable position — the whole point of having one
    NumModDests
};

// Sine and triangle sweep; square trills; ramps sweep and reset; sample & hold
// steps at random; the walk drifts. The shape is most of the character.
enum ModShape { ModSine = 0, ModTri, ModSquare, ModRampUp, ModRampDown,
                ModSampleHold, ModRandomWalk, NumModShapes };

struct ModSlot
{
    int   shape   = ModSine;
    int   dest    = ModNone;
    float rate    = 4.0f;   // Hz, when free-running
    float depth   = 0.0f;   // 0..1
    float phase   = 0.0f;   // 0..1 start offset, so slots don't move in lockstep
    int   syncDiv = 0;      // note division; 0 = free-running
};

struct Patch
{
    struct Osc
    {
        int   wave  = 2;     // 0 sine, 1 tri, 2 saw, 3 square, 4 noise
        int   semi  = 0;     // semitone offset
        float fine  = 0.0f;  // fine detune, cents
        float level = 0.7f;  // 0..1

        // 0 = follow the amp envelope. Above that, this oscillator gets its own
        // decay in seconds — which is how you layer a transient (hammer, pick,
        // mallet) over a sustained body instead of every layer moving together.
        float decay = 0.0f;
    };
    Osc osc[3];

    // Three independent modulators.
    static constexpr int NumMods = 3;
    ModSlot mod[NumMods];

    // The mod envelope always drives cutoff via filterEnvAmt; this routes it to
    // one more place, which is how you get pitch blips, evolving FM, and sweeps
    // that aren't just the filter opening.
    int   envDest   = ModNone;
    float envAmount = 0.0f;   // -1..1

    // Oscillator interaction mode — this is what makes patches sound different
    // from each other, not just "another subtractive saw".
    int   oscMode  = 0;     // 0 normal, 1 ring-mod, 2 hard-sync, 3 FM
    float fmAmount = 0.0f;  // 0..1, used by ring/sync depth and FM index

    // Filter
    int   filterType   = 0;      // 0 LP, 1 BP, 2 HP
    int   filterModel  = 0;      // 0 SVF, 1 ladder, 2 diode, 3 formant, 4 comb
    int   filterPoles  = 2;      // 2 or 4 (SVF cascade / ladder taps)
    float filterMorph  = 0.3f;   // formant vowel select / comb tone
    float cutoff       = 1200.f; // Hz
    float resonance    = 0.2f;   // 0..1
    float filterEnvAmt = 0.0f;   // -1..1 (octaves = amt * 4)
    float keytrack     = 0.4f;   // 0..1

    // Envelopes
    float ampA = 0.005f, ampD = 0.20f, ampS = 0.80f, ampR = 0.30f;
    float modA = 0.005f, modD = 0.30f, modS = 0.30f, modR = 0.30f;

    // Unison (supersaw)
    int   unisonVoices = 1;    // 1..7 detuned copies per oscillator
    float unisonDetune = 0.0f; // cents of spread

    // Sub oscillator & noise layer
    int   subWave    = 0;      // 0 sine, 3 square
    float subLevel   = 0.0f;   // one octave below, adds weight
    float noiseLevel = 0.0f;   // noise texture (pre-filter)
    int   noiseColour = 0;     // 0 white, 1 pink, 2 brown, 3 crackle

    // Plucked string (Karplus-Strong). A different kind of sound from the
    // oscillators: excitation decaying in a tuned loop, not a filtered wave.
    float pluckLevel      = 0.0f;   // 0 = off
    float pluckDamping    = 0.5f;   // high end die-off; nylon .. steel
    float pluckDecay      = 0.6f;   // how long it rings
    float pluckBrightness = 0.5f;   // how sharp the pick is

    // Chord unison: unison voices tuned to intervals instead of detuned, so one
    // key gives a chord. 0 = off (plain detuned unison).
    int   chordType = 0;       // 0 off, 1 fifth, 2 octaves, 3 major, 4 minor, 5 sus4

    // PWM (square wave only)
    float pulseWidth = 0.5f;   // 0.05..0.95
    float pwmDepth   = 0.0f;   // LFO-driven width movement

    // Velocity sensitivity
    float velToFilter = 0.3f;  // 0..1, harder = brighter
    float velToAmp    = 0.3f;  // 0..1, harder = louder

    // Voice mode (Tier-B, archetype-gated)
    int   voiceMode = 0;       // 0 poly, 1 mono (retrigger), 2 legato
    float glideTime = 0.0f;    // portamento seconds (0 = off)

    // Stereo / movement
    float stereoWidth = 0.6f; // 0..1, spreads the 3 oscillators across L/R

    // FX — character (pre-modulation)
    float foldAmount = 0.0f;   // 0..1 wavefolder
    float crushBits  = 16.0f;  // 16 = off, down to 2
    float crushRate  = 1.0f;   // 1 = off, up to ~32 (sample-rate divisor)

    // FX — modulation
    float phaserMix   = 0.0f;  // 0..1
    float phaserRate  = 0.4f;  // Hz
    float phaserDepth = 0.6f;  // 0..1
    float phaserFb    = 0.3f;  // 0..1
    float flangerMix  = 0.0f;  // 0..1
    float flangerRate = 0.25f; // Hz
    float flangerDepth= 0.5f;  // 0..1
    float flangerFb   = 0.4f;  // 0..1

    // Wavetable, shared by any oscillator whose wave is 5. The position is
    // where in the table's morph you are; ModWtPos moves it.
    int   wtTable = 0;      // which of the generated tables
    float wtPos   = 0.3f;   // 0..1 morph position

    // Arpeggiator. Rolled onto a minority of patches and only where it suits
    // one: the pattern is re-timed from whatever is held, never invented.
    int   arpMode    = 0;      // 0 off, 1 up, 2 down, 3 up-down, 4 random
    int   arpDiv     = 3;      // note division; kept to 1/16, 1/8T, 1/8
    float arpGate    = 0.6f;   // note length as a fraction of the step
    int   arpOctaves = 1;      // 1..3

    // Tempo sync for the delay (0 = free-running, else a note division).
    // Modulators carry their own syncDiv; vibrato and drift stay free on purpose.
    int   delaySyncDiv = 0;

    // FX — dynamics
    float compAmount = 0.0f;   // 0..1 macro: threshold + ratio + make-up

    // FX
    float chorusMix = 0.0f;   // 0..1, ensemble shimmer / width
    float drive     = 0.0f;   // 0..1
    float delayTime = 0.30f;  // seconds
    float delayFb   = 0.30f;  // 0..1
    float delayMix  = 0.0f;   // 0..1
    float reverbSize= 0.4f;   // 0..1
    float reverbMix = 0.2f;   // 0..1
    float master    = 0.8f;   // 0..1

    // Metadata (not sound-affecting; for display / sharing / recall)
    int  seed  = 0;      // the number this patch was generated from
    bool chaos = false;  // was it a CHAOS roll?
    juce::String archetypeName { "Pad" };
    juce::String modifierName;   // post-roll twist, empty if the roll got none
};

// ---- Lockable reels: ROLL keeps whatever is locked and re-rolls the rest.
// Grouped the way a player thinks about a sound, not the way the struct is laid
// out — "the filter" is one lock, not eight. ----
enum Reel { ReelOsc = 0, ReelFilter, ReelEnv, ReelMod, ReelFx, NumReels };

inline const char* reelName (int reel)
{
    switch (reel)
    {
        case ReelOsc:    return "OSC";
        case ReelFilter: return "FILT";
        case ReelEnv:    return "ENV";
        case ReelMod:    return "MOD";
        default:         return "FX";
    }
}

// Copy one reel's worth of parameters from `from` into `into`.
inline void copyReel (Patch& into, const Patch& from, int reel)
{
    switch (reel)
    {
        case ReelOsc:
            for (int k = 0; k < 3; ++k) into.osc[k] = from.osc[k];   // includes per-osc decay
            into.oscMode = from.oscMode;             into.fmAmount = from.fmAmount;
            into.unisonVoices = from.unisonVoices;   into.unisonDetune = from.unisonDetune;
            into.subWave = from.subWave;             into.subLevel = from.subLevel;
            into.noiseLevel = from.noiseLevel;       into.noiseColour = from.noiseColour;
            into.pluckLevel = from.pluckLevel;       into.pluckDamping = from.pluckDamping;
            into.pluckDecay = from.pluckDecay;       into.pluckBrightness = from.pluckBrightness;
            into.chordType = from.chordType;
            into.pulseWidth = from.pulseWidth;       into.pwmDepth = from.pwmDepth;
            into.wtTable = from.wtTable;             into.wtPos = from.wtPos;
            break;

        case ReelFilter:
            into.filterType = from.filterType;       into.filterModel = from.filterModel;
            into.filterPoles = from.filterPoles;     into.filterMorph = from.filterMorph;
            into.cutoff = from.cutoff;               into.resonance = from.resonance;
            into.filterEnvAmt = from.filterEnvAmt;   into.keytrack = from.keytrack;
            break;

        case ReelEnv:
            into.ampA = from.ampA; into.ampD = from.ampD; into.ampS = from.ampS; into.ampR = from.ampR;
            into.modA = from.modA; into.modD = from.modD; into.modS = from.modS; into.modR = from.modR;
            into.velToFilter = from.velToFilter;     into.velToAmp = from.velToAmp;
            break;

        case ReelMod:
            for (int k = 0; k < Patch::NumMods; ++k) into.mod[k] = from.mod[k];
            into.envDest = from.envDest;             into.envAmount = from.envAmount;
            into.arpMode = from.arpMode;             into.arpDiv = from.arpDiv;
            into.arpGate = from.arpGate;             into.arpOctaves = from.arpOctaves;
            into.voiceMode = from.voiceMode;         into.glideTime = from.glideTime;
            into.stereoWidth = from.stereoWidth;
            break;

        default: // ReelFx
            into.foldAmount = from.foldAmount;
            into.crushBits = from.crushBits;         into.crushRate = from.crushRate;
            into.phaserMix = from.phaserMix;         into.phaserRate = from.phaserRate;
            into.phaserDepth = from.phaserDepth;     into.phaserFb = from.phaserFb;
            into.flangerMix = from.flangerMix;       into.flangerRate = from.flangerRate;
            into.flangerDepth = from.flangerDepth;   into.flangerFb = from.flangerFb;
            into.compAmount = from.compAmount;       into.chorusMix = from.chorusMix;
            into.drive = from.drive;
            into.delayTime = from.delayTime;         into.delayFb = from.delayFb;
            into.delayMix = from.delayMix;           into.delaySyncDiv = from.delaySyncDiv;
            into.reverbSize = from.reverbSize;       into.reverbMix = from.reverbMix;
            into.master = from.master;
            break;
    }
}

// Note divisions in beats (a quarter note = 1 beat). 0 = free-running.
inline float syncDivBeats (int div)
{
    switch (div)
    {
        case 1:  return 0.25f;          // 1/16
        case 2:  return 1.0f / 3.0f;    // 1/8 triplet
        case 3:  return 0.5f;           // 1/8
        case 4:  return 0.75f;          // dotted 1/8
        case 5:  return 1.0f;           // 1/4
        case 6:  return 1.5f;           // dotted 1/4
        case 7:  return 2.0f;           // 1/2
        default: return 0.0f;           // free
    }
}

// ---- Binary (de)serialisation — stores full parameters so saved sounds stay
// identical even if the randomizer algorithm changes later. ----
inline void writePatch (juce::OutputStream& s, const Patch& p)
{
    s.writeInt (12); // format version
    for (const auto& o : p.osc)
    {
        s.writeInt (o.wave); s.writeInt (o.semi);
        s.writeFloat (o.fine); s.writeFloat (o.level);
    }
    s.writeInt (p.oscMode); s.writeFloat (p.fmAmount);
    s.writeInt (p.filterType);
    s.writeFloat (p.cutoff); s.writeFloat (p.resonance);
    s.writeFloat (p.filterEnvAmt); s.writeFloat (p.keytrack);
    s.writeFloat (p.ampA); s.writeFloat (p.ampD); s.writeFloat (p.ampS); s.writeFloat (p.ampR);
    s.writeFloat (p.modA); s.writeFloat (p.modD); s.writeFloat (p.modS); s.writeFloat (p.modR);
    s.writeInt (p.unisonVoices); s.writeFloat (p.unisonDetune);
    s.writeInt (p.subWave); s.writeFloat (p.subLevel); s.writeFloat (p.noiseLevel);
    s.writeFloat (p.pulseWidth); s.writeFloat (p.pwmDepth);
    s.writeFloat (p.velToFilter); s.writeFloat (p.velToAmp);
    s.writeInt (p.voiceMode); s.writeFloat (p.glideTime);
    s.writeFloat (p.stereoWidth);
    s.writeFloat (p.chorusMix); s.writeFloat (p.drive);
    s.writeFloat (p.delayTime); s.writeFloat (p.delayFb); s.writeFloat (p.delayMix);
    s.writeFloat (p.reverbSize); s.writeFloat (p.reverbMix);
    s.writeFloat (p.master);
    s.writeInt (p.seed); s.writeInt (p.chaos ? 1 : 0);
    s.writeString (p.archetypeName);
    // v5+ — appended at the end so older data still reads back (defaults kick in).
    s.writeInt (p.filterModel); s.writeInt (p.filterPoles); s.writeFloat (p.filterMorph);
    s.writeString (p.modifierName);   // v6
    // v7 — the Tier-4 FX rack
    s.writeFloat (p.foldAmount); s.writeFloat (p.crushBits); s.writeFloat (p.crushRate);
    s.writeFloat (p.phaserMix);  s.writeFloat (p.phaserRate);
    s.writeFloat (p.phaserDepth); s.writeFloat (p.phaserFb);
    s.writeFloat (p.flangerMix); s.writeFloat (p.flangerRate);
    s.writeFloat (p.flangerDepth); s.writeFloat (p.flangerFb);
    s.writeFloat (p.compAmount);
    s.writeInt (p.delaySyncDiv);   // v8
    // v9 — the modulation overhaul
    for (const auto& m : p.mod)
    {
        s.writeInt (m.shape); s.writeInt (m.dest);
        s.writeFloat (m.rate); s.writeFloat (m.depth);
        s.writeFloat (m.phase); s.writeInt (m.syncDiv);
    }
    for (const auto& o : p.osc) s.writeFloat (o.decay);
    s.writeInt (p.envDest); s.writeFloat (p.envAmount);
    // v10
    s.writeInt (p.arpMode); s.writeInt (p.arpDiv);
    s.writeFloat (p.arpGate); s.writeInt (p.arpOctaves);
    // v11
    s.writeInt (p.wtTable); s.writeFloat (p.wtPos);
    // v12
    s.writeInt (p.noiseColour);
    s.writeFloat (p.pluckLevel); s.writeFloat (p.pluckDamping);
    s.writeFloat (p.pluckDecay); s.writeFloat (p.pluckBrightness);
    s.writeInt (p.chordType);
}

inline Patch readPatch (juce::InputStream& s)
{
    Patch p;
    const int version = s.readInt();
    for (auto& o : p.osc)
    {
        o.wave = s.readInt(); o.semi = s.readInt();
        o.fine = s.readFloat(); o.level = s.readFloat();
    }
    p.oscMode = s.readInt(); p.fmAmount = s.readFloat();
    p.filterType = s.readInt();
    p.cutoff = s.readFloat(); p.resonance = s.readFloat();
    p.filterEnvAmt = s.readFloat(); p.keytrack = s.readFloat();
    p.ampA = s.readFloat(); p.ampD = s.readFloat(); p.ampS = s.readFloat(); p.ampR = s.readFloat();
    p.modA = s.readFloat(); p.modD = s.readFloat(); p.modS = s.readFloat(); p.modR = s.readFloat();
    p.unisonVoices = s.readInt(); p.unisonDetune = s.readFloat();
    p.subWave = s.readInt(); p.subLevel = s.readFloat(); p.noiseLevel = s.readFloat();
    p.pulseWidth = s.readFloat(); p.pwmDepth = s.readFloat();
    p.velToFilter = s.readFloat(); p.velToAmp = s.readFloat();
    p.voiceMode = s.readInt(); p.glideTime = s.readFloat();
    p.stereoWidth = s.readFloat();
    p.chorusMix = s.readFloat(); p.drive = s.readFloat();
    p.delayTime = s.readFloat(); p.delayFb = s.readFloat(); p.delayMix = s.readFloat();
    p.reverbSize = s.readFloat(); p.reverbMix = s.readFloat();
    p.master = s.readFloat();
    p.seed = s.readInt(); p.chaos = s.readInt() != 0;
    p.archetypeName = s.readString();
    if (version >= 5)
    {
        p.filterModel = s.readInt(); p.filterPoles = s.readInt();
        p.filterMorph = s.readFloat();
    }
    if (version >= 6)
        p.modifierName = s.readString();
    if (version >= 7)
    {
        p.foldAmount = s.readFloat(); p.crushBits = s.readFloat(); p.crushRate = s.readFloat();
        p.phaserMix  = s.readFloat(); p.phaserRate = s.readFloat();
        p.phaserDepth = s.readFloat(); p.phaserFb = s.readFloat();
        p.flangerMix = s.readFloat(); p.flangerRate = s.readFloat();
        p.flangerDepth = s.readFloat(); p.flangerFb = s.readFloat();
        p.compAmount = s.readFloat();
    }
    if (version >= 8)
    {
        p.delaySyncDiv = s.readInt();
    }
    if (version >= 9)
    {
        for (auto& m : p.mod)
        {
            m.shape = s.readInt(); m.dest = s.readInt();
            m.rate = s.readFloat(); m.depth = s.readFloat();
            m.phase = s.readFloat(); m.syncDiv = s.readInt();
        }
        for (auto& o : p.osc) o.decay = s.readFloat();
        p.envDest = s.readInt(); p.envAmount = s.readFloat();
    }
    if (version >= 10)
    {
        p.arpMode = s.readInt(); p.arpDiv = s.readInt();
        p.arpGate = s.readFloat(); p.arpOctaves = s.readInt();
    }
    if (version >= 11)
    {
        p.wtTable = s.readInt(); p.wtPos = s.readFloat();
    }
    if (version >= 12)
    {
        p.noiseColour = s.readInt();
        p.pluckLevel = s.readFloat(); p.pluckDamping = s.readFloat();
        p.pluckDecay = s.readFloat(); p.pluckBrightness = s.readFloat();
        p.chordType = s.readInt();
    }
    return p;
}
