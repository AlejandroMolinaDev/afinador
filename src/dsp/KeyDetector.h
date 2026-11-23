#pragma once

#include "dsp/Scales.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace novapitch::dsp
{

/** Key detection for a monophonic voice.

    The voice's detected f0 frames are accumulated into a 12-bin chromagram
    (pitch-class histogram) weighted by duration, level and stability (frames
    near a semitone count more than glides). The chromagram is correlated
    (Pearson) with the 24 rotated Krumhansl–Schmuckler key profiles; the best
    one wins. Relative keys (C major / A minor) share their notes, so for
    pitch correction the choice between them does not change the result.

    Pure C++, real-time safe (fixed-size arrays, no allocation). */
class KeyDetector
{
public:
    struct Result
    {
        bool valid = false;
        int key = 0;          ///< tonic pitch class (0 = C)
        bool minor = false;
        float correlation = 0.0f;
        float confidence = 0.0f; ///< 0..1: margin over the best key with a different set of notes
        ScaleType scale() const noexcept { return minor ? ScaleType::NaturalMinor : ScaleType::Major; }
    };

    using Chroma = std::array<float, 12>;

    void reset() noexcept { chroma.fill (0.0f); total = 0.0f; }

    /** Weight of one analysis frame: `seconds` long, `rmsDb` level, pitch `midi`. */
    static float frameWeight (float midi, float seconds, float rmsDb) noexcept
    {
        const float deviation = std::abs (midi - std::round (midi)); // 0..0.5 semitones
        const float stability = std::max (0.0f, 1.0f - deviation / 0.4f);
        const float level = std::pow (10.0f, std::clamp (rmsDb, -60.0f, 0.0f) / 40.0f); // gentle level weight
        return seconds * stability * level;
    }

    void add (float midi, float weight) noexcept
    {
        if (! (weight > 0.0f) || ! std::isfinite (midi))
            return;
        const int pc = ((static_cast<int> (std::lround (midi)) % 12) + 12) % 12;
        chroma[static_cast<size_t> (pc)] += weight;
        total += weight;
    }

    const Chroma& getChroma() const noexcept { return chroma; }
    float getTotalWeight() const noexcept { return total; }

    Result estimate() const noexcept { return estimate (chroma); }

    static Result estimate (const Chroma& c) noexcept
    {
        static constexpr Chroma major { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
        static constexpr Chroma minor { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

        Result best;
        float sum = 0.0f;
        for (float v : c)
            sum += v;
        if (! (sum > 0.0f))
            return best;

        std::array<float, 24> scores {};
        for (int mode = 0; mode < 2; ++mode)
            for (int key = 0; key < 12; ++key)
            {
                const auto& profile = mode == 0 ? major : minor;
                Chroma rotated {};
                for (int pc = 0; pc < 12; ++pc)
                    rotated[static_cast<size_t> ((pc + key) % 12)] = profile[static_cast<size_t> (pc)];
                scores[static_cast<size_t> (mode * 12 + key)] = pearson (c, rotated);
            }

        int bestIndex = 0;
        for (int i = 1; i < 24; ++i)
            if (scores[static_cast<size_t> (i)] > scores[static_cast<size_t> (bestIndex)])
                bestIndex = i;

        best.valid = true;
        best.minor = bestIndex >= 12;
        best.key = bestIndex % 12;
        best.correlation = scores[static_cast<size_t> (bestIndex)];

        // Margin over the best key whose notes differ (the relative key has the same notes).
        const int relative = best.minor ? (best.key + 3) % 12 : 12 + (best.key + 9) % 12;
        float second = -1.0f;
        for (int i = 0; i < 24; ++i)
            if (i != bestIndex && i != relative)
                second = std::max (second, scores[static_cast<size_t> (i)]);
        best.confidence = std::clamp ((best.correlation - second) / 0.25f, 0.0f, 1.0f);
        return best;
    }

private:
    static float pearson (const Chroma& a, const Chroma& b) noexcept
    {
        float ma = 0.0f, mb = 0.0f;
        for (size_t i = 0; i < 12; ++i)
        {
            ma += a[i];
            mb += b[i];
        }
        ma /= 12.0f;
        mb /= 12.0f;
        float num = 0.0f, da = 0.0f, db = 0.0f;
        for (size_t i = 0; i < 12; ++i)
        {
            num += (a[i] - ma) * (b[i] - mb);
            da += (a[i] - ma) * (a[i] - ma);
            db += (b[i] - mb) * (b[i] - mb);
        }
        return da > 0.0f && db > 0.0f ? num / std::sqrt (da * db) : 0.0f;
    }

    Chroma chroma {};
    float total = 0.0f;
};

} // namespace novapitch::dsp
