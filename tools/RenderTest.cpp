// Headless render: instantiate the synth, play a chord, dump a WAV + print
// peak/RMS. Lets us verify sound and audition randomizer output with no DAW.
#include <map>
#include "../src/PluginProcessor.h"
#include "../src/DevPanel.h"
#include "../src/PluginEditor.h"
#include "../src/Fruit.h"
#include "../src/LeverDisplay.h"
#include "../src/Arp.h"
#include "../src/Wavetables.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double sr = 44100.0;
    const int    block = 512;
    const double seconds = 4.0;
    const int    numRolls = argc > 1 ? juce::jlimit (1, 16, atoi (argv[1])) : 4;

    GambleSynthProcessor proc;
    for (int a = 1; a < argc; ++a)
        if (juce::String (argv[a]) == "chaos") proc.setChaos (true);

    // A new patch ducks the output for ~12 ms (the roll cut) and drops anything
    // still sounding, so tests run a few empty blocks before they measure.
    auto settleAfterRoll = [&]
    {
        juce::AudioBuffer<float> tmp (2, block);
        for (int b = 0; b < 4; ++b)
        {
            tmp.clear();
            juce::MidiBuffer empty;
            proc.processBlock (tmp, empty);
        }
    };

    // --- Determinism self-check: same seed -> identical patch bytes ---
    {
        proc.rollSeed (4821);
        juce::MemoryBlock a; { juce::MemoryOutputStream os (a, false); writePatch (os, proc.getPatch()); }
        proc.rollSeed (999999); // roll something else in between
        proc.rollSeed (4821);
        juce::MemoryBlock b; { juce::MemoryOutputStream os (b, false); writePatch (os, proc.getPatch()); }
        std::cout << "seed reproducibility: " << (a == b ? "PASS" : "FAIL") << std::endl;
    }

    // --- State save/load round-trip: patch + favourites survive ---
    {
        proc.rollSeed (12345);
        proc.saveFavourite();
        proc.rollSeed (67890);
        proc.saveFavourite();
        juce::MemoryBlock cur; { juce::MemoryOutputStream os (cur, false); writePatch (os, proc.getPatch()); }

        juce::MemoryBlock state;
        proc.getStateInformation (state);

        GambleSynthProcessor loaded;
        loaded.setStateInformation (state.getData(), (int) state.getSize());
        juce::MemoryBlock cur2; { juce::MemoryOutputStream os (cur2, false); writePatch (os, loaded.getPatch()); }

        const bool ok = (cur == cur2) && (loaded.getNumFavourites() == 2);
        std::cout << "state round-trip: " << (ok ? "PASS" : "FAIL")
                  << " (favourites=" << loaded.getNumFavourites() << ")" << std::endl;
    }

    // --- Render the editor to a PNG so the layout can be inspected without a DAW ---
    bool uiShot = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "uishot") uiShot = true;
    if (uiShot)
    {
        auto shoot = [&] (bool dev, int w, int h, const juce::String& name)
        {
            std::unique_ptr<GambleSynthEditor> ed (
                dynamic_cast<GambleSynthEditor*> (proc.createEditor()));
            if (ed == nullptr) return;

            ed->setSize (w, h);
            if (dev) ed->setDevMode (true);
            ed->setSize (w, h);

            ed->settleReels();      // no message loop here, so land them by hand

            juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
            {
                juce::Graphics g (img);
                ed->paintEntireComponent (g, true);
            }

            juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (name));
            f.deleteFile();
            juce::PNGImageFormat png;
            if (auto out = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
                if (png.writeImageToStream (img, *out))
                    std::cout << "wrote " << f.getFullPathName() << std::endl;
        };

        shoot (false, 720, 1060, "ui_main.png");
        shoot (true,  1440, 1060, "ui_dev.png");
        return 0;
    }

    // --- Similarity: how many of N rolls are actually different sounds?
    //     Fingerprints each roll (log-band spectrum + envelope shape) and
    //     reports the near-duplicate pairs. Turns "feels samey" into a number.
    //     Usage: similaritytest [Bell|Pad|...]  (default: whatever comes up) ---
    bool simTest = false;
    juce::String wantArchetype;
    for (int a = 1; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        if (arg == "similaritytest") simTest = true;
        else if (arg.startsWithIgnoreCase ("arch=")) wantArchetype = arg.fromFirstOccurrenceOf ("=", false, false);
    }
    if (simTest)
    {
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);

        constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
        juce::dsp::FFT fft (fftOrder);
        constexpr int numBands = 20, numSegs = 6;

        const int total = (int) (sr * 1.5);
        juce::AudioBuffer<float> out (2, total), work (2, block);

        // Motion is now a major axis of variety, so the structural key has to
        // include it: shape+destination per slot, plus the envelope's routing.
        auto modKey = [] (const Patch& p)
        {
            juce::String k;
            for (const auto& m : p.mod)
                if (m.dest != ModNone && m.depth > 0.01f)
                    k << m.shape << ":" << m.dest << ",";
            k << "e" << p.envDest;
            return k;
        };

        struct Print { juce::String label; int seed; std::vector<float> v; };
        std::vector<Print> prints;
        juce::StringArray structures;

        const int wanted = 24;
        for (unsigned seed = 1; (int) prints.size() < wanted && seed < 4000; ++seed)
        {
            proc.rollSeed (seed);
            if (wantArchetype.isNotEmpty()
                && ! proc.getPatch().archetypeName.equalsIgnoreCase (wantArchetype))
                continue;

            const auto& pat = proc.getPatch();
            const juce::String label = pat.archetypeName
                                     + (pat.modifierName.isEmpty() ? juce::String() : "+" + pat.modifierName);

            for (int b = 0; b < 4; ++b) { work.clear(); juce::MidiBuffer e; proc.processBlock (work, e); }

            out.clear();
            for (int sPos = 0; sPos < total; sPos += block)
            {
                const int n = juce::jmin (block, total - sPos);
                work.clear();
                juce::MidiBuffer midi;
                if (sPos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
                proc.processBlock (sub, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, sPos, work, ch, 0, n);
            }

            // Spectrum just after the attack, in log-spaced bands.
            std::vector<float> fd ((size_t) fftSize * 2, 0.0f);
            const int from = (int) (sr * 0.15);
            for (int n = 0; n < fftSize; ++n)
            {
                const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) n / (float) (fftSize - 1));
                fd[(size_t) n] = out.getSample (0, from + n) * win;
            }
            fft.performFrequencyOnlyForwardTransform (fd.data());

            std::vector<float> v ((size_t) (numBands + numSegs), 0.0f);
            const double binHz = sr / fftSize;
            for (int b = 0; b < numBands; ++b)
            {
                const double lo = 100.0 * std::pow (12000.0 / 100.0, (double) b / numBands);
                const double hi = 100.0 * std::pow (12000.0 / 100.0, (double) (b + 1) / numBands);
                double e = 0.0;
                for (int k = (int) (lo / binHz); k < (int) (hi / binHz) && k < fftSize / 2; ++k)
                    e += (double) fd[(size_t) k] * fd[(size_t) k];
                v[(size_t) b] = (float) std::sqrt (e);
            }
            // Envelope shape matters as much as spectrum for "is this the same sound".
            for (int sgn = 0; sgn < numSegs; ++sgn)
                v[(size_t) (numBands + sgn)] =
                    out.getRMSLevel (0, sgn * total / numSegs, total / numSegs) * 2.0f;

            float norm = 0.0f;
            for (float x : v) norm += x * x;
            norm = std::sqrt (juce::jmax (1.0e-9f, norm));
            for (auto& x : v) x /= norm;

            // What the ear actually categorises by: the discrete structure, not
            // the continuous trim. Two rolls sharing this key are "that sound
            // again" however different their decay or cutoff happen to be.
            const float ratioSemis = (float) pat.osc[1].semi + pat.osc[1].fine * 0.01f;
            structures.addIfNotAlreadyThere (
                  juce::String (pat.oscMode)
                + "/" + juce::String (pat.osc[0].wave) + juce::String (pat.osc[1].wave)
                      + juce::String (pat.osc[2].wave)
                + "/r" + juce::String (juce::roundToInt (ratioSemis * 2.0f) / 2.0f, 1)
                + "/f" + juce::String (pat.filterModel)
                + "/" + pat.modifierName
                + "/m" + modKey (pat));

            prints.push_back ({ label, pat.seed, std::move (v) });
        }

        auto distance = [] (const Print& a, const Print& b)
        {
            float dot = 0.0f;
            for (size_t k = 0; k < a.v.size(); ++k) dot += a.v[k] * b.v[k];
            return 1.0f - juce::jlimit (0.0f, 1.0f, dot);       // cosine distance
        };

        const int distinctStructures = structures.size();
        double sum = 0.0; int pairs = 0, nearDupes = 0;
        float closest = 1.0f; juce::String closestPair;
        for (size_t i = 0; i < prints.size(); ++i)
            for (size_t j = i + 1; j < prints.size(); ++j)
            {
                const float d = distance (prints[i], prints[j]);
                sum += d; ++pairs;
                if (d < 0.02f) ++nearDupes;
                if (d < closest)
                {
                    closest = d;
                    closestPair = prints[i].label + " #" + juce::String (prints[i].seed)
                                + "  vs  " + prints[j].label + " #" + juce::String (prints[j].seed);
                }
            }

        std::cout << (wantArchetype.isEmpty() ? juce::String ("all archetypes") : wantArchetype)
                  << ": " << prints.size() << " rolls, " << pairs << " pairs" << std::endl;
        std::cout << "  distinct structures  " << distinctStructures << "/" << prints.size()
                  << "   (the discrete choices the ear latches onto)" << std::endl;
        std::cout << "  mean distance  " << juce::String (sum / juce::jmax (1, pairs), 4) << std::endl;
        std::cout << "  near-duplicates (<0.02)  " << nearDupes
                  << "   (" << juce::String (100.0 * nearDupes / juce::jmax (1, pairs), 1) << "% of pairs)" << std::endl;
        std::cout << "  most alike: " << closestPair
                  << "  d=" << juce::String (closest, 4) << std::endl;
        return 0;
    }

    // --- Host lifecycle: a plugin host does things the standalone never does —
    //     negotiates bus layouts, restores state before prepareToPlay, and can
    //     hand over a block bigger than the one it prepared. Each is checked
    //     separately so a failure names its own cause. ---
    bool hostTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "hosttest") hostTest = true;
    if (hostTest)
    {
        bool allOk = true;

        // Play a chord and report the peak that leaves the processor.
        auto runNotes = [&] (GambleSynthProcessor& pr, int blockSize, int blocks)
        {
            juce::AudioBuffer<float> work (2, blockSize);

            // Let any pending roll cut finish first. A note struck during the
            // fade-out briefly renders the *previous* patch, which would make
            // this measure the sound the processor happened to start on.
            for (int b = 0; b < 4; ++b)
            {
                work.clear();
                juce::MidiBuffer empty;
                pr.processBlock (work, empty);
            }

            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                work.clear();
                juce::MidiBuffer midi;
                if (b == 0)
                    for (int note : { 48, 55, 64 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
                pr.processBlock (work, midi);
                peak = juce::jmax (peak, work.getMagnitude (0, blockSize));
            }
            return peak;
        };

        // 1. Bus layout negotiation — a host asks before it commits.
        {
            GambleSynthProcessor pr;
            using AC = juce::AudioChannelSet;
            struct L { const char* name; AC set; bool mustAccept; };
            const L layouts[] = {
                { "stereo out", AC::stereo(), true },
                { "mono out",   AC::mono(),   true },
            };
            for (const auto& l : layouts)
            {
                juce::AudioProcessor::BusesLayout bl;
                bl.outputBuses.add (l.set);
                const bool accepted = pr.checkBusesLayoutSupported (bl);
                std::cout << "bus " << juce::String (l.name).paddedRight (' ', 12)
                          << (accepted ? "accepted" : "rejected");
                if (l.mustAccept && ! accepted) { allOk = false; std::cout << "   <-- MUST accept"; }
                std::cout << std::endl;
            }
        }

        // 2. State restored BEFORE prepareToPlay — the usual order when a host
        //    reopens a project.
        {
            GambleSynthProcessor donor;
            donor.rollSeed (4821);
            donor.saveFavourite();
            juce::MemoryBlock state;
            donor.getStateInformation (state);

            // Restore before preparing (how a host reopens a project)...
            GambleSynthProcessor early;
            early.setStateInformation (state.getData(), (int) state.getSize());
            early.setPlayConfigDetails (0, 2, sr, block);
            early.prepareToPlay (sr, block);
            const float peakEarly = runNotes (early, block, 60);

            // ...and after, which is the ordering that definitely works. What
            // matters is that the two agree; asserting an absolute level here
            // would just be asserting that one particular seed is loud.
            GambleSynthProcessor late;
            late.setPlayConfigDetails (0, 2, sr, block);
            late.prepareToPlay (sr, block);
            late.setStateInformation (state.getData(), (int) state.getSize());
            const float peakLate = runNotes (late, block, 60);

            const bool ok = std::abs (peakEarly - peakLate) < 0.02f;
            allOk = allOk && ok;
            std::cout << "state before prepare: " << (ok ? "PASS" : "FAIL")
                      << "  early=" << juce::String (peakEarly, 4)
                      << "  late=" << juce::String (peakLate, 4) << std::endl;
        }

        // 3. A block larger than the one prepared for. Hosts do this when the
        //    buffer size changes, and the oversampling path has a size guard.
        {
            GambleSynthProcessor pr;
            pr.setPlayConfigDetails (0, 2, sr, block);
            pr.prepareToPlay (sr, block);

            const float peak = runNotes (pr, block * 4, 20);
            const bool ok = peak > 0.01f;
            allOk = allOk && ok;
            std::cout << "oversized block:      " << (ok ? "PASS" : "FAIL")
                      << "  peak=" << juce::String (peak, 4) << std::endl;
        }

        // 4. A roll while the transport is idle, then notes — the roll cut has
        //    to complete on its own or the output stays ducked forever.
        {
            GambleSynthProcessor pr;
            pr.setPlayConfigDetails (0, 2, sr, block);
            pr.prepareToPlay (sr, block);
            pr.pullLever();

            const float peak = runNotes (pr, block, 60);
            const bool ok = peak > 0.01f;
            allOk = allOk && ok;
            std::cout << "roll then play:       " << (ok ? "PASS" : "FAIL")
                      << "  peak=" << juce::String (peak, 4) << std::endl;
        }

        // 5. Sample rates a host might pick.
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            GambleSynthProcessor pr;
            pr.setPlayConfigDetails (0, 2, rate, block);
            pr.prepareToPlay (rate, block);

            const float peak = runNotes (pr, block, 60);
            const bool ok = peak > 0.01f;
            allOk = allOk && ok;
            std::cout << "rate " << (int) rate << ":            " << (ok ? "PASS" : "FAIL")
                      << "  peak=" << juce::String (peak, 4) << std::endl;
        }

        // 6. Rendering into a mono buffer — the layout we now accept.
        {
            GambleSynthProcessor pr;
            pr.setPlayConfigDetails (0, 1, sr, block);
            pr.prepareToPlay (sr, block);

            juce::AudioBuffer<float> work (1, block);
            float peak = 0.0f;
            for (int b = 0; b < 60; ++b)
            {
                work.clear();
                juce::MidiBuffer midi;
                if (b == 0)
                    for (int note : { 48, 55, 64 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
                pr.processBlock (work, midi);
                peak = juce::jmax (peak, work.getMagnitude (0, block));
            }
            const bool ok = peak > 0.01f;
            allOk = allOk && ok;
            std::cout << "mono buffer:          " << (ok ? "PASS" : "FAIL")
                      << "  peak=" << juce::String (peak, 4) << std::endl;
        }

        std::cout << "host lifecycle: " << (allOk ? "PASS" : "FAIL") << std::endl;
        return allOk ? 0 : 1;
    }

    // --- Fruit lottery: the odds are constructed, so they should land on the
    //     numbers exactly rather than approximately. ---
    bool fruitTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "fruittest") fruitTest = true;
    if (fruitTest)
    {
        const int spins = 200000;
        FruitLottery lottery;
        int matches[4] = {};
        int perReel[3][(int) Fruit::NumFruits] = {};

        for (int k = 0; k < spins; ++k)
        {
            const auto s2 = lottery.spin();
            matches[s2.matches()]++;
            for (int r = 0; r < 3; ++r) perReel[r][(int) s2.symbol[r]]++;
        }

        const double jack = 100.0 * matches[3] / spins;
        const double near = 100.0 * matches[2] / spins;

        std::cout << "over " << spins << " spins\n" << std::endl;
        std::cout << "jackpot    " << juce::String (jack, 2) << "%   (target "
                  << juce::String (100.0 * FruitLottery::JackpotChance, 2) << "%)" << std::endl;
        std::cout << "near miss  " << juce::String (near, 2) << "%   (target "
                  << juce::String (100.0 * FruitLottery::NearMissChance, 2) << "%)" << std::endl;
        std::cout << "nothing    " << juce::String (100.0 * matches[1] / spins, 2) << "%" << std::endl;

        // Every symbol has to turn up about equally on every reel, or one of
        // them is barely worth drawing.
        std::cout << "\n            TONE   SHAPE  SPACE" << std::endl;
        bool balanced = true;
        for (int fr = 0; fr < (int) Fruit::NumFruits; ++fr)
        {
            std::cout << juce::String (fruitName ((Fruit) fr)).paddedRight (' ', 11);
            for (int r = 0; r < 3; ++r)
            {
                const double pct = 100.0 * perReel[r][fr] / spins;
                if (std::abs (pct - 100.0 / (int) Fruit::NumFruits) > 1.5) balanced = false;
                std::cout << juce::String (pct, 1).paddedLeft (' ', 6) << "%";
            }
            std::cout << std::endl;
        }

        const bool ok = std::abs (jack - 100.0 * FruitLottery::JackpotChance) < 0.15
                     && std::abs (near - 100.0 * FruitLottery::NearMissChance) < 0.6
                     && balanced;
        std::cout << "\nfruit lottery: " << (ok ? "PASS" : "FAIL") << std::endl;
        return ok ? 0 : 1;
    }

    // --- Arpeggiator: it re-times held notes, so it must never invent one, never
    //     leave one hanging, and must step at the rate it was asked for. ---
    bool arpTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "arptest") arpTest = true;
    if (arpTest)
    {
        bool allOk = true;
        const double stepSamples = 0.125 * sr;          // 1/8 at 120 BPM

        // Hold a C minor triad and run four seconds through the arp.
        auto run = [&] (int mode, float gate, int octaves, juce::Array<int>& notesOut, int& maxOverlap)
        {
            Arpeggiator arp;
            arp.reset();
            int sounding = 0;
            maxOverlap = 0;

            for (int b = 0; b < 350; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    for (int n : { 48, 51, 55 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);

                arp.process (midi, block, stepSamples, mode, gate, octaves);

                for (const auto meta : midi)
                {
                    const auto m = meta.getMessage();
                    if (m.isNoteOn())  { notesOut.add (m.getNoteNumber()); ++sounding; }
                    if (m.isNoteOff()) --sounding;
                    maxOverlap = juce::jmax (maxOverlap, sounding);
                }
            }
            return sounding;
        };

        // 1. Only notes that were held (plus octave copies) ever come out.
        {
            juce::Array<int> notes; int overlap = 0;
            run (Arpeggiator::Up, 0.6f, 1, notes, overlap);

            bool onlyHeld = true;
            for (int n : notes) if (n != 48 && n != 51 && n != 55) onlyHeld = false;

            const bool ok = onlyHeld && notes.size() > 20;
            allOk = allOk && ok;
            std::cout << "plays only held notes: " << (ok ? "PASS" : "FAIL")
                      << "  (" << notes.size() << " steps)" << std::endl;
        }

        // 2. Never two arp notes at once — that would stack voices forever.
        {
            juce::Array<int> notes; int overlap = 0;
            run (Arpeggiator::Up, 0.95f, 3, notes, overlap);
            const bool ok = overlap <= 1;
            allOk = allOk && ok;
            std::cout << "never overlaps:        " << (ok ? "PASS" : "FAIL")
                      << "  (max " << overlap << " sounding)" << std::endl;
        }

        // 3. Steps land at the requested rate.
        {
            Arpeggiator arp; arp.reset();
            juce::Array<int> onsets;
            int elapsed = 0;
            for (int b = 0; b < 350; ++b)      // enough blocks to see ~30 steps
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    for (int n : { 48, 51, 55 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
                arp.process (midi, block, stepSamples, Arpeggiator::Up, 0.6f, 1);
                for (const auto meta : midi)
                    if (meta.getMessage().isNoteOn())
                        onsets.add (elapsed + meta.samplePosition);
                elapsed += block;
            }

            double worst = 0.0;
            for (int k = 1; k < onsets.size(); ++k)
                worst = juce::jmax (worst, std::abs ((onsets[k] - onsets[k-1]) - stepSamples));

            const bool ok = onsets.size() > 20 && worst <= 2.0;
            allOk = allOk && ok;
            std::cout << "step timing:           " << (ok ? "PASS" : "FAIL")
                      << "  worst drift " << juce::String (worst, 1) << " samples"
                      << " of " << (int) stepSamples << std::endl;
        }

        // 4. Octave span actually reaches higher notes.
        {
            juce::Array<int> notes; int overlap = 0;
            run (Arpeggiator::Up, 0.6f, 3, notes, overlap);
            const bool reachedUp = notes.contains (48 + 24);
            allOk = allOk && reachedUp;
            std::cout << "octave span:           " << (reachedUp ? "PASS" : "FAIL") << std::endl;
        }

        // 5. Releasing the chord stops it, leaving nothing hanging.
        {
            Arpeggiator arp; arp.reset();
            int sounding = 0;
            for (int b = 0; b < 120; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    for (int n : { 48, 51, 55 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
                if (b == 60)
                    for (int n : { 48, 51, 55 })
                        midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);

                arp.process (midi, block, stepSamples, Arpeggiator::Up, 0.6f, 1);
                for (const auto meta : midi)
                {
                    if (meta.getMessage().isNoteOn())  ++sounding;
                    if (meta.getMessage().isNoteOff()) --sounding;
                }
            }
            const bool ok = (sounding == 0);
            allOk = allOk && ok;
            std::cout << "stops on release:      " << (ok ? "PASS" : "FAIL")
                      << "  (" << sounding << " left sounding)" << std::endl;
        }

        std::cout << "arpeggiator: " << (allOk ? "PASS" : "FAIL") << std::endl;
        return allOk ? 0 : 1;
    }

    // --- Wavetables. The whole gamble is the mipmapping: a naive wavetable
    //     aliases badly on high notes and would sound worse than the plain
    //     waves it replaces. Measured against the polyBLEP saw, which is
    //     properly band-limited, as the standard to beat. ---
    bool wtTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "wttest") wtTest = true;
    if (wtTest)
    {
        constexpr int fftOrder = 14, fftSize = 1 << fftOrder;
        juce::dsp::FFT fft (fftOrder);

        auto inharmonic = [&] (const juce::AudioBuffer<float>& buf, int from, double f0)
        {
            std::vector<float> fd ((size_t) fftSize * 2, 0.0f);
            for (int n = 0; n < fftSize; ++n)
            {
                const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) n / (float) (fftSize - 1));
                fd[(size_t) n] = buf.getSample (0, from + n) * win;
            }
            fft.performFrequencyOnlyForwardTransform (fd.data());

            const double binHz = sr / fftSize;
            std::vector<bool> harmonic ((size_t) fftSize / 2, false);
            for (int k = 1; k * f0 < sr * 0.5; ++k)
            {
                const int c = (int) std::round (k * f0 / binHz);
                for (int b = c - 3; b <= c + 3; ++b)
                    if (b >= 0 && b < fftSize / 2) harmonic[(size_t) b] = true;
            }

            double total = 0.0, harm = 0.0;
            for (int b = (int) (150.0 / binHz); b < fftSize / 2; ++b)
            {
                const double e = (double) fd[(size_t) b] * fd[(size_t) b];
                total += e;
                if (harmonic[(size_t) b]) harm += e;
            }
            return total > 0.0 ? (total - harm) / total : 0.0;
        };

        auto plain = []
        {
            Patch p;
            p.osc[0] = { 2, 0, 0.0f, 0.8f, 0.0f };
            p.osc[1] = { 2, 0, 0.0f, 0.0f, 0.0f };
            p.osc[2] = { 2, 0, 0.0f, 0.0f, 0.0f };
            p.oscMode = 0; p.unisonVoices = 1; p.subLevel = 0.0f; p.noiseLevel = 0.0f;
            p.filterModel = 0; p.filterType = 0; p.cutoff = 18000.0f;
            p.resonance = 0.0f; p.filterEnvAmt = 0.0f; p.keytrack = 0.0f;
            p.ampA = 0.005f; p.ampD = 0.05f; p.ampS = 1.0f; p.ampR = 0.1f;
            p.mod[0].dest = ModNone; p.envDest = ModNone;
            p.velToAmp = 0.0f; p.velToFilter = 0.0f; p.stereoWidth = 0.0f;
            p.chorusMix = 0.0f; p.drive = 0.0f; p.delayMix = 0.0f; p.reverbMix = 0.0f;
            p.foldAmount = 0.0f; p.crushBits = 16.0f; p.crushRate = 1.0f;
            p.phaserMix = 0.0f; p.flangerMix = 0.0f; p.compAmount = 0.0f;
            p.arpMode = 0; p.master = 0.8f;
            return p;
        };

        auto render = [&] (const Patch& p, int note, juce::AudioBuffer<float>& out)
        {
            proc.setPatch (p);
            juce::AudioBuffer<float> work (2, block);
            for (int b = 0; b < 4; ++b)
            { work.clear(); juce::MidiBuffer e; proc.processBlock (work, e); }

            const int total = out.getNumSamples();
            out.clear();
            for (int spos = 0; spos < total; spos += block)
            {
                const int n = juce::jmin (block, total - spos);
                work.clear();
                juce::MidiBuffer midi;
                if (spos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
                juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
                proc.processBlock (sub, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, spos, work, ch, 0, n);
            }
        };

        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);

        const int total = (int) (sr * 1.0);
        juce::AudioBuffer<float> out (2, total);
        bool allOk = true;

        // The saw is the reference: properly band-limited, so whatever it
        // measures at is the noise floor of the measurement itself.
        double sawAlias = 0.0;
        {
            Patch p = plain();
            render (p, 96, out);              // C7, where aliasing shows worst
            sawAlias = inharmonic (out, (int) (sr * 0.3), 
                                   juce::MidiMessage::getMidiNoteInHertz (96));
            std::cout << "polyBLEP saw @C7 inharmonic: "
                      << juce::String (sawAlias * 100.0, 2) << "%  (the reference)" << std::endl;
        }

        for (int t = 0; t < Wavetables::NumTables; ++t)
        {
            Patch p = plain();
            p.osc[0].wave = 5; p.wtTable = t; p.wtPos = 0.5f;

            render (p, 96, out);
            const double alias = inharmonic (out, (int) (sr * 0.3),
                                             juce::MidiMessage::getMidiNoteInHertz (96));
            const float peak = out.getMagnitude (0, total);

            bool finite = true;
            for (int n = 0; n < total && finite; ++n)
                if (! std::isfinite (out.getSample (0, n))) finite = false;

            // Must be in the same league as the band-limited saw, not merely
            // "not terrible" — a naive table would be several times worse.
            const bool ok = finite && peak > 0.02f && peak <= 1.01f
                         && alias < juce::jmax (0.02, sawAlias * 3.0);
            allOk = allOk && ok;

            std::cout << juce::String (Wavetables::name (t)).paddedRight (' ', 9)
                      << (ok ? "PASS" : "FAIL")
                      << "  alias " << juce::String (alias * 100.0, 2) << "%"
                      << "  peak " << juce::String (peak, 3)
                      << (finite ? "" : "  NON-FINITE") << std::endl;
        }

        // Position must actually change the sound, or none of this earned its
        // place. Compare the spectrum at both ends of the morph.
        {
            juce::AudioBuffer<float> a (2, total), b (2, total);
            Patch p = plain();
            p.osc[0].wave = 5; p.wtTable = 0;

            p.wtPos = 0.0f; render (p, 60, a);
            p.wtPos = 1.0f; render (p, 60, b);

            double diff = 0.0;
            for (int n = 0; n < total; ++n)
                diff += std::abs (a.getSample (0, n) - b.getSample (0, n));
            diff /= total;

            const bool ok = diff > 0.02;
            allOk = allOk && ok;
            std::cout << "position changes tone: " << (ok ? "PASS" : "FAIL")
                      << "  mean diff " << juce::String (diff, 4) << std::endl;
        }

        // A stepped modulator on position must not click: the voice slews it.
        {
            Patch p = plain();
            p.osc[0].wave = 5; p.wtTable = 0; p.wtPos = 0.5f;
            p.ampS = 1.0f;
            p.mod[0] = { ModSampleHold, ModWtPos, 12.0f, 1.0f, 0.0f, 0 };
            render (p, 60, out);

            // Same patch with the modulator off: whatever this counts is the
            // waveform's own steepness, not the modulation.
            Patch still = p;
            still.mod[0].dest = ModNone;
            juce::AudioBuffer<float> ref (2, total);
            render (still, 60, ref);

            auto countClicks = [&] (const juce::AudioBuffer<float>& buf)
            {
                const float* dd = buf.getReadPointer (0);
                std::vector<float> ddv ((size_t) total, 0.0f);
                for (int n = 1; n < total; ++n) ddv[(size_t) n] = std::abs (dd[n] - dd[n-1]);
                int c = 0;
                for (int n = (int) (sr * 0.2); n < total - 4; ++n)
                {
                    float around = 0.0f;
                    for (int k = 1; k <= 3; ++k)
                        around = juce::jmax (around, ddv[(size_t)(n-k)], ddv[(size_t)(n+k)]);
                    if (ddv[(size_t) n] > 0.05f && ddv[(size_t) n] > around * 4.0f) ++c;
                }
                return c;
            };
            const int baseline = countClicks (ref);

            const float* d = out.getReadPointer (0);
            std::vector<float> dv ((size_t) total, 0.0f);
            for (int n = 1; n < total; ++n) dv[(size_t) n] = std::abs (d[n] - d[n-1]);

            int clicks = 0;
            for (int n = (int) (sr * 0.2); n < total - 4; ++n)
            {
                float around = 0.0f;
                for (int k = 1; k <= 3; ++k)
                    around = juce::jmax (around, dv[(size_t)(n-k)], dv[(size_t)(n+k)]);
                if (dv[(size_t) n] > 0.05f && dv[(size_t) n] > around * 4.0f) ++clicks;
            }

            // The modulation must not add discontinuities of its own.
            const bool ok = (clicks <= baseline);
            allOk = allOk && ok;
            std::cout << "stepped mod, no clicks: " << (ok ? "PASS" : "FAIL")
                      << "  (" << clicks << " vs " << baseline
                      << " with the modulator off)" << std::endl;
        }

        std::cout << "wavetables: " << (allOk ? "PASS" : "FAIL") << std::endl;
        return allOk ? 0 : 1;
    }

    // --- Worst-case CPU: 16 held notes, 7-voice unison each, whole FX rack on,
    //     measured with and without oversampling. ---
    bool perfTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "perftest") perfTest = true;
    if (perfTest)
    {
        for (int os = 0; os < 2; ++os)
        {
            proc.setOversampling (os != 0);
            proc.setPlayConfigDetails (0, 2, sr, block);
            proc.prepareToPlay (sr, block);

            Patch p;
            p.osc[0] = { 2, 0, 4.0f, 0.7f };
            p.osc[1] = { 2, 0, -6.0f, 0.6f };
            p.osc[2] = { 3, -12, 2.0f, 0.5f };
            p.unisonVoices = 7; p.unisonDetune = 20.0f;
            p.subLevel = 0.3f; p.noiseLevel = 0.05f;
            p.filterModel = 1; p.filterPoles = 4; p.cutoff = 3000.0f; p.resonance = 0.5f;
            p.ampA = 0.05f; p.ampS = 1.0f; p.ampR = 1.0f;
            p.drive = 0.5f; p.foldAmount = 0.4f; p.crushBits = 8.0f; p.crushRate = 3.0f;
            p.phaserMix = 0.5f; p.flangerMix = 0.5f; p.chorusMix = 0.5f;
            p.delayMix = 0.4f; p.reverbMix = 0.4f; p.compAmount = 0.6f;
            p.master = 0.8f;
            proc.setPatch (p);

            juce::AudioBuffer<float> work (2, block);
            for (int b = 0; b < 4; ++b) { work.clear(); juce::MidiBuffer e; proc.processBlock (work, e); }

            const int total = (int) (sr * 5.0);
            const double start = juce::Time::getMillisecondCounterHiRes();
            for (int s = 0; s < total; s += block)
            {
                work.clear();
                juce::MidiBuffer midi;
                if (s == 0)
                    for (int k = 0; k < 16; ++k)      // 16 voices, all sustained
                        midi.addEvent (juce::MidiMessage::noteOn (1, 40 + k * 3, 0.8f), 0);
                proc.processBlock (work, midi);
            }
            const double elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;

            std::cout << (os ? "2x oversampled: " : "no oversampling: ")
                      << juce::String (5.0 / elapsed, 1) << "x realtime"
                      << "   (" << juce::String (elapsed / 5.0 * 100.0, 1) << "% of one core)"
                      << std::endl;
        }
        proc.setOversampling (true);
        return 0;
    }

    // --- Aliasing: hard sync and the wavefolder make harmonics far above
    //     Nyquist. Those fold back to frequencies that aren't multiples of the
    //     note, so measuring inharmonic energy measures aliasing directly. ---
    bool aliasTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "aliastest") aliasTest = true;
    if (aliasTest)
    {
        constexpr int fftOrder = 14, fftSize = 1 << fftOrder;   // 16384 -> 2.7 Hz bins
        juce::dsp::FFT fft (fftOrder);

        // Fraction of the spectrum that is NOT sitting on a harmonic of f0.
        auto inharmonicFraction = [&] (const juce::AudioBuffer<float>& buf, int from, double f0)
        {
            std::vector<float> fd ((size_t) fftSize * 2, 0.0f);
            for (int n = 0; n < fftSize; ++n)
            {
                const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) n / (float) (fftSize - 1));
                fd[(size_t) n] = buf.getSample (0, from + n) * win;
            }
            fft.performFrequencyOnlyForwardTransform (fd.data());

            const double binHz = sr / fftSize;
            const int lowBin = (int) (150.0 / binHz);           // ignore DC / rumble
            double total = 0.0, harmonic = 0.0;

            std::vector<bool> isHarmonic ((size_t) fftSize / 2, false);
            for (int k = 1; k * f0 < sr * 0.5; ++k)
            {
                const int centre = (int) std::round (k * f0 / binHz);
                for (int b = centre - 3; b <= centre + 3; ++b)
                    if (b >= 0 && b < fftSize / 2) isHarmonic[(size_t) b] = true;
            }

            for (int b = lowBin; b < fftSize / 2; ++b)
            {
                const double e = (double) fd[(size_t) b] * fd[(size_t) b];
                total += e;
                if (isHarmonic[(size_t) b]) harmonic += e;
            }
            return total > 0.0 ? (total - harmonic) / total : 0.0;
        };

        struct Case { const char* name; std::function<void (Patch&)> tweak; int note; };
        const Case cases[] = {
            { "hard sync",  [] (Patch& p) { p.oscMode = 2; p.fmAmount = 0.6f;
                                            p.osc[1].level = 0.8f; }, 84 },
            { "FM index",   [] (Patch& p) { p.oscMode = 3; p.fmAmount = 0.8f;
                                            p.osc[1].semi = 12; }, 84 },
            { "wavefolder", [] (Patch& p) { p.foldAmount = 0.8f; }, 79 },
        };

        bool allOk = true;
        for (const auto& cfg : cases)
        {
            double result[2] = { 0.0, 0.0 };
            for (int os = 0; os < 2; ++os)
            {
                proc.setOversampling (os != 0);
                proc.setPlayConfigDetails (0, 2, sr, block);
                proc.prepareToPlay (sr, block);

                Patch p;
                p.osc[0] = { 2, 0, 0.0f, 0.8f };
                p.osc[1] = { 2, 0, 0.0f, 0.0f };
                p.osc[2] = { 2, 0, 0.0f, 0.0f };
                p.oscMode = 0; p.unisonVoices = 1; p.subLevel = 0.0f; p.noiseLevel = 0.0f;
                p.filterModel = 0; p.filterType = 0; p.cutoff = 18000.0f; p.resonance = 0.0f;
                p.filterEnvAmt = 0.0f; p.keytrack = 0.0f; p.mod[0].dest = ModNone;
                p.ampA = 0.005f; p.ampD = 0.05f; p.ampS = 1.0f; p.ampR = 0.1f;
                p.velToAmp = 0.0f; p.velToFilter = 0.0f; p.stereoWidth = 0.0f;
                p.chorusMix = 0.0f; p.drive = 0.0f; p.delayMix = 0.0f; p.reverbMix = 0.0f;
                p.foldAmount = 0.0f; p.crushBits = 16.0f; p.crushRate = 1.0f;
                p.phaserMix = 0.0f; p.flangerMix = 0.0f; p.compAmount = 0.0f;
                p.master = 0.8f;
                cfg.tweak (p);
                proc.setPatch (p);

                juce::AudioBuffer<float> work (2, block);
                for (int b = 0; b < 4; ++b)
                {
                    work.clear(); juce::MidiBuffer empty; proc.processBlock (work, empty);
                }

                const int total = (int) (sr * 1.0);
                juce::AudioBuffer<float> out (2, total);
                out.clear();
                for (int s = 0; s < total; s += block)
                {
                    const int n = juce::jmin (block, total - s);
                    work.clear();
                    juce::MidiBuffer midi;
                    if (s == 0) midi.addEvent (juce::MidiMessage::noteOn (1, cfg.note, 0.9f), 0);
                    juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
                    proc.processBlock (sub, midi);
                    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, s, work, ch, 0, n);
                }

                const double f0 = juce::MidiMessage::getMidiNoteInHertz (cfg.note);
                result[os] = inharmonicFraction (out, (int) (sr * 0.3), f0);
            }

            const double improvement = result[1] > 0.0 ? result[0] / result[1] : 0.0;
            const bool ok = result[1] < result[0];
            allOk = allOk && ok;
            std::cout << juce::String (cfg.name).paddedRight (' ', 12)
                      << (ok ? "PASS" : "FAIL")
                      << "  inharmonic energy: 1x " << juce::String (result[0] * 100.0, 2) << "%"
                      << "  ->  2x " << juce::String (result[1] * 100.0, 2) << "%"
                      << "  (" << juce::String (improvement, 1) << "x cleaner)" << std::endl;
        }

        proc.setOversampling (true);
        std::cout << "oversampling: " << (allOk ? "PASS" : "FAIL") << std::endl;
        return allOk ? 0 : 1;
    }

    // --- Lockable reels: ROLL must keep exactly the locked groups and re-roll
    //     everything else. Plus the output meter tracks the real signal. ---
    bool lockTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "locktest") lockTest = true;
    if (lockTest)
    {
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);

        // Compare two patches reel by reel: did this group survive the roll?
        auto reelMatches = [] (const Patch& a, const Patch& b, int reel)
        {
            Patch probeA = a, probeB = a;      // same base...
            copyReel (probeB, b, reel);        // ...with only this reel taken from b
            juce::MemoryBlock ma, mb;
            { juce::MemoryOutputStream os (ma, false); writePatch (os, probeA); }
            { juce::MemoryOutputStream os (mb, false); writePatch (os, probeB); }
            return ma == mb;
        };

        bool allOk = true;
        for (int locked = 0; locked < NumReels; ++locked)
        {
            for (int r = 0; r < NumReels; ++r) proc.setReelLocked (r, r == locked);

            proc.pullLever();
            const Patch before = proc.getPatch();

            // Roll several times: the locked reel must never move, and at least
            // one other reel must (otherwise the lock is doing nothing).
            bool lockedHeld = true, othersMoved = false;
            for (int k = 0; k < 6; ++k)
            {
                proc.pullLever();
                const Patch after = proc.getPatch();
                lockedHeld = lockedHeld && reelMatches (before, after, locked);
                for (int r = 0; r < NumReels; ++r)
                    if (r != locked && ! reelMatches (before, after, r))
                        othersMoved = true;
            }

            const bool ok = lockedHeld && othersMoved;
            allOk = allOk && ok;
            std::cout << "lock " << juce::String (reelName (locked)).paddedRight (' ', 5)
                      << (ok ? "PASS" : "FAIL")
                      << (lockedHeld ? "  held" : "  LOCKED REEL CHANGED")
                      << (othersMoved ? ", rest re-rolled" : ", NOTHING ELSE CHANGED")
                      << std::endl;
        }

        // Every reel locked: the sound must be identical apart from metadata.
        {
            for (int r = 0; r < NumReels; ++r) proc.setReelLocked (r, true);
            proc.pullLever();
            Patch a = proc.getPatch();
            proc.pullLever();
            Patch b = proc.getPatch();
            a.seed = b.seed = 0; a.chaos = b.chaos = false;
            a.archetypeName = b.archetypeName = {};
            a.modifierName = b.modifierName = {};
            juce::MemoryBlock ma, mb;
            { juce::MemoryOutputStream os (ma, false); writePatch (os, a); }
            { juce::MemoryOutputStream os (mb, false); writePatch (os, b); }
            const bool ok = (ma == mb);
            allOk = allOk && ok;
            std::cout << "lock ALL   " << (ok ? "PASS" : "FAIL") << "  (sound unchanged)" << std::endl;
        }

        for (int r = 0; r < NumReels; ++r) proc.setReelLocked (r, false);

        // --- Meter: must follow the rendered peak, and flag clipping ---
        {
            proc.rollSeed (4821);
            Patch p = proc.getPatch();
            p.ampS = 1.0f; p.master = 0.8f;
            proc.setPatch (p);
            settleAfterRoll();

            juce::AudioBuffer<float> work (2, block);
            float renderedPeak = 0.0f;
            for (int s = 0; s < (int) (sr * 0.5); s += block)
            {
                work.clear();
                juce::MidiBuffer midi;
                if (s == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                proc.processBlock (work, midi);
                renderedPeak = juce::jmax (renderedPeak, work.getMagnitude (0, block));
            }
            const float shown = proc.getMeterLevel (0);
            const bool ok = shown > 0.05f && shown <= renderedPeak + 0.01f;
            allOk = allOk && ok;
            std::cout << "meter tracks output: " << (ok ? "PASS" : "FAIL")
                      << "  meter=" << juce::String (shown, 3)
                      << " rendered peak=" << juce::String (renderedPeak, 3) << std::endl;

            // Release the note: the meter has to come down with the tail, and
            // must never read higher than what the tail is actually doing.
            const float peakWhileHeld = proc.getMeterLevel (0);
            float tailPeak = 0.0f;
            for (int s = 0; s < (int) (sr * 2.0); s += block)
            {
                work.clear();
                juce::MidiBuffer midi;
                if (s == 0) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                proc.processBlock (work, midi);
                if (s > (int) (sr * 1.5))      // the last half second only
                    tailPeak = juce::jmax (tailPeak, work.getMagnitude (0, block));
            }
            const float now = proc.getMeterLevel (0);
            const bool decayed = now < peakWhileHeld && now <= juce::jmax (0.02f, tailPeak) + 0.01f;
            allOk = allOk && decayed;
            std::cout << "meter follows tail: " << (decayed ? "PASS" : "FAIL")
                      << "  meter " << juce::String (peakWhileHeld, 3)
                      << " -> " << juce::String (now, 3)
                      << "  (tail peak " << juce::String (tailPeak, 3) << ")" << std::endl;
        }

        // Locks are session config, so they have to survive a save/load too.
        {
            proc.setReelLocked (ReelOsc, true);
            proc.setReelLocked (ReelFx, true);
            juce::MemoryBlock state;
            proc.getStateInformation (state);

            GambleSynthProcessor loaded;
            loaded.setStateInformation (state.getData(), (int) state.getSize());
            const bool ok = loaded.isReelLocked (ReelOsc) && loaded.isReelLocked (ReelFx)
                         && ! loaded.isReelLocked (ReelEnv);
            allOk = allOk && ok;
            std::cout << "locks survive save/load: " << (ok ? "PASS" : "FAIL") << std::endl;
            for (int r = 0; r < NumReels; ++r) proc.setReelLocked (r, false);
        }

        std::cout << "locks + meter: " << (allOk ? "PASS" : "FAIL") << std::endl;
        return allOk ? 0 : 1;
    }

    // --- Dev panel (seed 777): live edits must apply instantly, without the
    //     roll cut muting the output and without filling up undo history. ---
    bool devTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "devtest") devTest = true;
    if (devTest)
    {
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);

        // 1. The panel exposes the whole patch.
        DevPanel panel (proc);
        panel.setBounds (0, 0, 400, 600);
        panel.syncFromPatch();
        const int nParams = panel.getNumParams();
        const bool paramsOk = nParams > 50;
        std::cout << "dev panel params: " << (paramsOk ? "PASS" : "FAIL")
                  << "  (" << nParams << " sliders)" << std::endl;

        // 2. Editing while a note sustains must not gap the audio.
        // A fully specified patch, not whatever a seed happens to roll: the edit
        // under test is a cutoff sweep, so the filter has to be one where cutoff
        // actually attenuates (a comb or formant would barely move).
        Patch held;
        held.osc[0] = { 2, 0, 0.0f, 0.8f };
        held.osc[1] = { 2, 0, 0.0f, 0.0f };
        held.osc[2] = { 2, 0, 0.0f, 0.0f };
        held.oscMode = 0; held.unisonVoices = 1; held.subLevel = 0.0f; held.noiseLevel = 0.0f;
        held.filterModel = 0; held.filterType = 0; held.filterPoles = 2;
        held.cutoff = 8000.0f; held.resonance = 0.1f;
        held.filterEnvAmt = 0.0f; held.keytrack = 0.0f;
        held.ampA = 0.01f; held.ampD = 0.05f; held.ampS = 1.0f; held.ampR = 0.2f;
        held.mod[0].dest = ModNone; held.velToAmp = 0.0f; held.velToFilter = 0.0f;
        held.chorusMix = 0.0f; held.drive = 0.0f; held.foldAmount = 0.0f;
        held.crushBits = 16.0f; held.crushRate = 1.0f;
        held.phaserMix = 0.0f; held.flangerMix = 0.0f; held.compAmount = 0.0f;
        held.delayMix = 0.0f; held.reverbMix = 0.0f; held.master = 0.8f;
        proc.setPatch (held);
        settleAfterRoll();

        const int total = (int) (sr * 1.0);
        juce::AudioBuffer<float> out (2, total), work (2, block);
        out.clear();
        const int editAt = total / 2;

        for (int s = 0; s < total; s += block)
        {
            const int n = juce::jmin (block, total - s);
            work.clear();
            juce::MidiBuffer midi;
            if (s == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            if (s <= editAt && s + n > editAt)
            {
                Patch edited = proc.getPatch();
                edited.cutoff = 400.0f;            // audible change, mid-note
                proc.applyLiveEdit (edited);
            }
            juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
            proc.processBlock (sub, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, s, work, ch, 0, n);
        }

        // The roll cut would leave ~500 near-silent samples; a live edit none.
        int longestGap = 0, gap = 0;
        for (int n = editAt - 1000; n < total; ++n)
        {
            if (std::abs (out.getSample (0, n)) < 1.0e-5f) { if (++gap > longestGap) longestGap = gap; }
            else gap = 0;
        }
        const bool noCut = longestGap < 64;

        // And it has to actually change the sound.
        const float before = out.getRMSLevel (0, editAt - 8000, 6000);
        const float after  = out.getRMSLevel (0, editAt + 2000, 6000);
        const bool changed = std::abs (after - before) > 0.005f;

        std::cout << "live edit, no cut: " << (noCut ? "PASS" : "FAIL")
                  << "  longest silent run=" << longestGap << " samples" << std::endl;
        std::cout << "live edit audible: " << (changed ? "PASS" : "FAIL")
                  << "  rms " << juce::String (before, 3) << " -> " << juce::String (after, 3) << std::endl;

        // 3. Live edits must not become undo steps.
        proc.rollSeed (111111);
        proc.rollSeed (222222);
        for (int k = 0; k < 20; ++k)
        {
            Patch e = proc.getPatch();
            e.cutoff = 300.0f + 40.0f * k;
            proc.applyLiveEdit (e);
        }
        proc.undo();
        const bool historyOk = (proc.getPatch().seed == 111111);
        std::cout << "undo skips edits: " << (historyOk ? "PASS" : "FAIL")
                  << "  (undo landed on seed " << proc.getPatch().seed << ")" << std::endl;

        // 4. The editor's 777 toggle: panel in, panel out, no crash either way.
        bool editorOk = false;
        {
            std::unique_ptr<GambleSynthEditor> ed (
                dynamic_cast<GambleSynthEditor*> (proc.createEditor()));
            if (ed != nullptr)
            {
                // Dev mode has to make room for the panel — by widening when the
                // machine artwork is up, or by growing taller on the plain UI.
                const int plainArea = ed->getWidth() * ed->getHeight();
                ed->setDevMode (true);
                const int devArea = ed->getWidth() * ed->getHeight();
                ed->setSize (1200, 800);             // exercise layout with the panel up
                ed->setDevMode (false);
                editorOk = (devArea > plainArea) && (ed->getWidth() > 0) && (ed->getHeight() > 0);
            }
        }
        std::cout << "editor 777 toggle: " << (editorOk ? "PASS" : "FAIL") << std::endl;

        // The window must open at one size every time, and may only be resized
        // along the artwork's diagonal. setSize bypasses the constrainer, so
        // this goes through setBoundsConstrained the way a real drag does.
        bool shapeOk = false, leverOk = false;
        {
            std::unique_ptr<GambleSynthEditor> a (
                dynamic_cast<GambleSynthEditor*> (proc.createEditor()));
            std::unique_ptr<GambleSynthEditor> b (
                dynamic_cast<GambleSynthEditor*> (proc.createEditor()));

            if (a != nullptr && b != nullptr)
            {
                const bool sameOpening = (a->getWidth() == b->getWidth())
                                      && (a->getHeight() == b->getHeight());

                bool ratioHeld = true;
                for (auto target : { juce::Rectangle<int> (0, 0, 1200, 500),
                                     juce::Rectangle<int> (0, 0, 400, 1400),
                                     juce::Rectangle<int> (0, 0, 900, 900) })
                {
                    a->setBoundsConstrained (target);
                    const float got = (float) a->getWidth() / (float) juce::jmax (1, a->getHeight());
                    if (std::abs (got - Skin::aspect()) > 0.02f) ratioHeld = false;
                }

                shapeOk = sameOpening && ratioHeld;
                std::cout << "opens at " << b->getWidth() << "x" << b->getHeight()
                          << ", ratio " << (ratioHeld ? "held" : "BROKEN") << std::endl;
            }
        }
        std::cout << "window shape: " << (shapeOk ? "PASS" : "FAIL") << std::endl;

        // The lever: half a second, down through every frame and back to rest.
        {
            juce::Array<int> seen;
            int last = -1;
            for (int ms = 0; ms < LeverDisplay::PullMs; ++ms)
            {
                const int f = LeverDisplay::frameAt (ms);
                if (f != last) { seen.add (f); last = f; }
            }

            const bool reachesEnd  = seen.contains (LeverDisplay::frameCount() - 1);
            const bool usesAll     = seen.size() >= LeverDisplay::frameCount();
            const bool restsAfter  = LeverDisplay::frameAt (LeverDisplay::PullMs) == 0
                                  && LeverDisplay::frameAt (LeverDisplay::PullMs + 50) == 0;
            const bool comesBack   = seen.size() > 1 && seen.getLast() == 0;

            juce::String seq;
            for (int f : seen) seq << f << " ";

            leverOk = reachesEnd && usesAll && restsAfter && comesBack;
            std::cout << "lever " << LeverDisplay::PullMs << "ms: "
                      << (leverOk ? "PASS" : "FAIL") << "  frames " << seq.trim() << std::endl;
        }

        // 5. Locks are dev-mode only: leaving dev mode must drop them, or they'd
        //    keep shaping every roll with their buttons hidden.
        bool locksGatedOk = false;
        {
            std::unique_ptr<GambleSynthEditor> ed (
                dynamic_cast<GambleSynthEditor*> (proc.createEditor()));
            if (ed != nullptr)
            {
                ed->setDevMode (true);
                proc.setReelLocked (ReelOsc, true);
                proc.setReelLocked (ReelFx, true);
                const bool heldWhileDev = proc.anyReelLocked();
                ed->setDevMode (false);
                locksGatedOk = heldWhileDev && ! proc.anyReelLocked();
            }
        }
        std::cout << "locks gated by dev mode: " << (locksGatedOk ? "PASS" : "FAIL") << std::endl;

        const bool allOk = paramsOk && noCut && changed && historyOk && editorOk
                        && locksGatedOk && shapeOk && leverOk;
        std::cout << "dev panel: " << (allOk ? "PASS" : "FAIL") << std::endl;
        return allOk ? 0 : 1;
    }

    // --- Tempo sync: pretend to be a host at a couple of tempos and measure
    //     the delay and gate periods that actually come out. ---
    bool syncTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "synctest") syncTest = true;
    if (syncTest)
    {
        struct FakeHost : juce::AudioPlayHead
        {
            double bpm = 120.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo info;
                info.setBpm (bpm);
                info.setIsPlaying (true);
                return info;
            }
        };

        FakeHost host;
        proc.setPlayHead (&host);
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);

        const int total = (int) (sr * 3.0);
        juce::AudioBuffer<float> out (2, total), work (2, block);

        auto render = [&] (GambleSynthProcessor& target, const Patch& p, bool holdNote)
        {
            target.setPatch (p);
            for (int b = 0; b < 4; ++b)   // let the roll cut settle
            {
                work.clear();
                juce::MidiBuffer empty;
                juce::AudioBuffer<float> warm (work.getArrayOfWritePointers(), 2, 0, block);
                target.processBlock (warm, empty);
            }
            out.clear();
            for (int s = 0; s < total; s += block)
            {
                const int n = juce::jmin (block, total - s);
                work.clear();
                juce::MidiBuffer midi;
                if (s == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                if (s == 0 && ! holdNote)                    // blip for the delay test;
                    midi.addEvent (juce::MidiMessage::noteOff (1, 60), block / 4);
                juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
                target.processBlock (sub, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, s, work, ch, 0, n);
            }
        };

        // Short-window energy envelope, used to find echoes / gate pulses.
        auto envelope = [&] (int hop)
        {
            std::vector<float> env;
            for (int s = 0; s + hop <= total; s += hop)
                env.push_back (out.getMagnitude (0, s, hop));
            return env;
        };

        auto plain = []
        {
            Patch p;
            p.osc[0] = { 2, 0, 0.0f, 0.8f };
            p.osc[1] = { 2, 0, 0.0f, 0.0f };
            p.osc[2] = { 2, 0, 0.0f, 0.0f };
            p.oscMode = 0; p.unisonVoices = 1;
            p.filterModel = 0; p.filterType = 0; p.cutoff = 5000.0f; p.keytrack = 0.0f;
            p.filterEnvAmt = 0.0f; p.mod[0].dest = ModNone;
            p.ampA = 0.002f; p.ampD = 0.05f; p.ampS = 0.0f; p.ampR = 0.05f;   // short blip
            p.velToAmp = 0.0f; p.velToFilter = 0.0f; p.stereoWidth = 0.0f;
            p.chorusMix = 0.0f; p.drive = 0.0f; p.reverbMix = 0.0f;
            p.master = 0.8f;
            return p;
        };

        bool allOk = true;

        // --- Delay: a blip plus its echoes; measure the gap to the first echo ---
        for (double bpm : { 120.0, 90.0 })
        {
            host.bpm = bpm;
            Patch p = plain();
            p.delayMix = 0.6f; p.delayFb = 0.5f; p.delaySyncDiv = 5;   // 1/4 note
            render (proc, p, false);

            const int hop = 128;
            const auto env = envelope (hop);
            const int skip = (int) (sr * 0.15) / hop;      // past the direct blip
            int peakAt = skip; float peak = 0.0f;
            for (int k = skip; k < (int) env.size(); ++k)
                if (env[(size_t) k] > peak) { peak = env[(size_t) k]; peakAt = k; }

            const double measured = peakAt * hop / sr;
            const double expected = syncDivBeats (5) * 60.0 / bpm;
            const bool ok = std::abs (measured - expected) < 0.02;
            allOk = allOk && ok;
            std::cout << "delay 1/4 @ " << (int) bpm << " BPM: " << (ok ? "PASS" : "FAIL")
                      << "  expected " << juce::String (expected, 3) << " s"
                      << "  measured " << juce::String (measured, 3) << " s" << std::endl;
        }

        // --- Gate: sustained note chopped at 1/8; measure the pulse period ---
        for (double bpm : { 120.0, 90.0 })
        {
            host.bpm = bpm;
            Patch p = plain();
            p.ampS = 1.0f; p.ampD = 0.05f; p.ampR = 0.1f;
            p.mod[0] = { ModSquare, ModAmp, 8.0f, 1.0f, 0.0f, 3 };     // 1/8
            render (proc, p, true);   // the gate needs a sustained note

            // Window has to be well above the note's own period (~4 ms) and well
            // below the gate period (250 ms), or this counts the waveform.
            const int hop = 512;
            const auto env = envelope (hop);

            double peakEnv = 0.0;
            for (float v : env) peakEnv = juce::jmax (peakEnv, (double) v);

            // Schmitt trigger: one count per gate opening.
            const double hi = peakEnv * 0.6, lo = peakEnv * 0.25;
            int crossings = 0;
            bool open = false;
            const int from = (int) (sr * 0.3) / hop, to = (int) (sr * 2.3) / hop;
            for (int k = from; k < to && k < (int) env.size(); ++k)
            {
                if (! open && env[(size_t) k] > hi) { open = true; ++crossings; }
                else if (open && env[(size_t) k] < lo) open = false;
            }

            const double measured = crossings > 0 ? 2.0 / crossings : 0.0;   // 2 s window
            const double expected = syncDivBeats (3) * 60.0 / bpm;
            const bool ok = crossings > 0 && std::abs (measured - expected) < 0.03;
            allOk = allOk && ok;
            std::cout << "gate 1/8 @ " << (int) bpm << " BPM:  " << (ok ? "PASS" : "FAIL")
                      << "  expected " << juce::String (expected, 3) << " s"
                      << "  measured " << juce::String (measured, 3) << " s"
                      << "  (" << crossings << " pulses)" << std::endl;
        }

        // --- No host at all (the standalone): must fall back to 120 BPM.
        //     A fresh processor, because a plugin that *had* a host keeps the
        //     last tempo it saw on purpose. ---
        {
            GambleSynthProcessor standalone;
            standalone.setPlayConfigDetails (0, 2, sr, block);
            standalone.prepareToPlay (sr, block);

            Patch p = plain();
            p.delayMix = 0.6f; p.delayFb = 0.5f; p.delaySyncDiv = 5;
            render (standalone, p, false);
            const int hop = 128;
            const auto env = envelope (hop);
            const int skip = (int) (sr * 0.15) / hop;
            int peakAt = skip; float peak = 0.0f;
            for (int k = skip; k < (int) env.size(); ++k)
                if (env[(size_t) k] > peak) { peak = env[(size_t) k]; peakAt = k; }
            const double measured = peakAt * hop / sr;
            const bool ok = std::abs (measured - 0.5) < 0.02;    // 1/4 @ 120
            allOk = allOk && ok;
            std::cout << "no host (fallback 120): " << (ok ? "PASS" : "FAIL")
                      << "  measured " << juce::String (measured, 3) << " s" << std::endl;
        }

        std::cout << "tempo sync: " << (allOk ? "PASS" : "FAIL") << std::endl;
        return allOk ? 0 : 1;
    }

    // --- FX rack: each effect on its own over the same plain note. Checks it
    //     stays finite, actually changes the sound, and doesn't run away. ---
    bool fxTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "fxtest") fxTest = true;
    if (fxTest)
    {
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);

        auto plain = []
        {
            Patch p;
            p.osc[0] = { 2, 0, 0.0f, 0.8f };          // one saw, plenty of harmonics
            p.osc[1] = { 2, 0, 0.0f, 0.0f };
            p.osc[2] = { 2, 0, 0.0f, 0.0f };
            p.oscMode = 0; p.unisonVoices = 1; p.subLevel = 0.0f; p.noiseLevel = 0.0f;
            p.filterModel = 0; p.filterType = 0; p.cutoff = 6000.0f; p.resonance = 0.1f;
            p.filterEnvAmt = 0.0f; p.keytrack = 0.0f;
            p.ampA = 0.01f; p.ampD = 0.1f; p.ampS = 1.0f; p.ampR = 0.2f;
            p.mod[0].dest = ModNone; p.velToAmp = 0.0f; p.velToFilter = 0.0f;
            p.stereoWidth = 0.0f; p.voiceMode = 0;
            p.chorusMix = 0.0f; p.drive = 0.0f; p.delayMix = 0.0f; p.reverbMix = 0.0f;
            p.master = 0.8f;
            return p;
        };

        struct Cfg { const char* name; std::function<void (Patch&)> tweak; };
        const Cfg cfgs[] = {
            { "clean",     [] (Patch&) {} },
            { "drive",     [] (Patch& p) { p.drive = 0.8f; } },
            { "fold",      [] (Patch& p) { p.foldAmount = 0.7f; } },
            { "crush-bits",[] (Patch& p) { p.crushBits = 4.0f; } },
            { "crush-rate",[] (Patch& p) { p.crushRate = 8.0f; } },
            { "phaser",    [] (Patch& p) { p.phaserMix = 0.8f; p.phaserRate = 0.5f;
                                           p.phaserDepth = 0.9f; p.phaserFb = 0.6f; } },
            { "flanger",   [] (Patch& p) { p.flangerMix = 0.7f; p.flangerRate = 0.3f;
                                           p.flangerDepth = 0.9f; p.flangerFb = 0.8f; } },
            { "comp",      [] (Patch& p) { p.compAmount = 0.85f; } },
            { "everything",[] (Patch& p) { p.drive = 0.6f; p.foldAmount = 0.5f;
                                           p.crushBits = 6.0f; p.crushRate = 4.0f;
                                           p.phaserMix = 0.6f; p.flangerMix = 0.5f;
                                           p.flangerFb = 0.8f; p.compAmount = 0.7f;
                                           p.chorusMix = 0.4f; p.delayMix = 0.3f;
                                           p.reverbMix = 0.4f; } },
        };

        const int total = (int) (sr * 1.5);
        juce::AudioBuffer<float> out (2, total), work (2, block), dry (2, total);

        auto renderCfg = [&] (const Patch& p, juce::AudioBuffer<float>& dest)
        {
            proc.setPatch (p);
            settleAfterRoll();
            dest.clear();
            for (int s = 0; s < total; s += block)
            {
                const int n = juce::jmin (block, total - s);
                work.clear();
                juce::MidiBuffer midi;
                if (s == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 55, 0.9f), 0);
                juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
                proc.processBlock (sub, midi);
                for (int ch = 0; ch < 2; ++ch) dest.copyFrom (ch, s, work, ch, 0, n);
            }
        };

        renderCfg (plain(), dry);
        const float dryRms = dry.getRMSLevel (0, 0, total);
        bool allOk = true;

        for (const auto& cfg : cfgs)
        {
            Patch p = plain();
            cfg.tweak (p);
            renderCfg (p, out);

            const float peak = out.getMagnitude (0, total);
            const float rms  = out.getRMSLevel (0, 0, total);
            bool finite = true;
            for (int n = 0; n < total && finite; ++n)
                if (! std::isfinite (out.getSample (0, n))) finite = false;

            // How far it moved from the dry render — an effect that changes
            // nothing is a wiring bug.
            double diff = 0.0;
            for (int n = 0; n < total; ++n)
                diff += std::abs (out.getSample (0, n) - dry.getSample (0, n));
            diff /= total;

            const bool isClean = juce::String (cfg.name) == "clean";
            const bool changed = isClean ? (diff < 1.0e-6) : (diff > 0.001);
            const bool sane    = finite && peak > 0.01f && peak <= 1.01f && rms > 0.005f;
            const bool ok      = changed && sane;
            allOk = allOk && ok;

            std::cout << juce::String (cfg.name).paddedRight (' ', 11)
                      << (ok ? "PASS" : "FAIL")
                      << "  peak=" << juce::String (peak, 3)
                      << " rms=" << juce::String (rms, 3)
                      << " (dry " << juce::String (dryRms, 3) << ")"
                      << " delta=" << juce::String (diff, 4)
                      << (finite ? "" : "  NON-FINITE")
                      << (changed ? "" : (isClean ? "  DRIFTED" : "  NO EFFECT"))
                      << std::endl;
        }

        std::cout << "fx rack: " << (allOk ? "PASS" : "FAIL") << std::endl;
        return allOk ? 0 : 1;
    }

    // --- Variety: what 40 consecutive pulls actually land on. Checks the
    //     no-repeat rule and that seeds still map 1:1 to sounds. ---
    bool varietyTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "varietytest") varietyTest = true;
    if (varietyTest)
    {
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);

        juce::StringArray seen;
        juce::String prev1, prev2, prevMod;
        int archRepeats = 0, modRepeats = 0, withMod = 0;
        const int rolls = 40;

        // Every roll also gets played, so a modifier can't sneak in a dud
        // (inaudible) or a blowout (pinned against the limiter).
        const int noteLen = (int) (sr * 1.2);
        juce::AudioBuffer<float> seg (2, noteLen), work (2, block);
        float loudest = 0.0f, quietest = 1.0f;
        juce::String loudestName, quietestName;
        int bad = 0, hotRolls = 0;

        for (int r = 0; r < rolls; ++r)
        {
            proc.pullLever();
            const auto& p = proc.getPatch();
            const juce::String label = p.archetypeName
                                     + (p.modifierName.isEmpty() ? juce::String() : " + " + p.modifierName);

            settleAfterRoll();
            seg.clear();
            for (int s = 0; s < noteLen; s += block)
            {
                const int n = juce::jmin (block, noteLen - s);
                work.clear();
                juce::MidiBuffer midi;
                if (s == 0)
                    for (int note : { 48, 55, 64 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.85f), 0);
                juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
                proc.processBlock (sub, midi);
                for (int ch = 0; ch < 2; ++ch) seg.copyFrom (ch, s, work, ch, 0, n);
            }

            const float peak = seg.getMagnitude (0, noteLen);
            const float rms  = seg.getRMSLevel (0, 0, noteLen);
            bool finite = true;
            for (int n = 0; n < noteLen && finite; ++n)
                if (! std::isfinite (seg.getSample (0, n))) finite = false;

            // A dud (inaudible) or non-finite roll is a defect. A hot roll is
            // only pinned against the limiter — worth watching, not a failure.
            const bool dud = rms < 0.01f, hot = peak > 0.995f;
            if (! finite || dud) ++bad;
            if (hot) ++hotRolls;
            if (peak > loudest)  { loudest = peak;  loudestName = label; }
            if (rms < quietest)  { quietest = rms;  quietestName = label; }

            std::cout << juce::String (r + 1).paddedLeft (' ', 3) << ". "
                      << label.paddedRight (' ', 26)
                      << " peak=" << juce::String (peak, 3) << " rms=" << juce::String (rms, 3)
                      << " probe=" << juce::String (proc.probeLevel (p), 4)
                      << (finite ? "" : "  NON-FINITE") << (dud ? "  DUD" : "") << (hot ? "  HOT" : "")
                      << std::endl;

            if (dud || hot || ! finite)   // why did this one misbehave?
                std::cout << "        seed=" << p.seed
                          << " ftype=" << p.filterType << " fmodel=" << p.filterModel
                          << " cut=" << (int) p.cutoff << " res=" << juce::String (p.resonance, 2)
                          << " fenv=" << juce::String (p.filterEnvAmt, 2)
                          << " keytrk=" << juce::String (p.keytrack, 2)
                          << " amp=" << juce::String (p.ampA, 2) << "/" << juce::String (p.ampD, 2)
                          << "/" << juce::String (p.ampS, 2) << "/" << juce::String (p.ampR, 2)
                          << " modS=" << juce::String (p.modS, 2)
                          << " osc=" << p.oscMode << " fm=" << juce::String (p.fmAmount, 2)
                          << " lvl=" << juce::String (p.osc[0].level, 2)
                          << " vmode=" << p.voiceMode
                          << " master=" << juce::String (p.master, 2) << std::endl;

            if (p.archetypeName == prev1 || p.archetypeName == prev2) ++archRepeats;
            if (p.modifierName.isNotEmpty())
            {
                ++withMod;
                if (p.modifierName == prevMod) ++modRepeats;
                prevMod = p.modifierName;
            }
            prev2 = prev1; prev1 = p.archetypeName;
            seen.addIfNotAlreadyThere (label);
        }

        // Same seed must still give the same sound, modifier included.
        proc.rollSeed (4821);
        juce::MemoryBlock a; { juce::MemoryOutputStream os (a, false); writePatch (os, proc.getPatch()); }
        proc.pullLever();
        proc.rollSeed (4821);
        juce::MemoryBlock b; { juce::MemoryOutputStream os (b, false); writePatch (os, proc.getPatch()); }

        // CHAOS has no archetypes, so the no-repeat rule doesn't apply to it.
        const bool chaosRun = proc.isChaos();
        const bool ok = (chaosRun || (archRepeats == 0 && modRepeats == 0))
                        && (a == b) && (bad == 0);
        std::cout << "\nunique combos: " << seen.size() << "/" << rolls
                  << "   with modifier: " << withMod
                  << "   archetype repeats: " << archRepeats
                  << "   modifier repeats: " << modRepeats
                  << "   seed still 1:1: " << (a == b ? "yes" : "NO") << std::endl;
        std::cout << "bad rolls: " << bad << "   hot (limiter-pinned): " << hotRolls
                  << "   loudest: " << loudestName << " (" << loudest << ")"
                  << "   quietest: " << quietestName << " (rms " << quietest << ")" << std::endl;
        std::cout << "variety: " << (ok ? "PASS" : "FAIL") << std::endl;
        return ok ? 0 : 1;
    }

    // --- Click hunt: hold one plain note per config and look for discontinuities.
    //     Each stage of the signal path is enabled on its own so a glitch points
    //     straight at the culprit. ---
    bool clickTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "clicktest") clickTest = true;
    if (clickTest)
    {
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);

        auto plain = []
        {
            Patch p;
            p.osc[0] = { 0, 0, 0.0f, 0.8f };          // one sine, nothing else
            p.osc[1] = { 0, 0, 0.0f, 0.0f };
            p.osc[2] = { 0, 0, 0.0f, 0.0f };
            p.oscMode = 0; p.unisonVoices = 1; p.subLevel = 0.0f; p.noiseLevel = 0.0f;
            p.filterModel = 0; p.filterPoles = 2; p.filterType = 0;
            p.cutoff = 5000.0f; p.resonance = 0.1f; p.filterEnvAmt = 0.0f; p.keytrack = 0.0f;
            p.ampA = 0.01f; p.ampD = 0.1f; p.ampS = 1.0f; p.ampR = 0.2f;
            p.mod[0].dest = ModNone; p.velToAmp = 0.0f; p.velToFilter = 0.0f;
            p.stereoWidth = 0.0f; p.voiceMode = 0;
            p.chorusMix = 0.0f; p.drive = 0.0f;
            p.delayMix = 0.0f; p.reverbMix = 0.0f; p.reverbSize = 0.4f;
            p.master = 0.8f;
            return p;
        };

        struct Cfg { const char* name; std::function<void (Patch&)> tweak; };
        const Cfg cfgs[] = {
            { "clean",      [] (Patch&) {} },
            { "chorus",     [] (Patch& p) { p.chorusMix = 0.5f; } },
            { "delay",      [] (Patch& p) { p.delayMix = 0.35f; p.delayTime = 0.3f; p.delayFb = 0.4f; } },
            { "reverb",     [] (Patch& p) { p.reverbMix = 0.4f; } },
            { "drive",      [] (Patch& p) { p.drive = 0.6f; } },
            { "lfo-filter", [] (Patch& p) { p.cutoff = 1200.f;
                                            p.mod[0] = { ModSine, ModCutoff, 3.0f, 0.6f, 0.0f, 0 }; } },
            { "filter-env", [] (Patch& p) { p.cutoff = 800.f; p.filterEnvAmt = 0.7f; p.modD = 0.5f; p.modS = 0.3f; } },
            { "ladder",     [] (Patch& p) { p.filterModel = 1; p.filterPoles = 4; p.cutoff = 1500.f; } },
            { "comb",       [] (Patch& p) { p.filterModel = 4; p.cutoff = 400.f; } },
            { "formant",    [] (Patch& p) { p.filterModel = 3; } },
        };

        const int total = (int) (sr * 1.5);
        juce::AudioBuffer<float> out (2, total), work (2, block);

        for (const auto& cfg : cfgs)
        {
            Patch p = plain();
            cfg.tweak (p);
            proc.setPatch (p);
            settleAfterRoll();
            out.clear();

            for (int s = 0; s < total; s += block)
            {
                const int n = juce::jmin (block, total - s);
                work.clear();
                juce::MidiBuffer midi;
                if (s == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
                proc.processBlock (sub, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, s, work, ch, 0, n);
            }

            // Ignore the first 0.25 s (attack) and measure sample-to-sample jumps
            // against the median jump of a steady tone.
            const int from = (int) (sr * 0.25);
            const float* d = out.getReadPointer (0);
            std::vector<float> deltas;
            deltas.reserve ((size_t) (total - from));
            for (int n = from + 1; n < total; ++n) deltas.push_back (std::abs (d[n] - d[n - 1]));
            std::vector<float> sorted (deltas);
            std::nth_element (sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
            const float med = juce::jmax (1.0e-6f, sorted[sorted.size() / 2]);

            int spikes = 0; float worst = 0.0f; int worstAt = -1;
            std::vector<int> firstFew;
            for (size_t k = 0; k < deltas.size(); ++k)
            {
                if (deltas[k] > med * 8.0f)
                {
                    ++spikes;
                    if (firstFew.size() < 6) firstFew.push_back (from + 1 + (int) k);
                }
                if (deltas[k] > worst) { worst = deltas[k]; worstAt = from + 1 + (int) k; }
            }
            std::cout << juce::String (cfg.name).paddedRight (' ', 11)
                      << " spikes=" << spikes
                      << "  worstJump=" << worst << " (median " << med << ") at " << worstAt;
            if (! firstFew.empty())
            {
                std::cout << "  positions:";
                for (int q : firstFew) std::cout << " " << q << "(mod512=" << q % 512 << ")";
            }
            std::cout << std::endl;
        }

        // Voice stealing: long release + more notes than voices. JUCE hard-stops
        // the stolen voice, so any click here is a cut-off tail.
        {
            Patch p = plain();
            p.ampR = 2.5f; p.ampS = 0.9f; p.ampA = 0.01f;
            proc.setPatch (p);
            settleAfterRoll();
            out.clear();
            int note = 36;
            for (int s = 0; s < total; s += block)
            {
                const int n = juce::jmin (block, total - s);
                work.clear();
                juce::MidiBuffer midi;
                if ((s / block) % 2 == 0 && note < 36 + 40)                 // a new note every ~23 ms
                    midi.addEvent (juce::MidiMessage::noteOn (1, note++, 0.9f), 0);
                juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
                proc.processBlock (sub, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, s, work, ch, 0, n);
            }
            // A click is an *isolated* jump: far bigger than the slope either side
            // of it. Plain loud high-frequency content fails that test.
            const float* d = out.getReadPointer (0);
            std::vector<float> dv ((size_t) total, 0.0f);
            for (int n = 1; n < total; ++n) dv[(size_t) n] = std::abs (d[n] - d[n - 1]);

            int clicks = 0; float worst = 0.0f;
            std::vector<int> where;
            for (int n = 4; n < total - 4; ++n)
            {
                float around = 0.0f;
                for (int k = 1; k <= 3; ++k)
                    around = juce::jmax (around, dv[(size_t) (n - k)], dv[(size_t) (n + k)]);
                if (dv[(size_t) n] > 0.05f && dv[(size_t) n] > around * 4.0f)
                {
                    ++clicks;
                    worst = juce::jmax (worst, dv[(size_t) n]);
                    if (where.size() < 8) where.push_back (n);
                }
            }
            std::cout << "voice-steal  clicks=" << clicks << "  worstClick=" << worst;
            if (! where.empty()) { std::cout << "  at:"; for (int q : where) std::cout << " " << q; }
            std::cout << std::endl;
        }

        // Roll while a note is sounding: the old sound must fade out cleanly and
        // leave nothing behind (voices, delay, chorus and reverb tails).
        {
            Patch p = plain();
            p.ampS = 0.9f; p.ampR = 2.0f;
            p.delayMix = 0.4f; p.delayTime = 0.25f; p.delayFb = 0.6f;
            p.reverbMix = 0.5f; p.chorusMix = 0.4f;
            proc.setPatch (p);
            settleAfterRoll();
            out.clear();

            const int rollAt = (int) (sr * 0.5);
            for (int s = 0; s < total; s += block)
            {
                const int n = juce::jmin (block, total - s);
                work.clear();
                juce::MidiBuffer midi;
                if (s == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                if (s <= rollAt && s + n > rollAt)
                {
                    Patch q = plain();          // "next roll" — silent unless played
                    q.osc[0].wave = 2; q.cutoff = 900.0f;
                    proc.setPatch (q);
                }
                juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
                proc.processBlock (sub, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, s, work, ch, 0, n);
            }

            const float* d = out.getReadPointer (0);
            float worstJump = 0.0f; int worstAt = -1;
            for (int n = rollAt - 1024; n < rollAt + 4096 && n < total; ++n)
                if (n > 0 && std::abs (d[n] - d[n - 1]) > worstJump)
                { worstJump = std::abs (d[n] - d[n - 1]); worstAt = n; }

            // 50 ms after the roll everything should be gone.
            const int quietFrom = rollAt + (int) (sr * 0.05);
            const float leftover = out.getMagnitude (quietFrom, total - quietFrom);
            const float before   = out.getMagnitude (0, rollAt);
            const bool ok = leftover < 1.0e-6f && worstJump < 0.02f;
            std::cout << "roll cut: " << (ok ? "PASS" : "FAIL")
                      << "  beforePeak=" << before
                      << "  leftover=" << leftover
                      << "  worstJump=" << worstJump << " at " << worstAt << std::endl;
            if (! ok) return 1;
        }
        return 0;
    }

    // --- Filter flavour sweep: every model renders, stays finite, makes sound ---
    bool filterTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "filtertest") filterTest = true;
    if (filterTest)
    {
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);

        static const char* names[] = { "SVF", "ladder", "diode", "formant", "comb" };
        static const char* typeNames[] = { "LP", "BP", "HP" };
        struct Case { int model, poles, type; };
        // Every model against every response — a model that outputs silence for
        // one filter type is a dud roll waiting to happen.
        std::vector<Case> cases;
        for (int model = 0; model < 5; ++model)
            for (int poles : { 2, 4 })
                for (int type = 0; type < 3; ++type)
                {
                    if (model >= 3 && poles == 4) continue;   // formant/comb ignore poles
                    cases.push_back ({ model, poles, type });
                }
        const int nCases = (int) cases.size();

        const double segSec = 2.0;
        const int    segSamples = (int) (sr * segSec);
        juce::AudioBuffer<float> out (2, segSamples * nCases), work (2, block);
        out.clear();

        bool allOk = true;
        for (int c = 0; c < nCases; ++c)
        {
            proc.rollSeed (4821);                       // same base sound every time
            Patch p = proc.getPatch();
            p.voiceMode   = 0;
            p.filterModel = cases[c].model;
            p.filterPoles = cases[c].poles;
            p.filterType  = cases[c].type;
            p.filterMorph = 0.5f;
            p.resonance   = 0.5f;
            proc.setPatch (p);
            settleAfterRoll();

            const int base = c * segSamples;
            for (int s = 0; s < segSamples; s += block)
            {
                const int n = juce::jmin (block, segSamples - s);
                work.clear();
                juce::MidiBuffer midi;
                if (s == 0)
                    for (int note : { 48, 55, 64 })
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
                if (s <= (int) (segSamples * 0.7) && s + n > (int) (segSamples * 0.7))
                    for (int note : { 48, 55, 64 })
                        midi.addEvent (juce::MidiMessage::noteOff (1, note),
                                       (int) (segSamples * 0.7) - s);

                juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
                proc.processBlock (sub, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, base + s, work, ch, 0, n);
            }

            const float peak = out.getMagnitude (base, segSamples);
            const float rms  = out.getRMSLevel (0, base, segSamples);
            bool finite = true;
            for (int ch = 0; ch < 2 && finite; ++ch)
            {
                const float* d = out.getReadPointer (ch, base);
                for (int n = 0; n < segSamples; ++n)
                    if (! std::isfinite (d[n])) { finite = false; break; }
            }
            const bool ok = finite && peak > 0.005f && peak <= 1.05f;
            allOk = allOk && ok;
            std::cout << juce::String (names[cases[c].model]).paddedRight (' ', 8)
                      << cases[c].poles << "-pole " << typeNames[cases[c].type] << ": "
                      << (ok ? "PASS" : "FAIL")
                      << "  peak=" << peak << " rms=" << rms
                      << (finite ? "" : "  NON-FINITE") << std::endl;
        }

        juce::File ff (juce::File::getCurrentWorkingDirectory().getChildFile ("gamble_filters.wav"));
        ff.deleteFile();
        juce::WavAudioFormat wav;
        if (auto* st = ff.createOutputStream().release())
        {
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (st, sr, 2, 16, {}, 0));
            if (w) { w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()); std::cout << "wrote " << ff.getFullPathName() << std::endl; }
            else delete st;
        }
        std::cout << "filter flavours: " << (allOk ? "PASS" : "FAIL") << std::endl;
        return allOk ? 0 : 1;
    }

    // --- Mono/legato/glide test: overlapping melody through one voice ---
    bool monoTest = false;
    for (int a = 1; a < argc; ++a) if (juce::String (argv[a]) == "monotest") monoTest = true;
    if (monoTest)
    {
        proc.setChaos (true);
        unsigned s = 1; int tries = 0;
        while ((proc.getPatch().voiceMode == 0 || proc.getPatch().glideTime <= 0.0f)
               && tries++ < 5000) proc.rollSeed (s++);
        std::cout << "found voiceMode=" << proc.getPatch().voiceMode
                  << " glide=" << proc.getPatch().glideTime << std::endl;

        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);
        settleAfterRoll();

        const int total = (int) (sr * 2.0);
        juce::AudioBuffer<float> out (2, total), work (2, block);
        out.clear();
        struct Ev { int at; bool on; int note; };
        const Ev evs[] = { {0,true,48}, {(int)(sr*0.5),true,55}, {(int)(sr*1.0),false,55}, {(int)(sr*1.5),false,48} };

        for (int s0 = 0; s0 < total; s0 += block)
        {
            const int n = juce::jmin (block, total - s0);
            work.clear();
            juce::MidiBuffer midi;
            for (auto& e : evs)
                if (e.at >= s0 && e.at < s0 + n)
                    midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, 0.8f)
                                        : juce::MidiMessage::noteOff (1, e.note), e.at - s0);
            juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
            proc.processBlock (sub, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, s0, work, ch, 0, n);
        }
        bool ok = out.getMagnitude (0, total) > 0.001f;
        for (int q = 0; q < 3; ++q) // first 3 quarters should have sustained sound
            ok = ok && out.getRMSLevel (0, q * total / 4, total / 4) > 0.0005f;
        std::cout << "mono/legato render: " << (ok ? "PASS" : "FAIL")
                  << "  peak=" << out.getMagnitude (0, total) << std::endl;
        return ok ? 0 : 1;
    }

    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);

    const int totalSamples = (int) (sr * seconds);
    juce::AudioBuffer<float> out (2, totalSamples);
    out.clear();

    juce::AudioBuffer<float> work (2, block);
    const double segLen = seconds / numRolls;

    int pos = 0;
    for (int roll = 0; roll < numRolls; ++roll)
    {
        proc.pullLever();
        settleAfterRoll();
        const auto& rp = proc.getPatch();
        DBG (""); std::cout << "Roll " << roll + 1 << ": " << rp.archetypeName
                            << (rp.modifierName.isEmpty() ? juce::String()
                                                          : " + " + rp.modifierName) << std::endl;

        const int segSamples = (roll == numRolls - 1) ? (totalSamples - pos)
                                                       : (int) (segLen * sr);
        const int noteOnAt   = pos;
        const int noteOffAt  = pos + (int) (segSamples * 0.7);

        for (int s = 0; s < segSamples; s += block)
        {
            const int n = juce::jmin (block, segSamples - s);
            work.clear();

            juce::MidiBuffer midi;
            const int global = pos + s;
            auto maybe = [&] (int when, bool on)
            {
                if (when >= global && when < global + n)
                {
                    const int off = when - global;
                    for (int note : { 60, 64, 67 }) // C major triad
                        midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, 0.8f)
                                          : juce::MidiMessage::noteOff (1, note),
                                       off);
                }
            };
            maybe (noteOnAt, true);
            maybe (noteOffAt, false);

            juce::AudioBuffer<float> sub (work.getArrayOfWritePointers(), 2, 0, n);
            proc.processBlock (sub, midi);

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, global, work, ch, 0, n);
        }
        pos += segSamples;
    }

    const float peak = out.getMagnitude (0, totalSamples);
    const float rms  = out.getRMSLevel (0, 0, totalSamples);
    std::cout << "\npeak=" << peak << "  rms=" << rms << std::endl;

    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile ("gamble_render.wav"));
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (f.createOutputStream());
    if (stream)
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sr, 2, 16, {}, 0));
        if (writer)
        {
            stream.release();
            writer->writeFromAudioSampleBuffer (out, 0, totalSamples);
            std::cout << "wrote " << f.getFullPathName() << std::endl;
        }
    }
    return (peak > 0.001f) ? 0 : 1;
}
