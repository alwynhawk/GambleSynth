#pragma once
#include <cmath>
#include "Wavetables.h"

// ---- polyBLEP: cheap anti-aliasing correction for saw / square edges ----
inline float polyBLEP (float t, float dt)
{
    if (t < dt)            { t /= dt;            return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt)     { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// ---- One oscillator: holds up to MaxUni detuned phases for supersaw unison,
//      band-limited saw/square, variable pulse width for PWM. ----
struct Oscillator
{
    static constexpr int MaxUni = 7;
    float phase[MaxUni] = { 0.0f };
    bool  wrapped = false;   // true on the sample where phase[0] looped (hard-sync)

    // Randomise all unison phases (fuller sound) or, if rng is null, zero them.
    void reset (juce::Random* rng = nullptr)
    {
        for (auto& p : phase) p = (rng != nullptr) ? rng->nextFloat() : 0.0f;
        wrapped = false;
    }
    void syncReset() { phase[0] = 0.0f; }   // hard-sync: restart primary phase

    // pw = pulse width (0..1), only affects the square wave.
    static float waveAt (float p, float dt, int wave, float pw, juce::Random& rng,
                         const Wavetable* wt = nullptr, float wtPos = 0.0f)
    {
        switch (wave)
        {
            case 0:  return std::sin (p * juce::MathConstants<float>::twoPi);           // sine
            case 1:  return 2.0f * std::abs (2.0f * p - 1.0f) - 1.0f;                    // triangle
            case 2:  return (2.0f * p - 1.0f) - polyBLEP (p, dt);                        // saw
            case 3:                                                                       // square / pulse
            {
                float v = (p < pw ? 1.0f : -1.0f) + polyBLEP (p, dt);
                float t2 = p - pw; if (t2 < 0.0f) t2 += 1.0f;
                return v - polyBLEP (t2, dt);
            }
            case 5:  // wavetable — mip-mapped by dt so high notes stay band-limited
                return wt != nullptr ? wt->sample (p, wtPos, dt)
                                     : std::sin (p * juce::MathConstants<float>::twoPi);
            default: return rng.nextFloat() * 2.0f - 1.0f;                               // noise
        }
    }

    // Single-voice (uses phase[0]). phaseMod shifts read position (FM).
    float next (float freq, double sampleRate, int wave, juce::Random& rng,
                float phaseMod = 0.0f, float pw = 0.5f,
                const Wavetable* wt = nullptr, float wtPos = 0.0f)
    {
        wrapped = false;
        const float dt = (float) (freq / sampleRate);
        float p = phase[0] + phaseMod;
        p -= std::floor (p);
        const float value = waveAt (p, dt, wave, pw, rng, wt, wtPos);
        phase[0] += dt;
        if (phase[0] >= 1.0f) { phase[0] -= std::floor (phase[0]); wrapped = true; }
        return value;
    }

    // Unison: `voices` detuned copies, panned around basePan by `spread`.
    // Accumulates equal-power into L/R (does not overwrite).
    // Chord intervals in semitones. Voice 0 is always the root, so a chord never
    // moves the note you actually played.
    static const int* chordIntervals (int type, int& count)
    {
        static const int fifth[]  = { 0, 7, 12 };
        static const int octave[] = { 0, 12, 24 };
        static const int major[]  = { 0, 4, 7, 12 };
        static const int minor[]  = { 0, 3, 7, 12 };
        static const int sus4[]   = { 0, 5, 7, 12 };

        switch (type)
        {
            case 1: count = 3; return fifth;
            case 2: count = 3; return octave;
            case 3: count = 4; return major;
            case 4: count = 4; return minor;
            case 5: count = 4; return sus4;
            default: count = 0; return nullptr;
        }
    }

    void nextUnison (float freq, double sampleRate, int wave, juce::Random& rng,
                     int voices, float detuneCents, float basePan, float spread,
                     float pw, float& L, float& R,
                     const Wavetable* wt = nullptr, float wtPos = 0.0f,
                     int chordType = 0)
    {
        // A chord replaces the detune spread: the voices become intervals rather
        // than copies, so one key press plays a shape.
        int chordCount = 0;
        const int* chord = chordIntervals (chordType, chordCount);
        if (chord != nullptr)
            voices = juce::jmin (MaxUni, chordCount);

        voices = juce::jlimit (1, MaxUni, voices);
        float outL = 0.0f, outR = 0.0f;
        for (int v = 0; v < voices; ++v)
        {
            const float d = (voices > 1) ? ((float) v / (voices - 1) * 2.0f - 1.0f) : 0.0f;
            const float ratio = (chord != nullptr)
                ? std::exp2 ((float) chord[v] / 12.0f
                             + d * detuneCents / 4800.0f)   // a touch of detune on top
                : std::exp2 (d * detuneCents / 1200.0f);
            const float dt = (float) (freq * ratio / sampleRate);
            const float val = waveAt (phase[v], dt, wave, pw, rng, wt, wtPos);
            phase[v] += dt;
            if (phase[v] >= 1.0f) phase[v] -= std::floor (phase[v]);

            const float pan = juce::jlimit (-1.0f, 1.0f, basePan + d * spread);
            const float a = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            outL += val * std::cos (a);
            outR += val * std::sin (a);
        }
        const float norm = 1.0f / std::sqrt ((float) voices);
        L += outL * norm;
        R += outR * norm;
    }
};

// ---- Noise, in four colours. White is flat; pink falls 3 dB/octave and is the
//      one that sounds like breath or wind; brown falls 6 and rumbles; crackle
//      is sparse impulses, i.e. vinyl. ----
struct NoiseSource
{
    float pink[7] = { 0.0f };
    float brown   = 0.0f;
    float crackle = 0.0f;

    void reset() { for (auto& v : pink) v = 0.0f; brown = 0.0f; crackle = 0.0f; }

    float next (int colour, juce::Random& rng)
    {
        const float w = rng.nextFloat() * 2.0f - 1.0f;

        switch (colour)
        {
            case 1:   // pink — Paul Kellet's economy filter bank
                pink[0] = 0.99886f * pink[0] + w * 0.0555179f;
                pink[1] = 0.99332f * pink[1] + w * 0.0750759f;
                pink[2] = 0.96900f * pink[2] + w * 0.1538520f;
                pink[3] = 0.86650f * pink[3] + w * 0.3104856f;
                pink[4] = 0.55000f * pink[4] + w * 0.5329522f;
                pink[5] = -0.7616f * pink[5] - w * 0.0168980f;
                {
                    const float out = pink[0] + pink[1] + pink[2] + pink[3]
                                    + pink[4] + pink[5] + pink[6] + w * 0.5362f;
                    pink[6] = w * 0.115926f;
                    return out * 0.11f;
                }

            case 2:   // brown — integrated white, leaked so it cannot wander off
                brown = juce::jlimit (-1.0f, 1.0f, brown * 0.997f + w * 0.035f);
                return brown * 3.2f;

            case 3:   // crackle — sparse impulses that ring down briefly
                crackle *= 0.72f;
                if (rng.nextFloat() < 0.0016f)
                    crackle = (rng.nextFloat() * 2.0f - 1.0f);
                return crackle;

            default:  return w;   // white
        }
    }
};

// ---- Karplus-Strong: a noise burst circulating in a tuned delay, losing its
//      high end each lap. That is a plucked string — and it is a different
//      *kind* of sound from anything subtractive, because the tone comes from
//      the excitation decaying rather than from a filter sweeping. ----
struct PluckedString
{
    static constexpr int MaxDelay = 4096;
    float buf[MaxDelay] = { 0.0f };
    int   w = 0;
    float delay = 100.0f;
    float lp = 0.0f;          // one-pole damping inside the loop
    float excite = 0.0f;      // samples of burst left to inject
    bool  active = false;

    void reset()
    {
        for (auto& v : buf) v = 0.0f;
        w = 0; lp = 0.0f; excite = 0.0f; active = false;
    }

    // Called on note-on: fill the loop with a burst as long as one period.
    void pluck (float freq, double sampleRate, float brightness, juce::Random& rng)
    {
        delay = juce::jlimit (2.0f, (float) (MaxDelay - 2),
                              (float) sampleRate / juce::jmax (20.0f, freq));

        // The read tap sits `delay` samples *behind* the write head, so the
        // burst has to be written there — writing ahead of it means the loop
        // reads silence for a full period and the string never starts.
        const int n = (int) delay;
        for (int i = 0; i < n; ++i)
        {
            // A darker pluck starts with a smoother burst, like a soft pick.
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            lp += (white - lp) * juce::jlimit (0.05f, 1.0f, brightness);

            int idx = w - n + i;
            while (idx < 0) idx += MaxDelay;
            buf[idx % MaxDelay] = lp;
        }
        lp = 0.0f;
        active = true;
    }

    // damping 0..1: how fast the high end dies, i.e. string material.
    // decay 0..1: how long the fundamental rings.
    float next (float damping, float decay)
    {
        if (! active) return 0.0f;

        float rp = (float) w - delay;
        while (rp < 0.0f) rp += (float) MaxDelay;
        const int i0 = (int) rp;
        const int i1 = (i0 + 1) % MaxDelay;
        const float fr = rp - (float) i0;
        const float v = buf[i0] + (buf[i1] - buf[i0]) * fr;

        // Damping is the whole character: heavy = nylon, light = steel.
        lp += (v - lp) * juce::jlimit (0.02f, 0.95f, 1.0f - damping);

        const float fb = juce::jlimit (0.80f, 0.9995f, 0.95f + decay * 0.049f);
        buf[w] = lp * fb;
        if (++w >= MaxDelay) w = 0;

        return v;
    }
};

// ---- One modulator. Per-voice, so every note carries its own phase and its
//      own random stream — sixteen voices moving in lockstep sounds mechanical.
//      Always returns bipolar -1..1; the destination decides what that means. ----
struct ModOsc
{
    float phase  = 0.0f;
    float held   = 0.0f;    // sample & hold: the value being held
    float from   = 0.0f;    // random walk: where this segment started
    float to     = 0.0f;    // random walk: where it is heading
    juce::Random rng;

    void reset (juce::uint32 seed, float startPhase)
    {
        rng.setSeed ((juce::int64) seed + 1);
        phase = startPhase - std::floor (startPhase);
        held  = rng.nextFloat() * 2.0f - 1.0f;
        from  = held;
        to    = rng.nextFloat() * 2.0f - 1.0f;
    }

    // inc = cycles per sample.
    float next (float inc, int shape)
    {
        phase += inc;
        const bool wrapped = phase >= 1.0f;
        if (wrapped)
        {
            phase -= std::floor (phase);
            held = rng.nextFloat() * 2.0f - 1.0f;   // new step for S&H
            from = to;
            to   = rng.nextFloat() * 2.0f - 1.0f;   // new leg for the walk
        }

        switch (shape)
        {
            case 1:  return 1.0f - 4.0f * std::abs (std::fmod (phase + 0.25f, 1.0f) - 0.5f); // tri
            case 2:  return phase < 0.5f ? 1.0f : -1.0f;                                     // square
            case 3:  return 2.0f * phase - 1.0f;                                             // ramp up
            case 4:  return 1.0f - 2.0f * phase;                                             // ramp down
            case 5:  return held;                                                            // sample & hold
            case 6:                                                                          // random walk
            {
                // Smoothstep between the two random endpoints: drifting, never stepped.
                const float t = phase * phase * (3.0f - 2.0f * phase);
                return from + (to - from) * t;
            }
            default: return std::sin (phase * juce::MathConstants<float>::twoPi);            // sine
        }
    }
};

// ---- Fractional read from a delay line (shared by chorus / flanger) ----
inline float readFracDelay (const std::vector<float>& b, float delaySamples, int w)
{
    const int n = (int) b.size();
    float rp = (float) w - delaySamples;
    while (rp < 0.0f) rp += (float) n;
    const int i0 = (int) rp;
    const float fr = rp - (float) i0;
    const int i1 = (i0 + 1) % n;
    return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
}

// ---- Stereo chorus / ensemble: two modulated delay lines, L/R offset ----
struct Chorus
{
    std::vector<float> bufL, bufR;
    int    w     = 0;
    double sr    = 44100.0;
    float  phase = 0.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int n = (int) (sr * 0.05) + 4;   // 50 ms max
        bufL.assign ((size_t) n, 0.0f);
        bufR.assign ((size_t) n, 0.0f);
        w = 0; phase = 0.0f;
    }

    // Drop the tail without reallocating (safe on the audio thread).
    void clear()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
    }

    // baseMs ~ 12-18, depthMs ~ 3-6, rate ~ 0.3-0.9 Hz
    void process (float* L, float* R, int num, float rate, float depthMs, float baseMs, float mix)
    {
        if (bufL.empty() || mix <= 0.001f) return;
        const float inc = rate / (float) sr;
        const float d2s = 0.001f * (float) sr;

        for (int i = 0; i < num; ++i)
        {
            const float lfoL = std::sin (phase * juce::MathConstants<float>::twoPi);
            const float lfoR = std::sin ((phase + 0.25f) * juce::MathConstants<float>::twoPi);
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;

            const float dL = (baseMs + depthMs * (0.5f + 0.5f * lfoL)) * d2s;
            const float dR = (baseMs + depthMs * (0.5f + 0.5f * lfoR)) * d2s;

            const float wetL = readFracDelay (bufL, dL, w);
            const float wetR = readFracDelay (bufR, dR, w);

            bufL[(size_t) w] = L[i];
            bufR[(size_t) w] = R[i];
            if (++w >= (int) bufL.size()) w = 0;

            L[i] = L[i] * (1.0f - mix * 0.5f) + wetL * mix;
            R[i] = R[i] * (1.0f - mix * 0.5f) + wetR * mix;
        }
    }
};

