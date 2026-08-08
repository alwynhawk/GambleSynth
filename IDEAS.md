# GambleSynth roadmap

A one-button synth: pull the lever, get a random patch. Ranked by what matters for a cheap,
shareable novelty instrument rather than by what would be interesting to build.

## Where it is now

Own JUCE engine, no third-party synth code.

- 16-voice polyphony, plus mono and legato with glide
- 3 oscillators (polyBLEP) with unison, sub and noise; normal, ring, sync and FM modes
- 5 filter models: state-variable, ladder, diode, formant, comb
- Dual envelopes, LFO, velocity routing
- FX: drive, wavefolder, bitcrusher, phaser, flanger, chorus, delay, reverb, compressor,
  DC blocker, limiter
- 2x oversampling on the voices and the nonlinear stages
- 12 archetypes and 18 modifiers behind the roll, with tempo-synced delay and gate

Every roll is a 6-digit seed that reproduces the sound exactly. Rolls are checked for
audibility before you hear them, and won't repeat the last two archetypes.

## Done

- Roll history with undo/redo, save/load favourites, full plugin state
- Seed system: type a seed to recall a sound, sample-accurate
- Archetype rolls, modifiers, no-repeat rule, silent-roll rejection
- Lockable reels (OSC / FILT / ENV / MOD / FX) for keeping part of a sound
- Complete FX rack and oversampling
- Output meter
- Dev panel behind seed 777: every parameter as a live slider
- Black-and-white UI

## Next

**Sound**

- Rarity tiers. Weight rolls common / rare / jackpot and show it on the pull. Ties variety to
  the theme and gives people something worth screenshotting.
- [x] Cross-archetype borrowing. 12% of rolls build a second archetype and transplant one
      of the five HOLD sections from it — a bell's envelope on a bass, an organ's effects on
      a pluck. Weighted away from the oscillators, since borrowing those replaces what the
      sound is made of rather than colouring it.
- [x] Loosen Bass and Lead. They measured 0.387 and 0.430 mean spectral distance against
      0.504 across the whole engine, i.e. two Bass rolls were more alike than two rolls
      picked at random. Now 0.454 and 0.506.
- Chaos intensity. A dial from tame to unhinged instead of a switch.
- [x] Nudge. A slightly different version of the current sound, bought with credits won on
      the reels: 2 for three fruit, 20 for three sevens, 5 to start. Continuous values drift,
      every discrete choice holds, so it stays the same instrument.

**Interface**

- The slot machine. Five reels are already there in the lock groups; they need a face, a
  lever, and symbols. Colour comes at this point.
- Auto-play preview on roll, so a demo video needs no keyboard.

**Before charging for it**

- Host parameters. The plugin currently publishes none, so nothing is automatable or
  MIDI-learnable. Master, chaos and a roll trigger at minimum — automating the roll to fire
  every bar suits the concept.
- macOS build, signing and notarisation. Windows builds on CI already.
- Code signing for Windows, to stop the SmartScreen warning.
- [x] pluginval: passes at strictness 10 (max), runs in CI.
- Preset browser for favourites: name, tag, delete.

**Possible, not decided**

- [x] CPU spikes. Worst case was 46% of one core against 1.2% for the cheapest roll - a 30x
      spread. Measured rather than guessed: the cost is almost entirely per-note (the whole
      effect chain is 0.5%), and wide unison multiplies every note by up to 21 oscillators.
      Fixed by capping polyphony against unison width and by not generating oscillators that
      are turned all the way down. 46% -> 18% at 16 held notes, sound unchanged.
      Auto-bypassing oversampling was the plan here and turned out to be worthless: 95% of
      rolls engage something nonlinear, and of the ones that do not, none are expensive.

## Declined

- Trait slots (archetypes as independent slots plus a compatibility matrix). Measured
  instead of assumed: two rolls inside Pluck (0.544) or Keys (0.521) were already further
  apart than two rolls drawn from the whole engine (0.504). The clustering this was meant
  to break had mostly gone — the modulation overhaul, modifiers, loosen() and wavetables
  did it. And a compatibility matrix is an archetype system in another form: fully
  independent slots mostly produce mush, and the rules you add to stop that are the
  archetypes again, spread across a table instead of readable in one place. What the
  numbers did justify was loosening the two tight archetypes and letting rolls borrow a
  section from each other, both of which are done.

- Record last roll to WAV.
- Factory presets. The rolls are the product.

## Note on saved sounds

Favourites and plugin state store full resolved parameters, not just the seed, so saved
sounds survive changes to the randomizer. The seed is kept as shareable metadata and
regenerates against whatever the current algorithm is.
