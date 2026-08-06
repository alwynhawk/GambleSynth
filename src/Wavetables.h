#pragma once
#include <array>
#include <cmath>
#include <vector>

// Six wavetables, generated at startup from harmonic recipes — no sample data
// shipped, everything stays own-code.
//
// Each table is NumFrames single-cycle waveforms and the position control
// morphs through them. Every frame is stored at NumMips band-limited levels,
// one per octave: level m keeps harmonics up to MaxHarmonics >> m, and the
// oscillator picks the level whose top harmonic stays under Nyquist for the
// note being played. This is most of the point — a naive wavetable aliases
// badly enough on high notes to sound worse than the plain waves.
struct Wavetable
{
    static constexpr int FrameSize    = 1024;
    static constexpr int NumFrames    = 16;
    static constexpr int NumMips      = 8;
    static constexpr int MaxHarmonics = 256;

    std::vector<float> data;   // [mip][frame][sample], flattened

    const float* frame (int mip, int f) const
    {
        return data.data() + ((size_t) mip * NumFrames + (size_t) f) * FrameSize;
    }

    // phase 0..1, pos 0..1, dt = freq / sampleRate (decides the mip).
    float sample (float phase, float pos, float dt) const
    {
        // Harmonic h lands at h*dt cycles/sample and must stay under 0.5. The
        // margin matters: interpolating between two frames of different mips is
        // not exact, so a level that only just fits still leaks. `glass` has
        // sparse high partials and showed this first.
        const float allowed = 0.40f / (dt > 1.0e-6f ? dt : 1.0e-6f);
        int mip = 0;
        while (mip < NumMips - 1 && (float) (MaxHarmonics >> mip) > allowed)
            ++mip;

        pos = pos < 0.0f ? 0.0f : (pos > 1.0f ? 1.0f : pos);
        const float fpos = pos * (float) (NumFrames - 1);
        const int   f0   = (int) fpos;
        const int   f1   = f0 + 1 < NumFrames ? f0 + 1 : f0;
        const float ff   = fpos - (float) f0;

        return read (mip, f0, phase) * (1.0f - ff) + read (mip, f1, phase) * ff;
    }

    float read (int mip, int f, float phase) const
    {
        const float sp = (phase - std::floor (phase)) * (float) FrameSize;
        const int   i0 = (int) sp & (FrameSize - 1);
        const int   i1 = (i0 + 1) & (FrameSize - 1);
        const float fr = sp - std::floor (sp);
        const float* d = frame (mip, f);
        return d[i0] + (d[i1] - d[i0]) * fr;
    }
};

class Wavetables
{
public:
    static constexpr int NumTables = 6;

    // Built on first touch. The processor constructor touches it from the
    // message thread, so the audio thread never pays for the build.
    static const Wavetables& get()
    {
        static Wavetables instance;
        return instance;
    }

    const Wavetable& table (int index) const
    {
        index = index < 0 ? 0 : (index >= NumTables ? NumTables - 1 : index);
        return tables[(size_t) index];
    }

    static const char* name (int index)
    {
        switch (index)
        {
            case 0:  return "morph";    // sine -> saw -> square -> thin pulse
            case 1:  return "sweep";    // saw with a resonant peak climbing it
            case 2:  return "vowel";    // two formants trading places
            case 3:  return "hollow";   // odd harmonics, dull to buzzy
            case 4:  return "glass";    // sparse partials, emphasis rising
            default: return "grit";     // full spectrum through a moving comb
        }
    }

private:
    // A cheap stable hash onto 0..1, for recipe phases. Fixed per harmonic so
    // every mip of a frame agrees about it.
    static float hash01 (int n)
    {
        const float x = std::sin ((float) n * 12.9898f) * 43758.5453f;
        return x - std::floor (x);
    }

    static float gauss (float h, float centre, float width)
    {
        const float d = (h - centre) / width;
        return std::exp (-0.5f * d * d);
    }

