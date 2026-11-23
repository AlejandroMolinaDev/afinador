#include "dsp/PsolaShifter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace novapitch::dsp
{

namespace
{
int nextPow2 (int64_t n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}
} // namespace

void PsolaShifter::prepare (double newSampleRate, int maxLatencySamples)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxLatency = std::max (0, maxLatencySamples);
    unvoicedPeriod = std::max (8, static_cast<int> (std::lround (0.005 * sampleRate)));
    minPeriod = static_cast<float> (sampleRate / 2500.0);
    maxPeriod = static_cast<float> (sampleRate / 20.0);

    const int size = nextPow2 (static_cast<int64_t> (maxLatency) + static_cast<int64_t> (4.0f * maxPeriod) + 4096);
    for (auto& r : ring)
        r.assign (static_cast<size_t> (size), 0.0f);
    monoRing.assign (static_cast<size_t> (size), 0.0f);
    mask = size - 1;

    lpCoeff = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * 1000.0 / sampleRate));
    voicedCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (0.006 * sampleRate)));
    mixCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
    fadeStep = static_cast<float> (1.0 / (0.01 * sampleRate));

    for (int i = 0; i <= kWindowTableSize; ++i)
        riseTable[static_cast<size_t> (i)] =
            static_cast<float> (0.5 - 0.5 * std::cos (std::numbers::pi * i / kWindowTableSize));

    latency = std::min (latency, maxLatency);
    reset();
}

void PsolaShifter::reset() noexcept
{
    for (auto& r : ring)
        std::fill (r.begin(), r.end(), 0.0f);
    std::fill (monoRing.begin(), monoRing.end(), 0.0f);
    written = 0;
    lpState = 0.0f;
    epochWrite = epochCount = 0;
    haveEpoch = false;
    lastEpoch = 0;
    numGrains = 0;
    nextSynth = -static_cast<double> (latency);
    voicedGain = 0.0f;
    mix = mixTarget;
    fadeIn = 1.0f;
}

void PsolaShifter::setLatency (int samples) noexcept
{
    const int l = std::clamp (samples, 0, maxLatency);
    if (l == latency)
        return;
    latency = l;
    numGrains = 0;
    nextSynth = static_cast<double> (written - latency);
    fadeIn = 0.0f; // avoid the click of the read-position jump
}

void PsolaShifter::setControl (bool isVoiced, float periodSamples, float newRatio) noexcept
{
    voiced = isVoiced && periodSamples > 0.0f && std::isfinite (periodSamples);
    period = voiced ? std::clamp (periodSamples, minPeriod, maxPeriod) : 0.0f;
    ratio = std::isfinite (newRatio) ? std::clamp (newRatio, 0.25f, 4.0f) : 1.0f;
}

void PsolaShifter::setFormantFactor (float factor) noexcept
{
    formantFactor = std::isfinite (factor) ? std::clamp (factor, 0.25f, 4.0f) : 1.0f;
}

void PsolaShifter::setGrainScale (float scale) noexcept
{
    grainScale = std::isfinite (scale) ? std::clamp (scale, 0.5f, 1.0f) : 1.0f;
}

float PsolaShifter::rise (float u) const noexcept
{
    const float pos = std::clamp (u, 0.0f, 1.0f) * static_cast<float> (kWindowTableSize);
    const int i = std::min (static_cast<int> (pos), kWindowTableSize - 1);
    const float frac = pos - static_cast<float> (i);
    return riseTable[static_cast<size_t> (i)] + frac * (riseTable[static_cast<size_t> (i + 1)] - riseTable[static_cast<size_t> (i)]);
}

//==============================================================================
void PsolaShifter::pushEpoch (int64_t pos, float p) noexcept
{
    // Measured spacing: only between consecutive voiced epochs of plausible distance.
    float spacing = 0.0f;
    if (haveEpoch && voiced)
    {
        const auto d = static_cast<float> (pos - lastEpoch);
        if (d > 0.5f * p && d < 1.5f * p)
            spacing = d;
    }
    epochPos[static_cast<size_t> (epochWrite)] = pos;
    epochPeriod[static_cast<size_t> (epochWrite)] = p;
    epochSpacing[static_cast<size_t> (epochWrite)] = spacing;
    epochWrite = (epochWrite + 1) % kMaxEpochs;
    epochCount = std::min (epochCount + 1, kMaxEpochs);
    lastEpoch = pos;
    haveEpoch = true;
}

