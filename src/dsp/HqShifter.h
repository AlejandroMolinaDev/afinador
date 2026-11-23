#pragma once

#include <array>
#include <memory>
#include <vector>

namespace novapitch::dsp
{

/** HQ pitch engine: wraps signalsmith-stretch (phase vocoder with spectral
    envelope handling) behind the same control interface as PsolaShifter.
    Higher quality on large shifts and noisy material, ~100 ms latency.

    - Pitch: setTransposeFactor(ratio) updated every analysis frame, with a
      tonality limit at 8 kHz (keeps breath / sibilance natural).
    - Formants: setFormantFactor(factor, compensatePitch = preserve), with the
      detected f0 as formant base when voiced.
    - Computation is split across calls (splitComputation) to avoid CPU spikes
      in small host buffers.
    - Dry path (mix, bypass) is delayed by the same latency.
    Real-time safe after prepare(). */
class HqShifter
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr double kBlockSeconds = 0.090; // shorter blocks bias pitch (+4 c at +12 st with 45 ms)
    static constexpr double kIntervalSeconds = 0.010;

    HqShifter();
    ~HqShifter();

    /** maxBlockSize: largest numSamples passed to process(). */
    void prepare (double sampleRate, int numChannels, int maxBlockSize);
    void reset() noexcept;

    /** Total latency (input + output) in samples. */
    int getLatency() const noexcept { return latency; }

    /** Latency the engine will have at a sample rate (allocates: not for the audio thread). */
    static int computeLatency (double sampleRate);

    void setControl (bool voiced, float f0Hz, float ratio) noexcept;
    void setFormant (float factor, bool preserve) noexcept;
    void setMix (float mix01) noexcept { mixTarget = mix01 < 0.0f ? 0.0f : (mix01 > 1.0f ? 1.0f : mix01); }

    void process (float* const* io, int numChannels, int numSamples) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    double sampleRate = 48000.0;
    int channels = 2;
    int maxBlock = 0;
    int latency = 0;

    std::array<std::vector<float>, kMaxChannels> inScratch, outScratch, dryRing;
    int dryMask = 0, dryWrite = 0;

    // Last control values, re-applied after reset()
    float transpose = 1.0f, formantBase = 0.0f, formantFactor = 1.0f;
    bool formantPreserve = true;

    float mix = 1.0f, mixTarget = 1.0f, mixCoeff = 0.001f;
    float fadeIn = 1.0f, fadeStep = 0.001f;
};

} // namespace novapitch::dsp