    // Harmonic amplitude for table `t`, morph position `x` (0..1), harmonic h.
    static float ampFor (int t, float x, int h)
    {
        const float fh = (float) h;
        float a = 0.0f;

        switch (t)
        {
            case 0:   // morph: sine -> saw -> square -> thin pulse, in thirds
            {
                const float sine   = (h == 1) ? 1.0f : 0.0f;
                const float saw    = 1.0f / fh;
                const float square = (h & 1) ? 1.0f / fh : 0.0f;
                const float pulse  = std::abs (std::sin (3.14159265f * fh * 0.15f)) / fh;

                if      (x < 1.0f / 3) { const float b = x * 3.0f;              a = sine   * (1 - b) + saw   * b; }
                else if (x < 2.0f / 3) { const float b = (x - 1.0f / 3) * 3.0f; a = saw    * (1 - b) + square * b; }
                else                   { const float b = (x - 2.0f / 3) * 3.0f; a = square * (1 - b) + pulse * b; }
                break;
            }

            case 1:   // sweep: saw plus a resonant bump riding up the harmonics
                a = 1.0f / fh
                  + 3.0f * gauss (fh, 1.0f + x * 39.0f, 2.5f) / std::sqrt (fh);
                break;

            case 2:   // vowel: two formants moving against each other
            {
                const float base = 1.0f / std::pow (fh, 1.2f);
                a = base * (0.25f + 2.2f * gauss (fh, 2.0f + 5.0f * x, 1.6f)
                                  + 1.4f * gauss (fh, 14.0f - 8.0f * x, 2.5f));
                break;
            }

            case 3:   // hollow: odd harmonics only, spectrum tilting brighter
                a = (h & 1) ? 1.0f / std::pow (fh, 2.2f - 1.5f * x) : 0.0f;
                break;

            case 4:   // glass: a sparse partial set, emphasis climbing it
            {
                // The set stays low: a table whose energy lives at harmonic 29
                // simply vanishes above the top octave, because the mip that
                // keeps a high note clean has to discard it. Spread the emphasis
                // over partials that survive everywhere, and keep a real
                // fundamental under it so the tone thins rather than dies.
                static constexpr int set[6] = { 1, 2, 3, 5, 7, 9 };
                for (int j = 0; j < 6; ++j)
                    if (set[j] == h)
                        a = std::exp (-0.7f * std::abs ((float) j - 5.0f * x))
                          + (h == 1 ? 0.45f : 0.0f);
                break;
            }

            default:  // grit: everything, through a comb that x drags along
                a = (0.5f + 0.5f * std::sin (fh * (0.4f + 2.6f * x) + 1.7f))
                  / std::pow (fh, 0.75f);
                break;
        }

        // Every frame keeps a solid fundamental, so no mip of any table can be
        // near-silent and the audibility probe never has to care.
        if (h == 1 && a < 0.25f)
            a = 0.25f;

        return a;
    }

    Wavetables()
    {
        // Exact harmonic sampling: with the sine table the same length as a
        // frame, sin(2*pi*h*s/N) is sineTab[(h*s) & (N-1)] with no drift.
        std::array<float, Wavetable::FrameSize> sineTab;
        for (int i = 0; i < Wavetable::FrameSize; ++i)
            sineTab[(size_t) i] = std::sin (2.0f * 3.14159265358979f * (float) i
                                            / (float) Wavetable::FrameSize);

        for (int t = 0; t < NumTables; ++t)
        {
            auto& wt = tables[(size_t) t];
            wt.data.assign ((size_t) Wavetable::NumMips * Wavetable::NumFrames
                                * Wavetable::FrameSize, 0.0f);

            for (int f = 0; f < Wavetable::NumFrames; ++f)
            {
                const float x = (float) f / (float) (Wavetable::NumFrames - 1);

                // The recipe once per frame; every mip is a truncation of it.
                std::array<float, Wavetable::MaxHarmonics + 1> amp {}, ph {};
                for (int h = 1; h <= Wavetable::MaxHarmonics; ++h)
                {
                    amp[(size_t) h] = ampFor (t, x, h);
                    ph[(size_t) h]  = (t == 0 || t == 3) ? 0.0f : hash01 (h + t * 977);
                }

                for (int m = 0; m < Wavetable::NumMips; ++m)
                {
                    const int maxH = Wavetable::MaxHarmonics >> m;
                    float* out = wt.data.data()
                               + ((size_t) m * Wavetable::NumFrames + (size_t) f)
                                     * Wavetable::FrameSize;

                    for (int h = 1; h <= maxH; ++h)
                    {
                        const float a = amp[(size_t) h];
                        if (a < 1.0e-4f)
                            continue;

                        const int phIdx = (int) (ph[(size_t) h] * Wavetable::FrameSize);
                        for (int s = 0; s < Wavetable::FrameSize; ++s)
                            out[s] += a * sineTab[(size_t) ((h * s + phIdx)
                                                            & (Wavetable::FrameSize - 1))];
                    }
                }

                // One scale for the whole frame, taken from the fullest mip, so
                // crossing mips as pitch moves never changes loudness.
                const float* full = wt.frame (0, f);
                float peak = 1.0e-6f;
                for (int s = 0; s < Wavetable::FrameSize; ++s)
                    peak = std::max (peak, std::abs (full[s]));

                const float scale = 1.0f / peak;
                for (int m = 0; m < Wavetable::NumMips; ++m)
                {
                    float* out = wt.data.data()
                               + ((size_t) m * Wavetable::NumFrames + (size_t) f)
                                     * Wavetable::FrameSize;
                    for (int s = 0; s < Wavetable::FrameSize; ++s)
                        out[s] *= scale;
                }
            }
        }
    }

    std::array<Wavetable, NumTables> tables;
};
