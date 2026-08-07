#pragma once
#include <JuceHeader.h>
#include "Patch.h"
#include "Randomizer.h"
#include "DSP.h"
#include "Fruit.h"
#include "Arp.h"
#include "Library.h"
#include "NudgeBank.h"

class GambleVoice; // defined in SynthVoice.h; held by unique_ptr for the mono path

// What the plugin publishes to the host. Deliberately few: the sound comes from
// rolls, not from a user turning knobs, so exposing every patch parameter would
// fight the concept and fill the automation list with things that get
// overwritten on the next pull. These are the controls a producer would
// actually reach for — and ROLL is here because automating it to fire on the
// bar is exactly the sort of thing this synth is for.
namespace ParamID
{
    inline constexpr const char* master   = "master";
    inline constexpr const char* chaos    = "chaos";
    inline constexpr const char* roll     = "roll";
    inline constexpr const char* seed     = "seed";
    inline constexpr const char* arpMode  = "arpMode";
    inline constexpr const char* arpDiv   = "arpDiv";
    inline constexpr const char* filter   = "filterTrim";
    inline constexpr const char* reverb   = "reverbTrim";
    inline constexpr const char* delayMix = "delayTrim";
}

class GambleSynthProcessor : public juce::AudioProcessor,
                             private juce::Timer
{
public:
    GambleSynthProcessor();
    ~GambleSynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { hostLog ("releaseResources"); }
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

    // Nudge: the same sound, played slightly differently. Costs a credit, which
    // is only ever won on the reels. Not a pull, so it never spins them.
    bool nudge();
    int  getNudgeCredits() const { return nudgeBank.balance(); }
    int  getLastPayout() const { return lastPayout; }
    NudgeBank& getNudgeBank() { return nudgeBank; }

    // ---- Live diagnostics, shown in dev mode. Everything here is written on
    // the audio thread and read on the message thread, so a silent plugin can
    // say *where* it went silent instead of being guessed at. ----
    struct Diagnostics
    {
        // Entered at the very top, before any guard. blocks counts the ones that
        // got all the way through — a host that calls processBlock and gets
        // turned away at a guard must not look identical to one that never
        // calls it at all.
        std::atomic<int>   entries    { 0 };
        std::atomic<int>   bailReason { 0 };   // 0 none, 1 no channels, 2 no samples, 3 unprepared
        std::atomic<int>   inChannels { 0 };   // what the host actually handed over
        std::atomic<int>   inSamples  { 0 };
        std::atomic<int>   blocks     { 0 };   // processBlock calls that ran fully
        std::atomic<int>   notesIn    { 0 };   // note-ons arriving
        std::atomic<int>   voicesOn   { 0 };   // voices sounding right now
        std::atomic<float> voicePeak  { 0.0f };// level straight out of the voices
        std::atomic<float> outPeak    { 0.0f };// level leaving processBlock
        std::atomic<int>   sampleRate { 0 };
        std::atomic<int>   blockSize  { 0 };
        std::atomic<int>   channels   { 0 };
        std::atomic<bool>  prepared   { false };
    };
    const Diagnostics& getDiagnostics() const { return diag; }

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

    juce::AudioProcessorValueTreeState apvts;

private:
    // Rolls asked for by host automation land here, on the message thread, where
    // allocating is allowed. Runs whether or not the editor is open — a plugin
    // in a session usually has its window closed.
    void timerCallback() override;

public:
    // Same work, callable directly. A console test has no message loop to fire
    // the timer, and pumping one is not portable.
    void serviceHostRequests();

private:

    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void readHostParameters();           // per block, on the audio thread

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
    NudgeBank    nudgeBank;
    int          lastPayout = 0;
    Diagnostics diag;
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

    // Host-facing parameter cache, read once per block.
    std::atomic<float>* pMaster   = nullptr;
    std::atomic<float>* pChaos    = nullptr;
    std::atomic<float>* pRoll     = nullptr;
    std::atomic<float>* pSeed     = nullptr;
    std::atomic<float>* pArpMode  = nullptr;
    std::atomic<float>* pArpDiv   = nullptr;
    std::atomic<float>* pFilter   = nullptr;
    std::atomic<float>* pReverb   = nullptr;
    std::atomic<float>* pDelayMix = nullptr;

    // Host call log. The dev panel can only ever show the instance whose editor
    // is open, and a host may have several — one scanned, one previewed, one
    // actually wired to the audio graph. This records what every instance was
    // asked to do, with thread ids, so the real sequence is visible.
    void hostLog (const juce::String& what);
    int  instanceId = 0;
    std::atomic<bool> loggedFirstProcess { false };

    // The host-request timer is started on the message thread, never in the
    // constructor: hosts build plugins on background scanning threads, and a
    // JUCE Timer needs the message thread to exist. This token lets the deferred
    // start know whether the processor is still alive.
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    // ROLL is a trigger, so what matters is the *edge*, not the level.
    bool  lastRollHigh = false;
    float lastSeenSeed = -1.0f;
    std::atomic<bool> rollRequested { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GambleSynthProcessor)
};
