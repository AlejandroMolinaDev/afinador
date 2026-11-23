#pragma once

// Synthetic test signals (pure C++). Deterministic: every generator that uses
// randomness takes an explicit seed.

#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

namespace novapitch::signals
{

using Buffer = std::vector<float>;

inline int numSamplesFor (double sampleRate, double seconds)
{
    return static_cast<int> (std::lround (sampleRate * seconds));
}

inline Buffer silence (double sampleRate, double seconds)
{
    return Buffer (static_cast<size_t> (numSamplesFor (sampleRate, seconds)), 0.0f);
}

/** Sum of harmonics k = 1..amps.size() with the given amplitudes, following an
    arbitrary instantaneous-frequency function f(t). Band-limited to 0.45 * fs. */
template <typename FreqFn>
Buffer harmonicSweep (double sampleRate, double seconds, const std::vector<float>& amps, FreqFn freqAt)
{
    const int n = numSamplesFor (sampleRate, seconds);
    Buffer out (static_cast<size_t> (n), 0.0f);
    double phase = 0.0; // cycles of the fundamental
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sampleRate;
        const double f = freqAt (t);
        double v = 0.0;
        for (size_t k = 0; k < amps.size(); ++k)
        {
            const double hk = static_cast<double> (k + 1);
            if (hk * f >= 0.45 * sampleRate)
                break;
            v += amps[k] * std::sin (2.0 * std::numbers::pi * hk * phase);
        }
        out[static_cast<size_t> (i)] = static_cast<float> (v);
        phase += f / sampleRate;
        phase -= std::floor (phase);
    }
    return out;
}

inline Buffer sine (double freqHz, double sampleRate, double seconds, float amplitude = 0.5f)
{
    return harmonicSweep (sampleRate, seconds, { amplitude }, [=] (double) { return freqHz; });
}

/** Band-limited sawtooth (additive), peak amplitude about `amplitude`. */
inline Buffer sawtooth (double freqHz, double sampleRate, double seconds, float amplitude = 0.4f)
{
    std::vector<float> amps;
    for (int k = 1; k <= 200; ++k)
        amps.push_back (amplitude * 0.6f * ((k % 2 == 1) ? 1.0f : -1.0f) / static_cast<float> (k));
    return harmonicSweep (sampleRate, seconds, amps, [=] (double) { return freqHz; });
}

/** Vowel-like harmonic spectrum with a weak fundamental (octave-error stress test). */
inline Buffer weakFundamental (double freqHz, double sampleRate, double seconds)
{
    return harmonicSweep (sampleRate, seconds, { 0.08f, 0.30f, 0.22f, 0.10f, 0.06f, 0.04f, 0.03f },
                          [=] (double) { return freqHz; });
}

/** Sine with sinusoidal vibrato: f(t) = f0 * 2^(depth/1200 * sin(2π rate t)). */
inline Buffer vibratoSine (double f0, double rateHz, double depthCents, double sampleRate, double seconds,
                           float amplitude = 0.5f)
{
    return harmonicSweep (sampleRate, seconds, { amplitude }, [=] (double t) {
        return f0 * std::exp2 (depthCents / 1200.0 * std::sin (2.0 * std::numbers::pi * rateHz * t));
    });
}

/** Exponential glissando from f0 to f1 over the whole duration. */
inline Buffer glissando (double f0, double f1, double sampleRate, double seconds, float amplitude = 0.5f)
{
    return harmonicSweep (sampleRate, seconds, { amplitude }, [=] (double t) {
        return f0 * std::pow (f1 / f0, t / seconds);
    });
}

inline Buffer whiteNoise (double sampleRate, double seconds, float amplitude, uint32_t seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-amplitude, amplitude);
    Buffer out (static_cast<size_t> (numSamplesFor (sampleRate, seconds)));
    for (auto& v : out)
        v = dist (rng);
    return out;
}

inline void addInPlace (Buffer& a, const Buffer& b, float gain = 1.0f)
{
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        a[i] += gain * b[i];
}

inline Buffer concat (std::initializer_list<Buffer> parts)
{
    Buffer out;
    for (const auto& p : parts)
        out.insert (out.end(), p.begin(), p.end());
    return out;
}

} // namespace novapitch::signals
