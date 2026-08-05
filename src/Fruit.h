#pragma once
#include <JuceHeader.h>

// The back layer of the three windows: a classic slot symbol.
//
// This is deliberately a *lottery* and says nothing about the sound. The front
// layer — the drawn waveform / filter / effect glyph — is what reports what the
// patch actually is. Keeping the two apart means the odds are set exactly here
// instead of emerging from whatever the randomizer happens to favour, and it
// means changing the engine can never make a symbol unreachable.
//
// (An earlier attempt derived the fruit from the sound. Measuring 6000 rolls
// showed one symbol landing 41% of the time on one reel and another 1.5% — the
// mapping needed rebalancing every time the engine changed. Hence this.)
enum class Fruit { Cherry = 0, Lemon, Orange, Apple, Grape, Seven, NumFruits };

inline const char* fruitName (Fruit f)
{
    switch (f)
    {
        case Fruit::Cherry: return "cherry";
        case Fruit::Lemon:  return "lemon";
        case Fruit::Orange: return "orange";
        case Fruit::Apple:  return "apple";
        case Fruit::Grape:  return "grape";
        default:            return "seven";
    }
}

struct FruitSpin
{
    Fruit symbol[3] { Fruit::Cherry, Fruit::Lemon, Fruit::Orange };

    // 3 = jackpot, 2 = near miss, 1 = nothing.
    int matches() const
    {
        if (symbol[0] == symbol[1] && symbol[1] == symbol[2]) return 3;
        if (symbol[0] == symbol[1] || symbol[1] == symbol[2]
                                   || symbol[0] == symbol[2]) return 2;
        return 1;
    }

    bool isJackpot()  const { return matches() == 3; }
    bool isNearMiss() const { return matches() == 2; }
};

// Constructs the outcome first, then picks symbols to fit it — which is what
// makes the odds exact rather than approximate. Picking three symbols freely
// would give a near miss on ~42% of pulls, far too often to feel like anything.
class FruitLottery
{
public:
    static constexpr float JackpotChance  = 0.01f;   // 1 in 100
    static constexpr float NearMissChance = 0.25f;

    FruitSpin spin()
    {
        FruitSpin s;
        const int n = (int) Fruit::NumFruits;
        const float r = rng.nextFloat();

        if (r < JackpotChance)
        {
            const auto f = (Fruit) rng.nextInt (n);
            s.symbol[0] = s.symbol[1] = s.symbol[2] = f;
        }
        else if (r < JackpotChance + NearMissChance)
        {
            const int pair = rng.nextInt (n);
            int odd = rng.nextInt (n - 1);
            if (odd >= pair) ++odd;                    // anything but the pair

            const int oddPos = rng.nextInt (3);
            for (int k = 0; k < 3; ++k)
                s.symbol[k] = (Fruit) (k == oddPos ? odd : pair);
        }
        else
        {
            // Three distinct symbols: partial Fisher-Yates over the full set.
            int pool[(int) Fruit::NumFruits];
            for (int k = 0; k < n; ++k) pool[k] = k;
            for (int k = 0; k < 3; ++k)
            {
                const int j = k + rng.nextInt (n - k);
                std::swap (pool[k], pool[j]);
                s.symbol[k] = (Fruit) pool[k];
            }
        }
        return s;
    }

private:
    juce::Random rng { juce::Random::getSystemRandom().nextInt64() };
};
