#pragma once
#include <JuceHeader.h>
#include "Patch.h"
#include "DSP.h"

struct GambleSound : public juce::SynthesiserSound
{
    bool appliesToNote (int)    override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class GambleVoice : public juce::SynthesiserVoice
{
public:
    explicit GambleVoice (const Patch& p) : patch (p) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<GambleSound*> (s) != nullptr;
    }

    void setCurrentPlaybackSampleRate (double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
        if (newRate > 0.0)
        {
            ampEnv.setSampleRate (newRate);
            modEnv.setSampleRate (newRate);
        }
        filterL.reset();
        filterR.reset();
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        // A stolen voice is still fading out its old note — queue this one and
        // start it the moment we reach silence (see finishFade).
        if (fading)
        {
            pendingNote = midiNote;
            pendingVel  = velocity;
            hasPending  = true;
            return;
        }
        triggerNote (midiNote, velocity, true);
    }

    // Shared trigger logic, also driven directly by the mono/legato path.
    // retrigger=false → legato: keep the envelope & phases, just glide to the new note.
    void triggerNote (int midiNote, float velocity, bool retrigger)
    {
        targetFreq = (float) juce::MidiMessage::getMidiNoteInHertz (midiNote);
        const bool fresh = ! ampEnv.isActive();
        noteNumber = midiNote;

        if (retrigger || fresh)
        {
            level    = velocity;
            lfoPhase = 0.0f;
            if (fresh || patch.glideTime <= 0.0005f)
                baseFreq = targetFreq;          // jump straight to pitch
            // else: keep baseFreq so we glide from the previous note

            // Only re-randomise phases / clear the filter when the voice was
            // silent. Doing it under a sounding note (mono retrigger) would
            // jump the waveform mid-cycle — an audible click.
            if (fresh)
            {
                // Seed from patch + note so the phases stay varied between
                // sounds but a given seed always renders identically — "try
                // seed 4821" should reproduce down to the sample.
                rng.setSeed ((juce::int64) ((juce::uint32) patch.seed * 2654435761u
                                            + (juce::uint32) midiNote * 40503u + 1u));
                for (auto& o : osc) o.reset (&rng);
                subOsc.reset (&rng);
                filterL.reset();
                filterR.reset();

                // Each modulator gets its own stream, keyed on the patch, the
                // slot and the note, so voices never move in lockstep but a
                // given seed still renders identically.
                for (int m = 0; m < Patch::NumMods; ++m)
                    mods[m].reset ((juce::uint32) (patch.seed * 2654435761u
                                                   + (juce::uint32) (m * 40503)
                                                   + (juce::uint32) midiNote),
                                   patch.mod[m].phase);

                for (auto& e : oscEnv) e = 1.0f;
                wtPosSm = patch.wtPos;
                noiseSrc.reset();

                if (patch.pluckLevel > 0.0001f)
                    string.pluck (targetFreq, getSampleRate(), patch.pluckBrightness, rng);
                else
                    string.reset();
            }

            ampEnv.setParameters ({ patch.ampA, patch.ampD, patch.ampS, patch.ampR });
            modEnv.setParameters ({ patch.modA, patch.modD, patch.modS, patch.modR });
            ampEnv.noteOn();
            modEnv.noteOn();
        }
    }

