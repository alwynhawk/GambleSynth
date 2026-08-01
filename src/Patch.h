#pragma once
#include <JuceHeader.h>

// All synth parameters for one sound. Plain data; the Randomizer fills it,
// the voices read it. Kept as simple values so a "patch" is trivial to
// copy, save, and (later) serialise.
struct Patch
{
    struct Osc
    {
        int   wave  = 2;     // 0 sine, 1 tri, 2 saw, 3 square, 4 noise
        int   semi  = 0;     // semitone offset
        float fine  = 0.0f;  // fine detune, cents
        float level = 0.7f;  // 0..1
    };
    Osc osc[3];

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

    // LFO
    float lfoRate  = 4.0f;  // Hz
    float lfoDepth = 0.0f;  // 0..1
    int   lfoDest  = 1;     // 0 pitch, 1 filter, 2 amp

    // Unison (supersaw)
    int   unisonVoices = 1;    // 1..7 detuned copies per oscillator
    float unisonDetune = 0.0f; // cents of spread

    // Sub oscillator & noise layer
    int   subWave    = 0;      // 0 sine, 3 square
    float subLevel   = 0.0f;   // one octave below, adds weight
    float noiseLevel = 0.0f;   // white noise texture (pre-filter)

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

    // Tempo sync (0 = free-running; otherwise a note division). Only the delay
    // and the gate tremolo sync — vibrato/PWM/filter wobble sound better free.
    int   delaySyncDiv = 0;
    int   gateSyncDiv  = 0;

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
            for (int k = 0; k < 3; ++k) into.osc[k] = from.osc[k];
            into.oscMode = from.oscMode;             into.fmAmount = from.fmAmount;
            into.unisonVoices = from.unisonVoices;   into.unisonDetune = from.unisonDetune;
            into.subWave = from.subWave;             into.subLevel = from.subLevel;
            into.noiseLevel = from.noiseLevel;
            into.pulseWidth = from.pulseWidth;       into.pwmDepth = from.pwmDepth;
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
            into.lfoRate = from.lfoRate;             into.lfoDepth = from.lfoDepth;
            into.lfoDest = from.lfoDest;             into.gateSyncDiv = from.gateSyncDiv;
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
    s.writeInt (8); // format version
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
    s.writeFloat (p.lfoRate); s.writeFloat (p.lfoDepth); s.writeInt (p.lfoDest);
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
    s.writeInt (p.delaySyncDiv); s.writeInt (p.gateSyncDiv);   // v8
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
    p.lfoRate = s.readFloat(); p.lfoDepth = s.readFloat(); p.lfoDest = s.readInt();
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
        p.delaySyncDiv = s.readInt(); p.gateSyncDiv = s.readInt();
    }
    return p;
}