void PsolaShifter::trackEpochs (int64_t n) noexcept
{
    if (! voiced)
    {
        const int64_t predicted = haveEpoch ? lastEpoch + unvoicedPeriod : n;
        if (n >= predicted)
            pushEpoch (predicted, static_cast<float> (unvoicedPeriod));
        return;
    }

    const float T = period;
    const int half = std::max (1, static_cast<int> (0.3f * T));

    if (! haveEpoch || static_cast<float> (n - lastEpoch) > 4.0f * T)
    {
        // (Re)seed on the highest peak of the last period.
        const int64_t lo = n - static_cast<int64_t> (T) - half;
        int64_t best = n - half;
        for (int64_t k = lo; k <= n - half; ++k)
            if (monoRing[static_cast<size_t> (k & mask)] > monoRing[static_cast<size_t> (best & mask)])
                best = k;
        pushEpoch (best, T);
        return;
    }

    const int64_t predicted = lastEpoch + static_cast<int64_t> (std::lround (T));
    if (n < predicted + half)
        return;

    const int64_t lo = std::max (lastEpoch + static_cast<int64_t> (0.5f * T), predicted - half);
    const int64_t hi = predicted + half;
    int64_t best = lo;
    for (int64_t k = lo + 1; k <= hi; ++k)
        if (monoRing[static_cast<size_t> (k & mask)] > monoRing[static_cast<size_t> (best & mask)])
            best = k;
    pushEpoch (best, T);
}

bool PsolaShifter::findEpoch (int64_t s, int64_t maxPos, int64_t& epoch, float& p, float* spacing) const noexcept
{
    bool found = false;
    int64_t bestDist = 0;
    for (int i = 0; i < epochCount; ++i)
    {
        const int idx = (epochWrite - 1 - i + kMaxEpochs) % kMaxEpochs;
        const int64_t pos = epochPos[static_cast<size_t> (idx)];
        if (pos > maxPos)
            continue;
        const int64_t d = pos > s ? pos - s : s - pos;
        if (! found || d < bestDist)
        {
            found = true;
            bestDist = d;
            epoch = pos;
            p = epochPeriod[static_cast<size_t> (idx)];
            if (spacing != nullptr)
            {
                // Average of this epoch's spacing and the next one (when known): less jitter.
                float sp = epochSpacing[static_cast<size_t> (idx)];
                const int next = (idx + 1) % kMaxEpochs;
                if (i > 0 && epochSpacing[static_cast<size_t> (next)] > 0.0f && sp > 0.0f)
                    sp = 0.5f * (sp + epochSpacing[static_cast<size_t> (next)]);
                *spacing = sp;
            }
        }
        if (pos < s - static_cast<int64_t> (3.0f * maxPeriod))
            break;
        if (found && pos < s && s - pos > bestDist)
            break; // epochs only get further away from here
    }
    return found;
}

void PsolaShifter::createGrains (int64_t x) noexcept
{
    const float synthPeriod = voiced ? period : static_cast<float> (unvoicedPeriod);

    // Recover if synthesis fell behind (reset, latency change, very long period jump).
    if (nextSynth < static_cast<double> (x))
        nextSynth = static_cast<double> (x) + synthPeriod;

    while (nextSynth - synthPeriod <= static_cast<double> (x) + 0.5 && numGrains < kMaxGrains)
    {
        Grain g;
        g.s = static_cast<int64_t> (std::llround (nextSynth));
        const int leftAvail = static_cast<int> (std::max<int64_t> (1, g.s - x));

        int64_t e = g.s;
        float p = static_cast<float> (unvoicedPeriod);
        float measured = 0.0f;
        // With formants raised (fr > 1) a grain reads further ahead of its mark:
        // keep every source sample inside what has already arrived.
        const float scaledPeriod = (voiced ? period : 0.0f) * grainScale;
        const auto ahead = static_cast<int64_t> (std::ceil (std::max (0.0f, scaledPeriod - scaledPeriod / formantFactor)));
        if (voiced && findEpoch (g.s, g.s + latency - ahead, e, p, &measured))
        {
            // The grain spans ±(grainScale · period) of the source; resampled by fr it
            // spans ±(grainScale · period / fr) of the output.
            g.fr = formantFactor;
            g.resampled = std::abs (formantFactor - 1.0f) > 1.0e-6f;
            const int half = std::max (2, static_cast<int> (std::lround (p * grainScale / formantFactor)));
            g.e = e;
            g.left = std::min (half, leftAvail);
            g.right = half;
        }
        else
        {
            g.e = g.s; // identity grain
            g.left = std::min (unvoicedPeriod, leftAvail);
            g.right = unvoicedPeriod;
        }

        grains[static_cast<size_t> (numGrains++)] = g;

        // Note transitions: the waveform's own period leads the detector by ~20 ms.
        float synthSource = period;
        if (voiced && measured > 0.0f && std::abs (measured - period) > kTransitionThreshold * period)
            synthSource = measured;
        const double step = voiced ? static_cast<double> (synthSource) / ratio : static_cast<double> (unvoicedPeriod);
        nextSynth += std::max (2.0, step);
    }
}

