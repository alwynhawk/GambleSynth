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
- Trait slots. Replace whole-archetype recipes with independent slots (amp shape, timbre
  source, filter character, motion, space) plus a compatibility matrix. Keys already works
  this way and it was the single biggest variety win so far.
- Chaos intensity. A dial from tame to unhinged instead of a switch.
- Nudge. Roll a slightly different version of the current sound.

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
- A pluginval pass.
- Preset browser for favourites: name, tag, delete.

**Possible, not decided**

- Auto-bypass the oversampling for patches with no nonlinearity engaged. Worst-case CPU is
  46% of one core; a clean patch gains nothing from 2x and could run at half that.

## Declined

- Record last roll to WAV.
- Factory presets. The rolls are the product.

## Note on saved sounds

Favourites and plugin state store full resolved parameters, not just the seed, so saved
sounds survive changes to the randomizer. The seed is kept as shareable metadata and
regenerates against whatever the current algorithm is.