// ---- 2x decimator: windowed-sinc low-pass at the base-rate Nyquist, then keep
//      every second sample. The voices and the nonlinear stages run at twice the
//      host rate; this is what stops the harmonics they create above Nyquist
//      from folding back down as inharmonic buzz. ----
struct Decimator2x
{
    static constexpr int Taps = 63;
    float h[Taps]  = { 0.0f };
    float zL[Taps] = { 0.0f }, zR[Taps] = { 0.0f };
    int   w = 0;

    // Latency this adds, expressed in base-rate samples.
    static constexpr int latencySamples() { return (Taps - 1) / 4; }

    void prepare()
    {
        const int   M  = Taps - 1;
        const double fc = 0.25;   // = base-rate Nyquist, normalised to the 2x rate
        double sum = 0.0;

        for (int n = 0; n < Taps; ++n)
        {
            const double k = n - M / 2.0;
            const double sinc = (std::abs (k) < 1.0e-9)
                              ? 2.0 * fc
                              : std::sin (2.0 * juce::MathConstants<double>::pi * fc * k)
                                / (juce::MathConstants<double>::pi * k);
            const double blackman = 0.42
                                  - 0.5  * std::cos (2.0 * juce::MathConstants<double>::pi * n / M)
                                  + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * n / M);
            h[n] = (float) (sinc * blackman);
            sum += h[n];
        }
        for (auto& v : h) v = (float) (v / sum);   // unity gain at DC

