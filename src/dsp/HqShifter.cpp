#include "dsp/HqShifter.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <cmath>

namespace novapitch::dsp
{

struct HqShifter::Impl
{
    signalsmith::stretch::SignalsmithStretch<float> stretch;
};

namespace
{
void configure (signalsmith::stretch::SignalsmithStretch<float>& s, int channels, double sampleRate)
{
    s.configure (channels,
                 static_cast<int> (std::lround (HqShifter::kBlockSeconds * sampleRate)),
                 static_cast<int> (std::lround (HqShifter::kIntervalSeconds * sampleRate)),
                 true); // split computation: no CPU spikes on small host buffers
}

int nextPow2 (int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}
} // namespace

HqShifter::HqShifter() : impl (std::make_unique<Impl>()) {}
HqShifter::~HqShifter() = default;

int HqShifter::computeLatency (double sr)
{
    signalsmith::stretch::SignalsmithStretch<float> s;
    configure (s, 1, sr);
    return s.inputLatency() + s.outputLatency();
}

void HqShifter::prepare (double newSampleRate, int numChannels, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    maxBlock = std::max (1, maxBlockSize);

    configure (impl->stretch, channels, sampleRate);
    latency = impl->stretch.inputLatency() + impl->stretch.outputLatency();

    const int ringSize = nextPow2 (latency + maxBlock + 1);
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        inScratch[static_cast<size_t> (ch)].assign (static_cast<size_t> (maxBlock), 0.0f);
        outScratch[static_cast<size_t> (ch)].assign (static_cast<size_t> (maxBlock), 0.0f);
        dryRing[static_cast<size_t> (ch)].assign (static_cast<size_t> (ringSize), 0.0f);
    }
    dryMask = ringSize - 1;

    mixCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));
    fadeStep = static_cast<float> (1.0 / (0.01 * sampleRate));
    reset();
}

void HqShifter::reset() noexcept
{
    impl->stretch.reset();
    impl->stretch.setTransposeFactor (transpose, static_cast<float> (8000.0 / sampleRate));
    impl->stretch.setFormantBase (formantBase);
    impl->stretch.setFormantFactor (formantFactor, formantPreserve);
    for (auto& r : dryRing)
        std::fill (r.begin(), r.end(), 0.0f);
    dryWrite = 0;
    mix = mixTarget;
    fadeIn = 1.0f;
}

void HqShifter::setControl (bool voiced, float f0Hz, float ratio) noexcept
{
    const float r = std::isfinite (ratio) ? std::clamp (ratio, 0.25f, 4.0f) : 1.0f;
    transpose = voiced ? r : 1.0f;
    // signalsmith expects the formant base frequency normalised to the sample rate.
    formantBase = voiced && f0Hz > 0.0f ? static_cast<float> (f0Hz / sampleRate) : 0.0f;
    impl->stretch.setTransposeFactor (transpose, static_cast<float> (8000.0 / sampleRate));
    impl->stretch.setFormantBase (formantBase);
}

void HqShifter::setFormant (float factor, bool preserve) noexcept
{
    formantFactor = std::isfinite (factor) ? std::clamp (factor, 0.25f, 4.0f) : 1.0f;
    formantPreserve = preserve;
    impl->stretch.setFormantFactor (formantFactor, formantPreserve);
}

void HqShifter::process (float* const* io, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, channels);
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - start);

        std::array<float*, kMaxChannels> in {}, out {};
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& is = inScratch[static_cast<size_t> (ch)];
            const int src = std::min (ch, numChannels - 1);
            std::copy (io[src] + start, io[src] + start + n, is.begin());
            in[static_cast<size_t> (ch)] = is.data();
            out[static_cast<size_t> (ch)] = outScratch[static_cast<size_t> (ch)].data();
        }

        impl->stretch.process (in, n, out, n);

        for (int i = 0; i < n; ++i)
        {
            mix += mixCoeff * (mixTarget - mix);
            const float gain = fadeIn;
            if (fadeIn < 1.0f)
                fadeIn = std::min (1.0f, fadeIn + fadeStep);

            const int w = (dryWrite + i) & dryMask;
            const int r = (dryWrite + i - latency) & dryMask;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& ring = dryRing[static_cast<size_t> (ch)];
                ring[static_cast<size_t> (w)] = inScratch[static_cast<size_t> (ch)][static_cast<size_t> (i)];
                const float dry = ring[static_cast<size_t> (r)];
                const float wet = outScratch[static_cast<size_t> (ch)][static_cast<size_t> (i)];
                io[ch][start + i] = gain * (dry + mix * (wet - dry));
            }
        }
        dryWrite = (dryWrite + n) & dryMask;
    }
}

} // namespace novapitch::dsp
