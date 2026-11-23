#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace novapitch::dsp
{

/** Streaming TD-PSOLA pitch shifter with a fixed, configurable latency.

    - Analysis: pitch marks (epochs) are placed one analysis period apart and
      snapped to the waveform peak of a low-passed mono signal (search window
      ±30 % of the period). In unvoiced parts epochs follow a fixed 5 ms grid.
    - Synthesis: synthesis marks are spaced by period / ratio. Each mark takes
      the nearest known analysis epoch e <= s + latency and overlap-adds a
      Hann-windowed grain of ±1 analysis period (asymmetric when needed).
      Grains are not resampled, so formants are preserved.
    - Output sample x is computed when input x + latency has arrived, so any
      epoch up to `latency` samples in the future can be used. With a small
      latency (Live) the grain may come from the previous period, which adds up
      to ~1 period of delay to voiced parts only; with Tracking latency (10 ms)
      grains are aligned for f0 >= ~50 Hz.
    - Unvoiced: grains use e == s on a constant grid (Hann COLA), i.e. the
      output equals the input delayed by `latency` exactly. A smoothed voicing
      gain additionally crossfades wet/dry at voiced <-> unvoiced transitions.
    - Normalised overlap-add: output = Σ w·x / max(Σ w, 1).

    Every channel uses the same grains (phase-coherent stereo). Real-time safe
    after prepare(). */
class PsolaShifter
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate, int maxLatencySamples);
    void reset() noexcept;

    /** Changes the latency (clamped to the prepared maximum). Resets the
        synthesis state and fades the output in over ~10 ms. */
    void setLatency (int samples) noexcept;
    int getLatency() const noexcept { return latency; }

    /** Control, typically once per analysis frame.
        periodSamples: analysis period in input samples (<= 0 or !voiced: unvoiced).
        ratio: output / input frequency ratio (clamped to 0.25..4). */
    void setControl (bool voiced, float periodSamples, float ratio) noexcept;

    /** Dry/wet mix 0..1 (smoothed). The dry path is delayed by the latency. */
    void setMix (float mix01) noexcept { mixTarget = mix01 < 0.0f ? 0.0f : (mix01 > 1.0f ? 1.0f : mix01); }

    /** Processes numChannels (<= kMaxChannels) channels in place. `mono` is the
        mono sum used for epoch detection (may alias io[0]). */
    void process (float* const* io, int numChannels, const float* mono, int numSamples) noexcept;

private:
    struct Grain
    {
        int64_t s = 0;  // synthesis mark (output timeline == input timeline)
        int64_t e = 0;  // analysis epoch the grain is taken from
        int left = 1;   // window half-length before s
        int right = 1;  // window half-length after s
    };

    static constexpr int kMaxGrains = 32;
    static constexpr int kMaxEpochs = 1024;
    static constexpr int kWindowTableSize = 1024;

    void trackEpochs (int64_t n) noexcept;
    void pushEpoch (int64_t pos, float period) noexcept;
    bool findEpoch (int64_t s, int64_t maxPos, int64_t& epoch, float& period) const noexcept;
    void createGrains (int64_t x) noexcept;
    float rise (float u) const noexcept;

    double sampleRate = 48000.0;
    int maxLatency = 0;
    int latency = 0;
    int unvoicedPeriod = 240;
    float minPeriod = 16.0f, maxPeriod = 2400.0f;

    std::array<std::vector<float>, kMaxChannels> ring;
    std::vector<float> monoRing;
    int64_t mask = 0;
    int64_t written = 0; // number of input samples written

    float lpCoeff = 0.1f, lpState = 0.0f;

    // Control
    bool voiced = false;
    float period = 0.0f;
    float ratio = 1.0f;

    // Epochs (ring)
    std::array<int64_t, kMaxEpochs> epochPos {};
    std::array<float, kMaxEpochs> epochPeriod {};
    int epochWrite = 0, epochCount = 0;
    int64_t lastEpoch = 0;
    bool haveEpoch = false;

    // Synthesis
    std::array<Grain, kMaxGrains> grains {};
    int numGrains = 0;
    double nextSynth = 0.0;

    // Gains
    float voicedGain = 0.0f, voicedCoeff = 0.01f;
    float mix = 1.0f, mixTarget = 1.0f, mixCoeff = 0.001f;
    float fadeIn = 1.0f, fadeStep = 0.001f;

    std::array<float, kWindowTableSize + 1> riseTable {};
};

} // namespace novapitch::dsp