        reset();
    }

    void reset()
    {
        for (int n = 0; n < Taps; ++n) { zL[n] = 0.0f; zR[n] = 0.0f; }
        w = 0;
    }

    // src holds 2*numOut samples per channel; dst gets numOut.
    void process (const float* srcL, const float* srcR, float* dstL, float* dstR, int numOut)
    {
        for (int n = 0; n < numOut; ++n)
        {
            for (int half = 0; half < 2; ++half)
            {
                zL[w] = srcL[n * 2 + half];
                zR[w] = srcR[n * 2 + half];
                if (++w >= Taps) w = 0;
            }

            float accL = 0.0f, accR = 0.0f;
            int idx = w;
            for (int t = Taps - 1; t >= 0; --t)
            {
                accL += h[t] * zL[idx];
                accR += h[t] * zR[idx];
                if (++idx >= Taps) idx = 0;
            }
            dstL[n] = accL;
            dstR[n] = accR;
        }
    }
};

// ---- Wavefolder: past full scale the signal folds back instead of clipping,
//      adding harmonics that distortion can't. Stateless. ----
inline float waveFold (float x, float amount)
{
    if (amount <= 0.001f) return x;
    const float g = 1.0f + amount * 5.0f;
    // Folding packs in harmonics and raises average energy, so trim harder
    // than a peak-based normalisation would suggest.
    return std::sin (x * g * juce::MathConstants<float>::halfPi) / (1.0f + amount * 2.2f);
}

