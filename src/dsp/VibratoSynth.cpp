#include "dsp/VibratoSynth.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace novapitch::dsp
{

void VibratoSynth::prepare (double frameRateHz, uint32_t seed) noexcept
{
    setFrameRate (frameRateHz);
    seed0 = seed != 0 ? seed : 0x2545f491u;
    reset();
}

void VibratoSynth::reset() noexcept
{
    phase = 0.0;
    noteTime = 0.0;
    rateWalk = depthWalk = 0.0f;
    rng = seed0;
    active = false;
}

float VibratoSynth::random() noexcept
{
    // xorshift32: deterministic, allocation-free
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return static_cast<float> (rng) / 2147483648.0f - 1.0f;
}

VibratoSynth::Output VibratoSynth::process (bool voiced, bool noteStarted) noexcept
{
    Output out;
    if (! voiced)
    {
        active = false;
        return out;
    }

    if (noteStarted || ! active)
    {
        active = true;
        noteTime = 0.0;
        phase = 0.0;
    }
    noteTime += dt;

    const float depth = std::clamp (settings.depthCents, 0.0f, 100.0f);
    const float am = std::clamp (settings.amplitudeMod, 0.0f, 1.0f);
    if (depth <= 0.0f && am <= 0.0f)
        return out;

    // Smoothed random walks (~1 Hz) for a less mechanical vibrato.
    const auto smooth = static_cast<float> (1.0 - std::exp (-dt * 2.0 * std::numbers::pi * 1.0));
    rateWalk += smooth * (random() - rateWalk);
    depthWalk += smooth * (random() - depthWalk);
    const float variation = std::clamp (settings.variation, 0.0f, 1.0f);

    const float rate = std::clamp (settings.rateHz, 0.1f, 10.0f) * (1.0f + 0.15f * variation * rateWalk);
    const float depthNow = depth * (1.0f + 0.3f * variation * depthWalk);

    const double delay = std::clamp (settings.delayMs, 0.0f, 2000.0f) / 1000.0;
    const auto env = static_cast<float> (std::clamp ((noteTime - delay) / kFadeInSeconds, 0.0, 1.0));

    if (noteTime > delay)
        phase = std::fmod (phase + 2.0 * std::numbers::pi * rate * dt, 2.0 * std::numbers::pi);
    const float s = shapeValue (settings.shape, phase);

    out.pitchCents = depthNow * env * s;
    out.gain = 1.0f + kMaxAmplitudeMod * am * env * s;
    out.formantFactor = std::exp2 (std::clamp (settings.formantMod, 0.0f, 1.0f) * out.pitchCents / 1200.0f);
    return out;
}

float VibratoSynth::shapeValue (int shape, double ph) noexcept
{
    constexpr double pi = std::numbers::pi;
    switch (shape)
    {
        case 1: // triangle, starting at 0 and rising (no jump at the onset)
            return static_cast<float> ((2.0 / pi) * std::asin (std::sin (ph)));
        case 2: // square, softened so the pitch steps don't click
            return static_cast<float> (std::tanh (5.0 * std::sin (ph)) / std::tanh (5.0));
        case 3: // sawtooth: rises over 96 % of the cycle, falls back over the last 4 % (no click)
        {
            const double u = std::fmod (ph / (2.0 * pi) + 0.48, 1.0); // phase 0 -> value 0
            return static_cast<float> (u < 0.96 ? -1.0 + 2.0 * u / 0.96 : 1.0 - 2.0 * (u - 0.96) / 0.04);
        }
        default:
            return static_cast<float> (std::sin (ph));
    }
}

} // namespace novapitch::dsp