    void releaseNote()
    {
        ampEnv.noteOff();
        modEnv.noteOff();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
            modEnv.noteOff();
        }
        else
        {
            beginFastFade();   // voice steal / panic: fade, never cut mid-waveform
        }
    }

    // Kill everything instantly. Only safe when the output is already silent
    // (the processor calls this at the bottom of a roll cut).
    void hardReset()
    {
        ampEnv.reset();
        modEnv.reset();
        filterL.reset();
        filterR.reset();
        string.reset();
        noiseSrc.reset();
        fading = false; hasPending = false; fadeGain = 1.0f;
        clearCurrentNote();
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
    {
        if (! ampEnv.isActive() && ! fading)
            return;

        const double sr = getSampleRate();

        // Per-osc frequency ratios (patch only changes on a lever pull).
        float ratio[3];
        for (int i = 0; i < 3; ++i)
            ratio[i] = std::pow (2.0f, (patch.osc[i].semi + patch.osc[i].fine * 0.01f) / 12.0f);

        float modInc[Patch::NumMods];
        for (int k = 0; k < Patch::NumMods; ++k)
            modInc[k] = juce::jlimit (0.0f, 0.49f, patch.mod[k].rate / (float) sr);

        // Per-oscillator transient decay, as a per-sample multiplier.
        float oscEnvCoef[3];
        for (int k = 0; k < 3; ++k)
            oscEnvCoef[k] = (patch.osc[k].decay > 0.0005f)
                          ? (float) std::exp (-1.0 / (patch.osc[k].decay * sr))
                          : 1.0f;

        const float keyOct = patch.keytrack * (noteNumber - 60) / 12.0f;

        // Wavetable context: null unless an oscillator actually uses one.
        const auto& wts = Wavetables::get();
        const Wavetable* wtFor[3];
        for (int i = 0; i < 3; ++i)
            wtFor[i] = (patch.osc[i].wave == 5) ? &wts.table (patch.wtTable) : nullptr;
        // A short slew on position. Measurement says the wavetable does not
        // actually click when a stepped modulator jumps it — a table frame is a
        // continuous waveform, so moving between frames moves the *shape*, not
        // the sample value. Kept anyway because it costs nothing and stops the
        // very fastest modulators from buzzing at their own rate.
        const float wtSlew = 1.0f - (float) std::exp (-1.0 / (0.003 * sr));

        // Portamento coefficient (per-block); 1.0 = instant (no glide).
        const float glideCoef = (patch.glideTime > 0.0005f)
            ? (float) (1.0 - std::exp (-1.0 / (patch.glideTime * sr)))
            : 1.0f;

        // Pan positions + gains for the 3 oscillator slots (spread by stereoWidth).
        float basePan[3], gL[3], gR[3];
        for (int i = 0; i < 3; ++i)
        {
            basePan[i] = (i == 0 ? -patch.stereoWidth
                                 : i == 2 ?  patch.stereoWidth : 0.0f);
            const float a = (basePan[i] + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            gL[i] = std::cos (a);
            gR[i] = std::sin (a);
        }

        // Velocity is fixed for the note → precompute its effect on amp & filter.
        const float ampScale   = (1.0f - patch.velToAmp) + patch.velToAmp * level;
        const float velFiltMul = std::exp2 (patch.velToFilter * level * 2.0f);

        const int   uni    = patch.unisonVoices;
        const float uniDet = patch.unisonDetune;

        for (int n = 0; n < numSamples; ++n)
        {
            const float amp = ampEnv.getNextSample();
            const float me  = modEnv.getNextSample();

            // ---- Modulation: three slots, summed per destination ----
            float mPitch = 0.0f, mFilt = 0.0f, mAmp = 1.0f, mPw = 0.0f, mFm = 0.0f;
            float mPan = 0.0f, mDetune = 0.0f, mRes = 0.0f;
            float mOsc2 = 0.0f, mSub = 0.0f, mNoise = 0.0f, mWt = 0.0f;

            for (int k = 0; k < Patch::NumMods; ++k)
            {
                const auto& slot = patch.mod[k];
                if (slot.dest == ModNone || slot.depth < 0.001f)
                    continue;

                const float v = mods[k].next (modInc[k], slot.shape);
                const float d = slot.depth;

                switch (slot.dest)
                {
                    case ModPitch:      mPitch  += v * d * 0.06f;  break;   // +/- ~0.7 semitone
                    case ModCutoff:     mFilt   += v * d * 2.0f;   break;   // +/- 2 octaves
                    // Amp reaches silence at full depth, so this can gate and
                    // not merely wobble.
                    case ModAmp:        mAmp    *= juce::jmax (0.0f, 1.0f - d * (0.5f - 0.5f * v)); break;
                    case ModPulseWidth: mPw     += v * d * 0.4f;   break;
                    case ModFmAmount:   mFm     += v * d * 0.5f;   break;
                    case ModPan:        mPan    += v * d;          break;
                    case ModDetune:     mDetune += v * d * 25.0f;  break;
                    case ModResonance:  mRes    += v * d * 0.4f;   break;
                    case ModOsc2Level:  mOsc2   += v * d;          break;
                    case ModSubLevel:   mSub    += v * d * 0.5f;   break;
                    case ModNoise:      mNoise  += v * d * 0.3f;   break;
                    case ModWtPos:      mWt     += v * d * 0.5f;   break;
                    default: break;
                }
            }

            // The mod envelope's second routing, on top of its cutoff duty.
            if (patch.envDest != ModNone && std::abs (patch.envAmount) > 0.001f)
            {
                const float e = me * patch.envAmount;
                switch (patch.envDest)
                {
                    case ModPitch:      mPitch  += e * 0.25f;  break;   // blips and drops
                    case ModCutoff:     mFilt   += e * 2.0f;   break;
                    case ModAmp:        mAmp    *= juce::jlimit (0.0f, 1.0f, 1.0f - e); break;
                    case ModPulseWidth: mPw     += e * 0.4f;   break;
                    case ModFmAmount:   mFm     += e * 0.6f;   break;
                    case ModPan:        mPan    += e;          break;
                    case ModDetune:     mDetune += e * 25.0f;  break;
                    case ModResonance:  mRes    += e * 0.4f;   break;
                    case ModOsc2Level:  mOsc2   += e;          break;
                    case ModSubLevel:   mSub    += e * 0.5f;   break;
                    case ModNoise:      mNoise  += e * 0.3f;   break;
                    case ModWtPos:      mWt     += e * 0.6f;   break;
                    default: break;
                }
            }

            const float lfoPitch = mPitch;
            const float lfoFilt  = mFilt;
            const float lfoAmp   = mAmp;

            const float pw = juce::jlimit (0.05f, 0.95f, patch.pulseWidth + mPw);

            // Glide current pitch toward the target note.
            baseFreq += (targetFreq - baseFreq) * glideCoef;

            // Wavetable position, slewed over ~8 ms. A stepped modulator (S&H,
            // square) jumps the target; the slew turns that jump into a fast
            // morph instead of a waveform discontinuity, i.e. a click.
            const float wtTarget = juce::jlimit (0.0f, 1.0f, patch.wtPos + mWt);
            wtPosSm += (wtTarget - wtPosSm) * wtSlew;

            // Transient layers decay on their own clock.
            for (int k = 0; k < 3; ++k)
                if (oscEnvCoef[k] < 1.0f) oscEnv[k] *= oscEnvCoef[k];

            const float lv0 = patch.osc[0].level * oscEnv[0];
            const float lv1 = juce::jlimit (0.0f, 1.5f, patch.osc[1].level * oscEnv[1] + mOsc2);
            const float lv2 = patch.osc[2].level * oscEnv[2];
            const float fmA = juce::jlimit (0.0f, 1.6f, patch.fmAmount + mFm);
            const float uniDetMod = juce::jmax (0.0f, uniDet + mDetune);

            const float pitchMul = 1.0f + lfoPitch;
            const float f0 = baseFreq * ratio[0] * pitchMul;
            const float f1 = baseFreq * ratio[1] * pitchMul;
            const float f2 = baseFreq * ratio[2] * pitchMul;
            float sL = 0.0f, sR = 0.0f;

            switch (patch.oscMode)
            {
                case 1: // ring modulation: osc0 * osc1, plus osc2 underneath
                {
                    const float a = osc[0].next (f0, sr, patch.osc[0].wave, rng, 0.0f, pw, wtFor[0], wtPosSm);
                    const float b = osc[1].next (f1, sr, patch.osc[1].wave, rng, 0.0f, pw, wtFor[1], wtPosSm);
                    const float ring = a * b;
                    const float v01 = a * lv0 * (1.0f - fmA) + ring * 1.6f * fmA;
                    const float v2  = osc[2].next (f2, sr, patch.osc[2].wave, rng, 0.0f, pw, wtFor[2], wtPosSm) * lv2;
                    sL = v01 * gL[0] + v2 * gL[2];
                    sR = v01 * gR[0] + v2 * gR[2];
                    break;
                }
                case 2: // hard sync: osc1 phase reset by osc0 → bright metallic sweep
                {
                    const float master = osc[0].next (f0, sr, patch.osc[0].wave, rng, 0.0f, pw, wtFor[0], wtPosSm);
                    if (osc[0].wrapped) osc[1].syncReset();
                    const float slave = osc[1].next (f1 * (1.0f + fmA * 3.0f),
                                                     sr, patch.osc[1].wave, rng, 0.0f, pw,
                                                     wtFor[1], wtPosSm);
                    const float v1 = slave * lv1;
                    const float v0 = master * lv0 * 0.3f;
                    const float v2 = osc[2].next (f2, sr, patch.osc[2].wave, rng, 0.0f, pw, wtFor[2], wtPosSm) * lv2;
                    sL = v1 * gL[1] + v0 * gL[0] + v2 * gL[2];
                    sR = v1 * gR[1] + v0 * gR[0] + v2 * gR[2];
                    break;
                }
                case 3: // FM: osc1 modulates osc0's phase → bells, clangs, growls
                {
                    const float mod = osc[1].next (f1, sr, patch.osc[1].wave, rng, 0.0f, 0.5f, wtFor[1], wtPosSm);
                    const float car = osc[0].next (f0, sr, patch.osc[0].wave, rng,
                                                   mod * fmA, pw, wtFor[0], wtPosSm);
                    const float v0 = car * lv0;
                    const float v2 = osc[2].next (f2, sr, patch.osc[2].wave, rng, 0.0f, pw, wtFor[2], wtPosSm) * lv2 * 0.5f;
                    sL = v0 * gL[0] + v2 * gL[2];
                    sR = v0 * gR[0] + v2 * gR[2];
                    break;
                }
                default: // normal: three detuned oscillators, each with unison + stereo spread
                {
                    float t0L = 0, t0R = 0, t1L = 0, t1R = 0, t2L = 0, t2R = 0;
                    osc[0].nextUnison (f0, sr, patch.osc[0].wave, rng, uni, uniDetMod, basePan[0], 0.6f, pw, t0L, t0R, wtFor[0], wtPosSm, patch.chordType);
                    osc[1].nextUnison (f1, sr, patch.osc[1].wave, rng, uni, uniDetMod, basePan[1], 0.6f, pw, t1L, t1R, wtFor[1], wtPosSm, patch.chordType);
                    osc[2].nextUnison (f2, sr, patch.osc[2].wave, rng, uni, uniDetMod, basePan[2], 0.6f, pw, t2L, t2R, wtFor[2], wtPosSm, patch.chordType);
                    sL = t0L * lv0 + t1L * lv1 + t2L * lv2;
                    sR = t0R * lv0 + t1R * lv1 + t2R * lv2;
                    break;
                }
            }

            // Sub oscillator (one octave down) + noise layer — centred, pre-filter.
            const float subLvl   = juce::jlimit (0.0f, 1.0f, patch.subLevel + mSub);
            const float noiseLvl = juce::jlimit (0.0f, 1.0f, patch.noiseLevel + mNoise);

            if (subLvl > 0.0001f)
            {
                const float sub = subOsc.next (baseFreq * 0.5f * pitchMul, sr, patch.subWave, rng, 0.0f, pw)
                                  * subLvl;
                sL += sub; sR += sub;
            }
            if (noiseLvl > 0.0001f)
            {
                const float nz = noiseSrc.next (patch.noiseColour, rng) * noiseLvl;
                sL += nz; sR += nz;
            }
            if (patch.pluckLevel > 0.0001f)
            {
                // Centred and pre-filter, like the sub and noise layers.
                const float pl = string.next (patch.pluckDamping, patch.pluckDecay)
                                 * patch.pluckLevel;
                sL += pl; sR += pl;
            }

            sL *= 0.28f; sR *= 0.28f; // headroom (lowered: unison+sub+noise add level)

            // Filter: base cutoff * keytrack * mod-env * lfo * velocity (shared coeffs, per-ch state)
            float cutoff = patch.cutoff
                         * std::pow (2.0f, keyOct)
                         * std::pow (2.0f, me * patch.filterEnvAmt * 4.0f)
                         * std::pow (2.0f, lfoFilt)
                         * velFiltMul;
            const float res = juce::jlimit (0.0f, 0.92f, patch.resonance + mRes);
            filterL.set (patch.filterModel, patch.filterPoles, cutoff, res, patch.filterMorph, sr);
            filterR.set (patch.filterModel, patch.filterPoles, cutoff, res, patch.filterMorph, sr);
            sL = filterL.process (sL, patch.filterType);
            sR = filterR.process (sR, patch.filterType);

            const float g = amp * ampScale * lfoAmp * fadeGain;
            sL *= g; sR *= g;

            if (mPan != 0.0f)          // equal-power sweep across the field
            {
                const float a = (juce::jlimit (-1.0f, 1.0f, mPan) + 1.0f) * 0.25f
                                * juce::MathConstants<float>::pi;
                sL *= std::cos (a) * 1.414f;
                sR *= std::sin (a) * 1.414f;
            }

            if (out.getNumChannels() > 1)
            {
                out.addSample (0, startSample + n, sL);
                out.addSample (1, startSample + n, sR);
            }
            else
            {
                out.addSample (0, startSample + n, 0.5f * (sL + sR));
            }

            if (fading)
            {
                fadeGain -= fadeStep;
                if (fadeGain <= 0.0f)
                {
                    // Silent now: swap in the note that stole this voice and
                    // render the rest of the block from its fresh state.
                    finishFade();
                    renderNextBlock (out, startSample + n + 1, numSamples - n - 1);
                    return;
                }
            }
        }

        if (! ampEnv.isActive())
            clearCurrentNote();
    }

private:
    // ~4 ms ramp to silence, used whenever a note has to stop *now*.
    void beginFastFade()
    {
        if (fading) return;                       // already on the way out
        if (! ampEnv.isActive()) { hardReset(); return; }

        fadeGain = 1.0f;
        fadeStep = 1.0f / juce::jmax (1.0f, (float) (getSampleRate() * 0.004));
        fading   = true;
    }

    void finishFade()
    {
        fadeGain = 0.0f;
        fading   = false;

        if (hasPending)
        {
            hasPending = false;
            ampEnv.reset();                       // start from silence, not mid-tail
            modEnv.reset();
            fadeGain = 1.0f;
            triggerNote (pendingNote, pendingVel, true);
        }
        else
        {
            hardReset();
        }
    }

    const Patch& patch;
    juce::ADSR ampEnv, modEnv;
    Oscillator osc[3];
    Oscillator subOsc;
    NoiseSource noiseSrc;
    PluckedString string;
    MultiFilter filterL, filterR;
    juce::Random rng;

    ModOsc mods[Patch::NumMods];
    float  oscEnv[3] { 1.0f, 1.0f, 1.0f };

    int   noteNumber = 60;
    float baseFreq   = 440.0f;
    float targetFreq = 440.0f;
    float level      = 1.0f;
    float lfoPhase   = 0.0f;

    float wtPosSm = 0.3f;    // slewed wavetable position

    // Fast fade-out state (voice stealing)
    bool  fading     = false;
    float fadeGain   = 1.0f;
    float fadeStep   = 1.0f;
    bool  hasPending = false;
    int   pendingNote = 60;
    float pendingVel  = 1.0f;
};