// ---- Bitcrusher + sample-rate reducer: lo-fi grit, the gnarliest chaos fuel ----
struct Crusher
{
    float holdL = 0.0f, holdR = 0.0f, counter = 0.0f;

    void reset() { holdL = holdR = 0.0f; counter = 0.0f; }

    // bits: 16 = off, down to 2. rateDiv: 1 = off, up to ~32 (hold every Nth sample).
    void process (float* L, float* R, int num, float bits, float rateDiv)
    {
        const bool crushBits = bits < 15.9f;
        const bool crushRate = rateDiv > 1.01f;
        if (! crushBits && ! crushRate) return;

        const float levels = std::pow (2.0f, juce::jlimit (1.0f, 16.0f, bits)) - 1.0f;

        for (int i = 0; i < num; ++i)
        {
            counter += 1.0f;
            if (counter >= rateDiv) { counter -= rateDiv; holdL = L[i]; holdR = R[i]; }

            float l = crushRate ? holdL : L[i];
            float r = crushRate ? holdR : R[i];
            if (crushBits)
            {
                l = std::round (l * levels) / levels;
                r = std::round (r * levels) / levels;
            }
            L[i] = l; R[i] = r;
        }
    }
};

// ---- Phaser: six modulated all-pass stages with feedback. Cheap retro sweep ----
struct Phaser
{
    static constexpr int Stages = 6;
    float zL[Stages] = { 0.0f }, zR[Stages] = { 0.0f };
    float fbL = 0.0f, fbR = 0.0f;
    float phase = 0.0f;
    double sr = 44100.0;

