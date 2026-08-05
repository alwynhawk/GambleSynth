#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SynthVoice.h"
#include <algorithm>

GambleSynthProcessor::GambleSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    synth.addSound (new GambleSound());
    for (int i = 0; i < 16; ++i)
    {
        auto* v = new GambleVoice (active);
        voicePtrs.push_back (v);
        synth.addVoice (v);
    }

    monoVoice = std::make_unique<GambleVoice> (active);

    // Audibility probe: its own voice + buffer, fixed 44.1 kHz so the verdict
    // doesn't depend on the host's sample rate.
    probeVoice = std::make_unique<GambleVoice> (probePatch);
    probeVoice->setCurrentPlaybackSampleRate (44100.0);
    probeBuffer.setSize (2, 44100);

    commit (rollAudible());      // start on a random sound
    active = patch;              // nothing is sounding yet, so no cut needed
    patchDirty = false;
}

GambleSynthProcessor::~GambleSynthProcessor() = default;

void GambleSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = juce::jmax (samplesPerBlock, 64);

    // The voices generate at the oversampled rate — sync, FM and ring modulation
    // create their aliasing at the source, so upsampling afterwards can't help.
    const double voiceRate = oversample ? sampleRate * 2.0 : sampleRate;
    synth.setCurrentPlaybackSampleRate (voiceRate);
    monoVoice->setCurrentPlaybackSampleRate (voiceRate);

    osBuffer.setSize (2, maxBlockSize * 2, false, true, true);
    monoScratch.setSize (1, maxBlockSize, false, true, true);
    decimator.prepare();
    setLatencySamples (oversample ? Decimator2x::latencySamples() : 0);

    reverb.setSampleRate (sampleRate);
    chorus.prepare (sampleRate);
    phaser.prepare (sampleRate);
    flanger.prepare (sampleRate);
    compressor.prepare (sampleRate);
    crusher.reset();
    dcBlocker.reset();

    const int maxDelay = (int) (sampleRate * 1.0) + 4;
    delayL.assign ((size_t) maxDelay, 0.0f);
    delayR.assign ((size_t) maxDelay, 0.0f);
    delayWrite = 0;

    cutState = Cut::None;
    cutGain  = 1.0f;
}

bool GambleSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo is what this wants, but refusing everything else is how a plugin
    // ends up loaded-but-silent: a host that can't negotiate any layout it asked
    // for may never call processBlock at all. Accept mono too and downmix.
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    return layouts.getMainInputChannelSet().isDisabled();   // synth: no audio in
}

void GambleSynthProcessor::commit (const Patch& p)
{
    if (histPos < (int) history.size() - 1)
        history.resize ((size_t) (histPos + 1));   // drop any redo tail
    history.push_back (p);

    const int maxHistory = 64;
    if ((int) history.size() > maxHistory)
        history.erase (history.begin());

    histPos = (int) history.size() - 1;
    setCurrent (p);                                // audio thread picks it up via the cut
}

float GambleSynthProcessor::probeLevel (const Patch& p)
{
    probePatch = p;
    probeVoice->hardReset();
    probeVoice->triggerNote (60, 0.9f, true);

    probeBuffer.clear();
    probeVoice->renderNextBlock (probeBuffer, 0, probeBuffer.getNumSamples());

    const int n = probeBuffer.getNumSamples();
    return juce::jmax (probeBuffer.getRMSLevel (0, 0, n), probeBuffer.getRMSLevel (1, 0, n));
}

void GambleSynthProcessor::setOversampling (bool shouldOversample)
{
    if (oversample == shouldOversample)
        return;

    oversample = shouldOversample;
    if (currentSampleRate > 0.0)
        prepareToPlay (currentSampleRate, maxBlockSize);
}

void GambleSynthProcessor::setReelLocked (int reel, bool locked)
{
    if (reel < 0 || reel >= NumReels) return;
    if (locked) lockMask |= (1 << reel);
    else        lockMask &= ~(1 << reel);
    notifyChanged();
}

