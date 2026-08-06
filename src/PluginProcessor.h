#pragma once
#include <JuceHeader.h>
#include "Patch.h"
#include "Randomizer.h"
#include "DSP.h"
#include "Fruit.h"
#include "Arp.h"
#include "Library.h"

class GambleVoice; // defined in SynthVoice.h; held by unique_ptr for the mono path

class GambleSynthProcessor : public juce::AudioProcessor
{
public:
    GambleSynthProcessor();
    ~GambleSynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GambleSynth"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- GambleSynth API ---
    void pullLever();                    // roll a new random sound (respects chaos)
    void rollSeed (unsigned seed);       // roll a specific seed
    void setChaos (bool shouldBeChaos) { chaosMode = shouldBeChaos; }
    bool isChaos() const { return chaosMode; }

    // Lockable reels — ROLL keeps the locked groups and re-rolls the rest.
    void setReelLocked (int reel, bool locked);
    bool isReelLocked (int reel) const { return (lockMask & (1 << reel)) != 0; }
    int  getLockMask() const { return lockMask; }
    bool anyReelLocked() const { return lockMask != 0; }

    // 2x oversampling of the voices and the nonlinear stages. On by default;
    // exposed so it can be measured against itself (and later offered as a CPU
    // option).
    void setOversampling (bool shouldOversample);
    bool isOversampling() const { return oversample; }

    // The reel lottery. Spun on a pull only — undo, redo and loading a
    // favourite change the sound, so the front glyphs follow, but they are not
    // pulls and must not hand out jackpots.
    const FruitSpin& getFruitSpin() const { return fruitSpin; }

    // Output level for the meter: 0..1 per channel, already peak-decayed.
    float getMeterLevel (int channel) const
    {
        return meter[channel == 0 ? 0 : 1].load (std::memory_order_relaxed);
    }
    bool isClipping() const { return clipped.load (std::memory_order_relaxed); }
    void clearClip() { clipped.store (false, std::memory_order_relaxed); }

    // History
    void undo();
    void redo();
    bool canUndo() const { return histPos > 0; }
    bool canRedo() const { return histPos >= 0 && histPos < (int) history.size() - 1; }

    // The saved-sound library, on disk rather than in plugin state so it
    // outlives the project and the session that made it.
    void saveFavourite();                        // adds the current sound
    void loadNextFavourite();                    // step through, for the plain UI
    void loadFavourite (int index);
    int  getNumFavourites() const { return library.size(); }
    Library& getLibrary() { return library; }

    const Patch& getPatch() const { return patch; }
    void setPatch (const Patch& p) { commit (p); }   // load an explicit sound (tests, tweaks)

    // Renders a short note through one voice and returns its level. A roll that
    // can't clear this is a dud (filter closed, envelope never opens, ...) and
    // free rolls simply draw again. Public so tests can calibrate it.
    float probeLevel (const Patch& p);

    // Dev panel (seed 777): apply an edited patch immediately, with no roll cut
    // and no history entry — dragging a slider must not mute or spam undo.
    void applyLiveEdit (const Patch& p);
    juce::MidiKeyboardState keyboardState;

    // Notifies the editor to refresh its labels/buttons after a state change.
    std::function<void()> onPatchChanged;

private:
    Patch rollAudible();                 // roll, redrawing if the sound is inaudible
    Patch withLocks (Patch fresh) const; // keep the locked reels from the current sound
    void commit (const Patch& p);        // set current patch + push onto history
    void setCurrent (const Patch& p);    // hand a sound to the audio thread (no history)
    void notifyChanged() { if (onPatchChanged) onPatchChanged(); }

    // Roll cut: fade the output out, drop every tail (voices, delay, chorus,
    // reverb), switch to the new sound, fade back in. Without it a roll bleeds
    // the previous sound into the new one — and switching mid-note clicks.
    enum class Cut { None, FadeOut, Silent, FadeIn };
    void applyCut (float* L, float* R, int numSamples);
    void updateMeter (const float* L, const float* R, int numSamples);
    void applyDriveFold (float* L, float* R, int numSamples);   // the nonlinear pair
    void updateTempo();                  // read host BPM (cached; 120 with no host)
    void applyTempoSync();               // resolve note divisions against that BPM
    bool swapToNewPatch();               // false = message thread busy, retry next block

    // Mono/legato rendering (poly uses the Synthesiser; mono uses a single voice)
    void renderMono (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numSamples);
    void monoNoteOn (int note, float vel);
    void monoNoteOff (int note);

    Patch patch;                     // canonical, owned by the message thread
    Patch active;                    // what the audio thread plays; swapped only at silence
    juce::SpinLock patchLock;        // guards the patch -> active handoff
    std::atomic<bool> patchDirty { false };
    std::atomic<bool> liveEditDirty { false };
    Cut   cutState = Cut::None;
    float cutGain  = 1.0f;

    bool chaosMode = false;
    int  lockMask = 0;
    FruitLottery fruitLottery;
    FruitSpin    fruitSpin;
    std::atomic<float> meter[2] { { 0.0f }, { 0.0f } };
    std::atomic<bool>  clipped { false };
    Randomizer randomizer;
    juce::Synthesiser synth;
    std::vector<GambleVoice*> voicePtrs;            // same voices, typed (no RT casts)

    std::unique_ptr<GambleVoice> monoVoice;
    std::unique_ptr<GambleVoice> probeVoice;        // offline audibility check (message thread)
    Patch probePatch;
    juce::AudioBuffer<float> probeBuffer;
    std::vector<std::pair<int, float>> monoNotes;   // held-note stack (note, velocity)

    std::vector<Patch> history;
    int histPos = -1;
    Library library;
    int favIndex = -1;

    // Oversampling: voices render at 2x, the nonlinear stages run there too, and
    // the decimator's low-pass removes what would otherwise fold back.
    bool oversample = true;
    int  maxBlockSize = 512;
    juce::AudioBuffer<float> osBuffer;
    juce::AudioBuffer<float> monoScratch;   // right-channel sink when a host gives 1 channel
    juce::MidiBuffer osMidi;
    Decimator2x decimator;
    Arpeggiator arp;

    // Master FX
    Chorus chorus;
    Phaser phaser;
    Flanger flanger;
    Crusher crusher;
    Compressor compressor;
    DCBlocker dcBlocker;
    juce::Reverb reverb;
    std::vector<float> delayL, delayR;
    int delayWrite = 0;
    double currentSampleRate = 44100.0;
    double hostBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GambleSynthProcessor)
};