    void prepare (double sampleRate) { sr = sampleRate; reset(); }

    void reset()
    {
        for (int s = 0; s < Stages; ++s) { zL[s] = 0.0f; zR[s] = 0.0f; }
        fbL = fbR = 0.0f; phase = 0.0f;
    }

    // One-multiply first-order all-pass.
    static float allpass (float x, float& z, float a)
    {
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }

    void process (float* L, float* R, int num, float rate, float depth, float fb, float mix)
    {
        if (mix <= 0.001f) return;
        const float inc = rate / (float) sr;
        const float feedback = juce::jlimit (0.0f, 0.85f, fb);

        for (int i = 0; i < num; ++i)
        {
            const float lfoL = 0.5f + 0.5f * std::sin (phase * juce::MathConstants<float>::twoPi);
            const float lfoR = 0.5f + 0.5f * std::sin ((phase + 0.25f) * juce::MathConstants<float>::twoPi);
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;

            auto coefFor = [this, depth] (float lfo)
            {
                const float fmin = 200.0f;
                const float fmax = 200.0f + 3000.0f * juce::jlimit (0.0f, 1.0f, depth);
                const float f = fmin * std::pow (fmax / fmin, lfo);
                const float t = std::tan (juce::MathConstants<float>::pi * f / (float) sr);
                return (t - 1.0f) / (t + 1.0f);
            };

            const float aL = coefFor (lfoL), aR = coefFor (lfoR);

            float yL = L[i] + fbL * feedback;
            float yR = R[i] + fbR * feedback;
            for (int s = 0; s < Stages; ++s)
            {
                yL = allpass (yL, zL[s], aL);
                yR = allpass (yR, zR[s], aR);
            }
            fbL = juce::jlimit (-3.0f, 3.0f, yL);
            fbR = juce::jlimit (-3.0f, 3.0f, yR);

            L[i] = L[i] * (1.0f - mix * 0.5f) + yL * mix;
            R[i] = R[i] * (1.0f - mix * 0.5f) + yR * mix;
        }
    }
};