// Locked reels survive the roll: the fresh patch keeps its own new parameters
// everywhere except the groups the player pinned.
Patch GambleSynthProcessor::withLocks (Patch fresh) const
{
    for (int reel = 0; reel < NumReels; ++reel)
        if (isReelLocked (reel))
            copyReel (fresh, patch, reel);
    return fresh;
}

// A free roll draws again if the sound it landed on is inaudible. Explicit
// seeds are never re-rolled — a seed must always map to the same sound.
Patch GambleSynthProcessor::rollAudible()
{
    // Slightly above true silence: the probe only hears the voice, so a
    // marginal one can still be cancelled by a wet phaser/flanger downstream.
    const float minLevel = 0.013f;

    // Locks are applied *before* the probe: a locked filter over a fresh
    // oscillator can be silent even though the raw roll wasn't.
    Patch p = withLocks (chaosMode ? randomizer.rollChaos() : randomizer.roll());
    for (int tries = 0; tries < 8 && probeLevel (p) < minLevel; ++tries)
        p = withLocks (chaosMode ? randomizer.rollChaos() : randomizer.roll());

    randomizer.accept (p);     // only what actually plays counts as "recent"
    return p;
}

void GambleSynthProcessor::pullLever()
{
    commit (rollAudible());
}

void GambleSynthProcessor::rollSeed (unsigned seed)
{
    const Patch p = withLocks (chaosMode ? randomizer.rollChaos (seed) : randomizer.roll (seed));
    randomizer.accept (p);
    commit (p);
}

void GambleSynthProcessor::undo()
{
    if (canUndo()) { --histPos; setCurrent (history[(size_t) histPos]); }
}

void GambleSynthProcessor::redo()
{
    if (canRedo()) { ++histPos; setCurrent (history[(size_t) histPos]); }
}

// Hand a sound to the audio thread without touching history.
void GambleSynthProcessor::setCurrent (const Patch& p)
{
    {
        const juce::SpinLock::ScopedLockType lock (patchLock);
        patch = p;
    }
    patchDirty = true;
    notifyChanged();
}

void GambleSynthProcessor::applyLiveEdit (const Patch& p)
{
    {
        const juce::SpinLock::ScopedLockType lock (patchLock);
        patch = p;
    }
    liveEditDirty = true;
    notifyChanged();
}

void GambleSynthProcessor::saveFavourite()
{
    favourites.push_back (patch);
    favIndex = (int) favourites.size() - 1;
    notifyChanged();
}

void GambleSynthProcessor::loadNextFavourite()
{
    if (favourites.empty()) return;
    favIndex = (favIndex + 1) % (int) favourites.size();
    commit (favourites[(size_t) favIndex]);
}

// --- Mono / legato: one voice driven from a held-note stack, split at events ---
void GambleSynthProcessor::renderMono (juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midi, int numSamples)
{
    int last = 0;
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, numSamples, meta.samplePosition);
        if (pos > last) { monoVoice->renderNextBlock (buffer, last, pos - last); last = pos; }

        const auto m = meta.getMessage();
        if      (m.isNoteOn())  monoNoteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) monoNoteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            monoNotes.clear();
            monoVoice->releaseNote();
        }
    }
    if (numSamples > last)
        monoVoice->renderNextBlock (buffer, last, numSamples - last);
}

void GambleSynthProcessor::monoNoteOn (int note, float vel)
{
    const bool wasHeld = ! monoNotes.empty();

    monoNotes.erase (std::remove_if (monoNotes.begin(), monoNotes.end(),
                                     [note] (const auto& n) { return n.first == note; }),
                     monoNotes.end());
    monoNotes.emplace_back (note, vel);

    // mono (1) always retriggers the envelope; legato (2) only when nothing was held.
    const bool retrigger = (active.voiceMode == 1) || ! wasHeld;
    monoVoice->triggerNote (note, vel, retrigger);
}

void GambleSynthProcessor::monoNoteOff (int note)
{
    monoNotes.erase (std::remove_if (monoNotes.begin(), monoNotes.end(),
                                     [note] (const auto& n) { return n.first == note; }),
                     monoNotes.end());

    if (monoNotes.empty())
    {
        monoVoice->releaseNote();
    }
    else
    {
        const auto top = monoNotes.back();                 // fall back to newest still-held note
        const bool retrigger = (active.voiceMode == 1);
        monoVoice->triggerNote (top.first, top.second, retrigger);
    }
}

void GambleSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream os (destData, false);
    os.writeInt (2);                       // state format version
    os.writeInt (chaosMode ? 1 : 0);
    os.writeInt (lockMask);                // v2
    writePatch (os, patch);
    os.writeInt ((int) favourites.size());
    for (const auto& fp : favourites)
        writePatch (os, fp);
}

void GambleSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream is (data, (size_t) sizeInBytes, false);
    if (is.getTotalLength() < 8) return;

    const int stateVersion = is.readInt();
    chaosMode = is.readInt() != 0;
    lockMask = (stateVersion >= 2) ? is.readInt() : 0;
    const Patch current = readPatch (is);

    favourites.clear();
    const int n = is.readInt();
    for (int k = 0; k < n && ! is.isExhausted(); ++k)
        favourites.push_back (readPatch (is));
    favIndex = favourites.empty() ? -1 : (int) favourites.size() - 1;

    history.clear();
    histPos = -1;
    commit (current);
}

// At the bottom of the fade-out: take the new sound and wipe every tail, so
// nothing of the previous roll survives into the next one.
bool GambleSynthProcessor::swapToNewPatch()
{
    const juce::SpinLock::ScopedTryLockType lock (patchLock);
    if (! lock.isLocked())
        return false;                  // message thread mid-write; retry next block

    active = patch;

    for (auto* v : voicePtrs) v->hardReset();
    monoVoice->hardReset();
    monoNotes.clear();

    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    delayWrite = 0;
    chorus.clear();
    flanger.clear();
    decimator.reset();
    phaser.reset();
    crusher.reset();
    compressor.reset();
    dcBlocker.reset();
    reverb.reset();
    return true;
}

void GambleSynthProcessor::applyCut (float* L, float* R, int numSamples)
{
    const float step = 1.0f / juce::jmax (1.0f, (float) (currentSampleRate * 0.006));  // 6 ms

    for (int n = 0; n < numSamples; ++n)
    {
        if (cutState == Cut::FadeOut)
        {
            cutGain -= step;
            if (cutGain <= 0.0f)
            {
                cutGain = 0.0f;
                if (swapToNewPatch())
                    cutState = Cut::Silent;   // rest of this block stays muted;
            }                                 // the fade-in starts next block
        }
        else if (cutState == Cut::FadeIn)
        {
            cutGain += step;
            if (cutGain >= 1.0f) { cutGain = 1.0f; cutState = Cut::None; }
        }

        L[n] *= cutGain;
        R[n] *= cutGain;
    }
}

// Host tempo. Ableton and FL both report it through the VST3 process context;
// FL stops reporting while the transport is parked, so the last good value is
// cached and the standalone (no playhead at all) just keeps 120.
void GambleSynthProcessor::updateTempo()
{
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto bpm = pos->getBpm())
                if (*bpm > 20.0 && *bpm < 400.0)
                    hostBpm = *bpm;
}

// Derive the tempo-dependent values on the audio thread's copy, leaving the
// patch itself storing the *division* — so a shared seed keeps its rhythm
// wherever it lands, it just resolves against the local tempo.
void GambleSynthProcessor::applyTempoSync()
{
    const float delayBeats = syncDivBeats (active.delaySyncDiv);
    if (delayBeats > 0.0f)
        active.delayTime = juce::jlimit (0.02f, 0.95f, (float) (delayBeats * 60.0 / hostBpm));

    // Any modulator that asked for the grid gets it.
    for (auto& m : active.mod)
    {
        const float beats = syncDivBeats (m.syncDiv);
        if (beats > 0.0f)
            m.rate = juce::jlimit (0.02f, 30.0f, (float) (hostBpm / (60.0 * beats)));
    }
}

void GambleSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    buffer.clear();

    if (buffer.getNumChannels() == 0 || numSamples == 0)
        return;

    // Unprepared: emit silence rather than dividing by a zero sample rate deep in
    // the voices, which would spray NaN and get the track muted by some hosts.
    if (currentSampleRate <= 0.0 || osBuffer.getNumSamples() == 0)
        return;

    // A host may hand over a bigger block than it prepared for. Splitting keeps
    // every internal buffer within the size it was allocated at, without
    // allocating on the audio thread.
    if (numSamples > maxBlockSize)
    {
        int offset = 0;
        while (offset < numSamples)
        {
            const int chunk = juce::jmin (maxBlockSize, numSamples - offset);

            juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(),
                                            buffer.getNumChannels(), offset, chunk);
            juce::MidiBuffer sliceMidi;
            for (const auto meta : midi)
                if (meta.samplePosition >= offset && meta.samplePosition < offset + chunk)
                    sliceMidi.addEvent (meta.getMessage(), meta.samplePosition - offset);

            processBlock (slice, sliceMidi);
            offset += chunk;
        }
        return;
    }

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    updateTempo();
    applyTempoSync();

    // A dev-panel edit: take it straight away, no fade. Switching voice mode
    // mid-note would strand notes on the path we just left, so that one case
    // still clears both.
    if (liveEditDirty.exchange (false))
    {
        const juce::SpinLock::ScopedTryLockType lock (patchLock);
        if (lock.isLocked())
        {
            const int previousMode = active.voiceMode;
            active = patch;
            if (active.voiceMode != previousMode)
            {
                for (auto* v : voicePtrs) v->hardReset();
                monoVoice->hardReset();
                monoNotes.clear();
            }
        }
        else
        {
            liveEditDirty = true;              // message thread mid-write; next block
        }
    }

    // A new roll landed: duck the output, then swap at silence (see applyCut).
    if (patchDirty.exchange (false))
    {
        cutState = Cut::FadeOut;
    }
    else if (cutState == Cut::Silent)
    {
        cutState = Cut::FadeIn;      // this block renders the new sound from zero
    }

    // Everything downstream is written as a stereo pair. On a mono bus the right
    // channel goes to a scratch buffer and is folded back in at the end, rather
    // than being dropped (which would silence anything panned hard right).
    const bool monoOut = buffer.getNumChannels() < 2;
    auto* L = buffer.getWritePointer (0);
    auto* R = monoOut ? monoScratch.getWritePointer (0) : buffer.getWritePointer (1);

    if (monoOut)
        juce::FloatVectorOperations::clear (R, numSamples);

    if (oversample && numSamples * 2 <= osBuffer.getNumSamples())
    {
        const int osN = numSamples * 2;

        // MIDI positions are in base-rate samples; the voices are running twice
        // as fast, so events have to be placed twice as far in.
        osMidi.clear();
        for (const auto meta : midi)
            osMidi.addEvent (meta.getMessage(), meta.samplePosition * 2);

        osBuffer.clear (0, osN);
        if (active.voiceMode == 0)
            synth.renderNextBlock (osBuffer, osMidi, 0, osN);
        else
            renderMono (osBuffer, osMidi, osN);

        auto* osL = osBuffer.getWritePointer (0);
        auto* osR = osBuffer.getWritePointer (1);
        applyDriveFold (osL, osR, osN);
        decimator.process (osL, osR, L, R, numSamples);
    }
    else
    {
        if (active.voiceMode == 0)
            synth.renderNextBlock (buffer, midi, 0, numSamples);
        else
            renderMono (buffer, midi, numSamples);

        applyDriveFold (L, R, numSamples);
    }

    // --- Character: crush stays at the base rate on purpose — sample-rate
    // reduction *is* aliasing, that grit is the effect. Drive and fold already
    // ran upstream at 2x (see applyDriveFold). ---
    crusher.process (L, R, numSamples, active.crushBits, active.crushRate);
    dcBlocker.process (L, R, numSamples);

    // --- Modulation: phaser, flanger, chorus ---
    phaser.process (L, R, numSamples, active.phaserRate, active.phaserDepth,
                    active.phaserFb, active.phaserMix);
    flanger.process (L, R, numSamples, active.flangerRate, active.flangerDepth,
                     active.flangerFb, active.flangerMix);
    chorus.process (L, R, numSamples, 0.5f, 4.5f, 14.0f, active.chorusMix);

    // --- Stereo delay ---
    if (active.delayMix > 0.001f && ! delayL.empty())
    {
        const int size = (int) delayL.size();
        int dSamp = juce::jlimit (1, size - 1, (int) (active.delayTime * currentSampleRate));
        const float fb = juce::jlimit (0.0f, 0.95f, active.delayFb);
        const float mix = active.delayMix;
        for (int n = 0; n < numSamples; ++n)
        {
            int readPos = delayWrite - dSamp; if (readPos < 0) readPos += size;
            const float dl = delayL[(size_t) readPos];
            const float dr = delayR[(size_t) readPos];
            delayL[(size_t) delayWrite] = L[n] + dr * fb;   // ping-pong cross-feed
            delayR[(size_t) delayWrite] = R[n] + dl * fb;
            L[n] += dl * mix;
            R[n] += dr * mix;
            if (++delayWrite >= size) delayWrite = 0;
        }
    }

    // --- Reverb ---
    juce::Reverb::Parameters rp;
    rp.roomSize = active.reverbSize;
    rp.wetLevel = active.reverbMix;
    rp.dryLevel = 1.0f - active.reverbMix * 0.5f;
    rp.width    = 1.0f;
    reverb.setParameters (rp);
    reverb.processStereo (L, R, numSamples);

    // --- Dynamics: glue before the master limiter has to do the work ---
    compressor.process (L, R, numSamples, active.compAmount);

    // --- Master gain + soft limit ---
    const float g = active.master;
    for (int n = 0; n < numSamples; ++n)
    {
        L[n] = std::tanh (L[n] * g);
        R[n] = std::tanh (R[n] * g);
    }

    if (cutState != Cut::None)
        applyCut (L, R, numSamples);

    updateMeter (L, R, numSamples);

    if (monoOut)                          // fold the scratch right channel back in
        for (int n = 0; n < numSamples; ++n)
            L[n] = 0.5f * (L[n] + R[n]);
}