//==============================================================================
void PsolaShifter::process (float* const* io, int numChannels, const float* mono, int numSamples) noexcept
{
    if (mask == 0 || io == nullptr)
        return;

    numChannels = std::clamp (numChannels, 0, kMaxChannels);
    const float voicedTarget = voiced ? 1.0f : 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const int64_t n = written++;
        for (int ch = 0; ch < numChannels; ++ch)
            ring[static_cast<size_t> (ch)][static_cast<size_t> (n & mask)] = io[ch][i];

        lpState += lpCoeff * ((mono != nullptr ? mono[i] : io[0][i]) - lpState);
        monoRing[static_cast<size_t> (n & mask)] = lpState;

        trackEpochs (n);

        const int64_t x = n - latency;
        createGrains (x);

        // Window weights of the active grains at x (shared by all channels).
        std::array<float, kMaxGrains> w {};
        float wsum = 0.0f;
        int alive = 0;
        for (int k = 0; k < numGrains; ++k)
        {
            const auto& g = grains[static_cast<size_t> (k)];
            if (x >= g.s + g.right)
                continue; // finished: dropped below
            grains[static_cast<size_t> (alive)] = g;
            float weight = 0.0f;
            if (x >= g.s - g.left)
            {
                weight = x < g.s ? rise (static_cast<float> (x - (g.s - g.left)) / static_cast<float> (g.left))
                                 : rise (1.0f - static_cast<float> (x - g.s) / static_cast<float> (g.right));
            }
            w[static_cast<size_t> (alive)] = weight;
            wsum += weight;
            ++alive;
        }
        numGrains = alive;

        voicedGain += voicedCoeff * (voicedTarget - voicedGain);
        mix += mixCoeff * (mixTarget - mix);
        const float norm = 1.0f / std::max (wsum, 1.0f);
        const float wetAmount = mix * voicedGain;
        const float gain = fadeIn;
        if (fadeIn < 1.0f)
            fadeIn = std::min (1.0f, fadeIn + fadeStep);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto& r = ring[static_cast<size_t> (ch)];
            const float dry = r[static_cast<size_t> (x & mask)];
            float acc = 0.0f;
            for (int k = 0; k < numGrains; ++k)
            {
                const float weight = w[static_cast<size_t> (k)];
                if (weight <= 0.0f)
                    continue;

                const auto& g = grains[static_cast<size_t> (k)];
                if (! g.resampled)
                {
                    acc += weight * r[static_cast<size_t> ((x - g.s + g.e) & mask)];
                }
                else
                {
                    // Resampled grain (formant shift): linear interpolation.
                    const double pos = static_cast<double> (g.e) + static_cast<double> (x - g.s) * g.fr;
                    const double fl = std::floor (pos);
                    const auto i0 = static_cast<int64_t> (fl);
                    const auto frac = static_cast<float> (pos - fl);
                    const float a = r[static_cast<size_t> (i0 & mask)];
                    const float b = r[static_cast<size_t> ((i0 + 1) & mask)];
                    acc += weight * (a + frac * (b - a));
                }
            }
            const float wet = acc * norm;
            io[ch][i] = gain * (dry + wetAmount * (wet - dry));
        }
    }
}

} // namespace novapitch::dsp