// ---- Flanger: short modulated delay with feedback — jet-whoosh, sharper and
//      more metallic than the chorus (which is longer and feedback-free). ----
struct Flanger
{
    std::vector<float> bufL, bufR;
    int    w     = 0;
    double sr    = 44100.0;
    float  phase = 0.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        bufL.assign ((size_t) (sr * 0.03) + 4, 0.0f);   // 30 ms
        bufR.assign (bufL.size(), 0.0f);
        w = 0; phase = 0.0f;
    }

    void clear()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        phase = 0.0f;
    }

    void process (float* L, float* R, int num, float rate, float depth, float fb, float mix)
    {
        if (bufL.empty() || mix <= 0.001f) return;
        const float inc = rate / (float) sr;
        const float d2s = 0.001f * (float) sr;
        const float feedback = juce::jlimit (0.0f, 0.9f, fb);

        for (int i = 0; i < num; ++i)
        {
            const float lfoL = 0.5f + 0.5f * std::sin (phase * juce::MathConstants<float>::twoPi);
            const float lfoR = 0.5f + 0.5f * std::sin ((phase + 0.3f) * juce::MathConstants<float>::twoPi);
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;

            const float dL = (0.6f + 6.0f * depth * lfoL) * d2s;
            const float dR = (0.6f + 6.0f * depth * lfoR) * d2s;

            const float wetL = readFracDelay (bufL, dL, w);
            const float wetR = readFracDelay (bufR, dR, w);

            bufL[(size_t) w] = juce::jlimit (-3.0f, 3.0f, L[i] + wetL * feedback);
            bufR[(size_t) w] = juce::jlimit (-3.0f, 3.0f, R[i] + wetR * feedback);
            if (++w >= (int) bufL.size()) w = 0;

            L[i] = L[i] * (1.0f - mix * 0.5f) + wetL * mix;
            R[i] = R[i] * (1.0f - mix * 0.5f) + wetR * mix;
        }
    }
};

// ---- Compressor: stereo-linked peak compressor on one macro control.
//      Glues a patch together where the tanh limiter only squashes it. ----
struct Compressor
{
    float env = 0.0f;
    double sr = 44100.0;

    void prepare (double sampleRate) { sr = sampleRate; env = 0.0f; }
    void reset() { env = 0.0f; }

    void process (float* L, float* R, int num, float amount)
    {
        if (amount <= 0.001f) return;

        amount = juce::jlimit (0.0f, 1.0f, amount);
        const float thresh = std::pow (10.0f, (-2.0f - 20.0f * amount) / 20.0f);
        const float ratio  = 2.0f + 6.0f * amount;
        const float atk    = (float) std::exp (-1.0 / (0.004 * sr));
        const float rel    = (float) std::exp (-1.0 / (0.12  * sr));

        // Only part of the theoretical make-up, so heavy settings glue instead
        // of just turning everything up into the limiter.
        const float makeup = juce::jmin (4.0f, std::pow (1.0f / thresh, (1.0f - 1.0f / ratio) * 0.7f));

        for (int i = 0; i < num; ++i)
        {
            const float in = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            const float coef = (in > env) ? atk : rel;
            env = in + coef * (env - in);

            float gain = 1.0f;
            if (env > thresh)
                gain = std::pow (env / thresh, 1.0f / ratio - 1.0f);

            L[i] *= gain * makeup;
            R[i] *= gain * makeup;
        }
    }
};

// ---- DC blocker: ring-mod, folding and hard sync can all leave an offset,
//      which eats headroom and thumps on note changes. ----
struct DCBlocker
{
    float x1[2] = { 0.0f, 0.0f }, y1[2] = { 0.0f, 0.0f };

    void reset() { x1[0] = x1[1] = y1[0] = y1[1] = 0.0f; }

    void process (float* L, float* R, int num)
    {
        constexpr float r = 0.9985f;
        for (int i = 0; i < num; ++i)
        {
            const float yl = L[i] - x1[0] + r * y1[0];
            x1[0] = L[i]; y1[0] = yl; L[i] = yl;

            const float yr = R[i] - x1[1] + r * y1[1];
            x1[1] = R[i]; y1[1] = yr; R[i] = yr;
        }
    }
};

// ---- TPT State-Variable Filter (Cytomic / Zavalishin), LP/BP/HP ----
struct SVF
{
    float ic1eq = 0.0f, ic2eq = 0.0f;
    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

    void reset() { ic1eq = ic2eq = 0.0f; }