// Peak per block with a decay, so the meter falls smoothly instead of flickering.
// Drive + wavefolder. Both generate harmonics well past Nyquist, so they run in
// the oversampled section.
void GambleSynthProcessor::applyDriveFold (float* L, float* R, int numSamples)
{
    // tanh(x*d)/tanh(d) has a small-signal gain of d/tanh(d), so without the
    // make-up below drive doubles as a ~9x volume knob and slams the limiter.
    const float amount = juce::jmax (0.12f, active.drive);
    const float d = 1.0f + amount * 8.0f;
    const float norm = std::tanh (d);

    constexpr float floorD = 1.0f + 0.12f * 8.0f;             // the always-on floor
    const float floorGain  = floorD / std::tanh (floorD);
    const float makeup     = floorGain / (d / std::tanh (d)); // 1.0 at the floor

    for (int n = 0; n < numSamples; ++n)
    {
        L[n] = std::tanh (L[n] * d) / norm * makeup;
        R[n] = std::tanh (R[n] * d) / norm * makeup;
    }

    if (active.foldAmount > 0.001f)
        for (int n = 0; n < numSamples; ++n)
        {
            L[n] = waveFold (L[n], active.foldAmount);
            R[n] = waveFold (R[n], active.foldAmount);
        }
}

void GambleSynthProcessor::updateMeter (const float* L, const float* R, int numSamples)
{
    float peakL = 0.0f, peakR = 0.0f;
    for (int n = 0; n < numSamples; ++n)
    {
        peakL = juce::jmax (peakL, std::abs (L[n]));
        peakR = juce::jmax (peakR, std::abs (R[n]));
    }

    if (peakL > 0.99f || peakR > 0.99f)
        clipped.store (true, std::memory_order_relaxed);

    const float decay = std::pow (0.75f, (float) numSamples / 512.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float in = (ch == 0) ? peakL : peakR;
        const float held = meter[ch].load (std::memory_order_relaxed) * decay;
        meter[ch].store (juce::jmax (in, held), std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* GambleSynthProcessor::createEditor()
{
    return new GambleSynthEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GambleSynthProcessor();
}
