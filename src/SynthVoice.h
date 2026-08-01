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

        const float lfoInc = patch.lfoRate / (float) sr;
        const float keyOct = patch.keytrack * (noteNumber - 60) / 12.0f;

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

            // LFO
            const float lfo = std::sin (lfoPhase * juce::MathConstants<float>::twoPi);
            lfoPhase += lfoInc; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
            const float lfoPitch  = (patch.lfoDest == 0) ? lfo * patch.lfoDepth * 0.03f : 0.0f;
            const float lfoFilt   = (patch.lfoDest == 1) ? lfo * patch.lfoDepth : 0.0f;
            // Amp LFO reaches all the way to silence at full depth — at half
            // that it could only ever wobble, never gate.
            const float lfoAmp    = (patch.lfoDest == 2)
                                  ? juce::jmax (0.0f, 1.0f - patch.lfoDepth * (0.5f - 0.5f * lfo))
                                  : 1.0f;

            // Pulse width, moved by the LFO when pwmDepth > 0.
            const float pw = juce::jlimit (0.05f, 0.95f, patch.pulseWidth + patch.pwmDepth * lfo * 0.4f);

            // Glide current pitch toward the target note.
            baseFreq += (targetFreq - baseFreq) * glideCoef;

            const float pitchMul = 1.0f + lfoPitch;
            const float f0 = baseFreq * ratio[0] * pitchMul;
            const float f1 = baseFreq * ratio[1] * pitchMul;
            const float f2 = baseFreq * ratio[2] * pitchMul;
            float sL = 0.0f, sR = 0.0f;

            switch (patch.oscMode)
            {
                case 1: // ring modulation: osc0 * osc1, plus osc2 underneath
                {
                    const float a = osc[0].next (f0, sr, patch.osc[0].wave, rng, 0.0f, pw);
                    const float b = osc[1].next (f1, sr, patch.osc[1].wave, rng, 0.0f, pw);
                    const float ring = a * b;
                    const float v01 = a * patch.osc[0].level * (1.0f - patch.fmAmount)
                                      + ring * 1.6f * patch.fmAmount;
                    const float v2  = osc[2].next (f2, sr, patch.osc[2].wave, rng, 0.0f, pw) * patch.osc[2].level;
                    sL = v01 * gL[0] + v2 * gL[2];
                    sR = v01 * gR[0] + v2 * gR[2];
                    break;
                }
                case 2: // hard sync: osc1 phase reset by osc0 → bright metallic sweep
                {
                    const float master = osc[0].next (f0, sr, patch.osc[0].wave, rng, 0.0f, pw);
                    if (osc[0].wrapped) osc[1].syncReset();
                    const float slave = osc[1].next (f1 * (1.0f + patch.fmAmount * 3.0f),
                                                     sr, patch.osc[1].wave, rng, 0.0f, pw);
                    const float v1 = slave * patch.osc[1].level;
                    const float v0 = master * patch.osc[0].level * 0.3f;
                    const float v2 = osc[2].next (f2, sr, patch.osc[2].wave, rng, 0.0f, pw) * patch.osc[2].level;
                    sL = v1 * gL[1] + v0 * gL[0] + v2 * gL[2];
                    sR = v1 * gR[1] + v0 * gR[0] + v2 * gR[2];
                    break;
                }
                case 3: // FM: osc1 modulates osc0's phase → bells, clangs, growls
                {
                    const float mod = osc[1].next (f1, sr, patch.osc[1].wave, rng);
                    const float car = osc[0].next (f0, sr, patch.osc[0].wave, rng,
                                                   mod * patch.fmAmount, pw);
                    const float v0 = car * patch.osc[0].level;
                    const float v2 = osc[2].next (f2, sr, patch.osc[2].wave, rng, 0.0f, pw) * patch.osc[2].level * 0.5f;
                    sL = v0 * gL[0] + v2 * gL[2];
                    sR = v0 * gR[0] + v2 * gR[2];
                    break;
                }
                default: // normal: three detuned oscillators, each with unison + stereo spread
                {
                    float t0L = 0, t0R = 0, t1L = 0, t1R = 0, t2L = 0, t2R = 0;
                    osc[0].nextUnison (f0, sr, patch.osc[0].wave, rng, uni, uniDet, basePan[0], 0.6f, pw, t0L, t0R);
                    osc[1].nextUnison (f1, sr, patch.osc[1].wave, rng, uni, uniDet, basePan[1], 0.6f, pw, t1L, t1R);
                    osc[2].nextUnison (f2, sr, patch.osc[2].wave, rng, uni, uniDet, basePan[2], 0.6f, pw, t2L, t2R);
                    sL = t0L * patch.osc[0].level + t1L * patch.osc[1].level + t2L * patch.osc[2].level;
                    sR = t0R * patch.osc[0].level + t1R * patch.osc[1].level + t2R * patch.osc[2].level;
                    break;
                }
            }

            // Sub oscillator (one octave down) + noise layer — centred, pre-filter.
            if (patch.subLevel > 0.0001f)
            {
                const float sub = subOsc.next (baseFreq * 0.5f * pitchMul, sr, patch.subWave, rng, 0.0f, pw)
                                  * patch.subLevel;
                sL += sub; sR += sub;
            }
            if (patch.noiseLevel > 0.0001f)
            {
                const float nz = (rng.nextFloat() * 2.0f - 1.0f) * patch.noiseLevel;
                sL += nz; sR += nz;
            }

            sL *= 0.28f; sR *= 0.28f; // headroom (lowered: unison+sub+noise add level)

            // Filter: base cutoff * keytrack * mod-env * lfo * velocity (shared coeffs, per-ch state)
            float cutoff = patch.cutoff
                         * std::pow (2.0f, keyOct)
                         * std::pow (2.0f, me * patch.filterEnvAmt * 4.0f)
                         * std::pow (2.0f, lfoFilt)
                         * velFiltMul;
            filterL.set (patch.filterModel, patch.filterPoles, cutoff, patch.resonance, patch.filterMorph, sr);
            filterR.set (patch.filterModel, patch.filterPoles, cutoff, patch.resonance, patch.filterMorph, sr);
            sL = filterL.process (sL, patch.filterType);
            sR = filterR.process (sR, patch.filterType);

            const float g = amp * ampScale * lfoAmp * fadeGain;
            sL *= g; sR *= g;

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
    MultiFilter filterL, filterR;
    juce::Random rng;

    int   noteNumber = 60;
    float baseFreq   = 440.0f;
    float targetFreq = 440.0f;
    float level      = 1.0f;
    float lfoPhase   = 0.0f;

    // Fast fade-out state (voice stealing)
    bool  fading     = false;
    float fadeGain   = 1.0f;
    float fadeStep   = 1.0f;
    bool  hasPending = false;
    int   pendingNote = 60;
    float pendingVel  = 1.0f;
};