    void set (float cutoffHz, float resonance, double sampleRate)
    {
        cutoffHz = juce::jlimit (20.0f, (float) (sampleRate * 0.45), cutoffHz);
        g  = std::tan (juce::MathConstants<float>::pi * cutoffHz / (float) sampleRate);
        k  = 2.0f - 1.98f * juce::jlimit (0.0f, 1.0f, resonance);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // type: 0 LP, 1 BP, 2 HP
    float process (float v0, int type)
    {
        const float v3 = v0 - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        switch (type)
        {
            case 1:  return v1;                 // band
            case 2:  return v0 - k * v1 - v2;   // high
            default: return v2;                 // low
        }
    }
};

// ---- Ladder: 4 cascaded TPT one-poles with saturated resonant feedback.
//      diode=false → Moog-ish creamy; diode=true → 303-ish squelch (hotter
//      feedback, asymmetric clip, tap mixed from stage 2). ----
struct Ladder
{
    float s[4]  = { 0.0f };
    float fbz   = 0.0f;      // one-sample-delayed output (feedback path)
    float g     = 0.5f;
    float k     = 0.0f;
    bool  diode = false;

    void reset() { for (auto& v : s) v = 0.0f; fbz = 0.0f; }

    void set (float cutoffHz, float resonance, double sampleRate, bool diodeMode)
    {
        cutoffHz = juce::jlimit (20.0f, (float) (sampleRate * 0.45), cutoffHz);
        const float wc = std::tan (juce::MathConstants<float>::pi * cutoffHz / (float) sampleRate);
        g = wc / (1.0f + wc);
        diode = diodeMode;
        // 4.0 is the self-oscillation point; stay just under so it stays musical.
        k = juce::jlimit (0.0f, 1.0f, resonance) * (diodeMode ? 4.4f : 3.8f);
    }

    float onePole (int idx, float x)
    {
        const float v = (x - s[idx]) * g;
        const float y = v + s[idx];
        s[idx] = y + v;
        return y;
    }

    // type: 0 LP, 1 BP, 2 HP. poles: 2 or 4.
    // All four stages always run so the band-pass has a shallower tap to
    // subtract from — taking the difference of two *identical* taps was silence.
    float process (float x, int type, int poles)
    {
        const float fb = diode ? std::tanh (fbz * 1.6f) : fbz;
        const float u  = std::tanh (x * (1.0f + k * 0.4f) - k * fb);  // drive + resonance

        const float y1 = onePole (0, u);
        const float y2 = onePole (1, y1);
        const float y3 = onePole (2, y2);
        const float y4 = onePole (3, y3);

        const float lp      = (poles >= 4) ? y4 : y2;   // the selected slope
        const float shallow = (poles >= 4) ? y2 : y1;   // one stage back, for the band
        fbz = lp;

        // Diode ladders are brighter and less steep: mix the shallower tap in.
        const float lpOut = diode ? (lp * 0.6f + shallow * 0.4f) * 0.8f : lp;

        switch (type)
        {
            case 1:  return shallow - lp;    // band = shallow slope minus steep
            case 2:  return x - lpOut;       // high
            default: return lpOut;
        }
    }
};

// ---- Formant: three parallel band-passes on a vowel table. Cutoff shifts the
//      whole set (so the filter envelope/LFO still sweeps it). ----
struct Formant
{
    SVF band[3];

    void reset() { for (auto& b : band) b.reset(); }

    // vowel 0..1 selects a..e..i..o..u
    void set (float cutoffHz, float resonance, float vowel, double sampleRate)
    {
        static const float freq[5][3] = {
            { 800.f, 1150.f, 2900.f },   // a
            { 400.f, 1600.f, 2700.f },   // e
            { 350.f, 1700.f, 2700.f },   // i
            { 450.f,  800.f, 2830.f },   // o
            { 325.f,  700.f, 2530.f }    // u
        };
        const float pos = juce::jlimit (0.0f, 3.999f, vowel * 4.0f);
        const int   v0  = (int) pos;
        const float mix = pos - (float) v0;

        const float shift = juce::jlimit (0.25f, 4.0f, cutoffHz / 900.0f);
        const float q = 0.55f + 0.35f * juce::jlimit (0.0f, 1.0f, resonance);

        for (int i = 0; i < 3; ++i)
        {
            const float fHz = (freq[v0][i] + (freq[v0 + 1][i] - freq[v0][i]) * mix) * shift;
            band[i].set (fHz, q, sampleRate);
        }
    }

    float process (float x)
    {
        static const float gain[3] = { 1.0f, 0.55f, 0.28f };
        float out = 0.0f;
        for (int i = 0; i < 3; ++i)
            out += band[i].process (x, 1) * gain[i];
        return out * 1.8f;   // band-passes lose level; bring it back
    }
};

// ---- Comb: tuned feedback delay + damping. Metallic/plucked resonance;
//      negative feedback gives the hollow "square" flavour. ----
struct Comb
{
    static constexpr int Size = 4096;
    float buf[Size] = { 0.0f };
    int   w    = 0;
    float dly  = 100.0f;
    float fb   = 0.5f;
    float damp = 0.3f;
    float lp   = 0.0f;

    void reset() { for (auto& v : buf) v = 0.0f; w = 0; lp = 0.0f; }

    void set (float cutoffHz, float resonance, float tone, double sampleRate)
    {
        // The comb is tuned by cutoff; below ~60 Hz the delay outgrows the buffer.
        const float hz = juce::jlimit (60.0f, (float) (sampleRate * 0.45), cutoffHz);
        dly  = juce::jlimit (2.0f, (float) (Size - 2), (float) sampleRate / hz);
        fb   = juce::jlimit (0.0f, 0.95f, 0.55f + resonance * 0.4f);
        damp = juce::jlimit (0.05f, 0.9f, 1.0f - tone);
    }

    // type 2 (HP) flips the feedback sign → hollow, odd-harmonic comb.
    float process (float x, int type)
    {
        float rp = (float) w - dly;
        while (rp < 0.0f) rp += (float) Size;
        const int   i0 = (int) rp;
        const float fr = rp - (float) i0;
        const int   i1 = (i0 + 1) % Size;
        const float d  = buf[i0] + (buf[i1] - buf[i0]) * fr;

        lp += (d - lp) * (1.0f - damp);          // damping in the loop
        const float sign = (type == 2) ? -1.0f : 1.0f;
        float v = x + sign * lp * fb;
        v = juce::jlimit (-4.0f, 4.0f, v);       // loop can't run away

        buf[w] = v;
        if (++w >= Size) w = 0;
        return v * 0.7f;
    }
};

// ---- One filter slot: picks a model per patch. Keeps every model's state so a
//      roll can switch flavour without clicks. ----
struct MultiFilter
{
    enum Model { Svf = 0, LadderM, DiodeM, FormantM, CombM, NumModels };

    SVF     svf1, svf2;      // svf2 = second stage for the 4-pole cascade
    Ladder  ladder;
    Formant formant;
    Comb    comb;

    int model = Svf, poles = 2;

    void reset()
    {
        svf1.reset(); svf2.reset(); ladder.reset(); formant.reset(); comb.reset();
    }

    void set (int m, int p, float cutoffHz, float resonance, float morph, double sampleRate)
    {
        model = juce::jlimit (0, (int) NumModels - 1, m);
        poles = (p >= 4) ? 4 : 2;

        switch (model)
        {
            case LadderM:  ladder.set (cutoffHz, resonance, sampleRate, false); break;
            case DiodeM:   ladder.set (cutoffHz, resonance, sampleRate, true);  break;
            case FormantM: formant.set (cutoffHz, resonance, morph, sampleRate); break;
            case CombM:    comb.set (cutoffHz, resonance, morph, sampleRate);    break;
            default:
                svf1.set (cutoffHz, resonance, sampleRate);
                // Second stage runs flat-ish so the cascade doesn't double the peak.
                if (poles >= 4) svf2.set (cutoffHz, resonance * 0.4f, sampleRate);
                break;
        }
    }

    float process (float x, int type)
    {
        switch (model)
        {
            case LadderM:
            case DiodeM:   return ladder.process (x, type, poles);
            case FormantM: return formant.process (x);
            case CombM:    return comb.process (x, type);
            default:
            {
                const float y = svf1.process (x, type);
                return (poles >= 4) ? svf2.process (y, type) : y;
            }
        }
    }
};
