#pragma once

#include <array>

namespace novapitch::dsp
{

/** Detects vibrato on the detected pitch curve and splits it into a slow
    "centre" and the periodic oscillation (own method, see docs/DSP_NOTES.md):

    - Works at the analysis frame rate on pitch in cents.
    - Every ~10 ms, the last 0.6 s are linearly detrended (least squares) and
      the normalised autocorrelation of the residual is searched for a peak at
      lags corresponding to 3.5–8 Hz. A clear peak (r >= 0.45) with a peak
      amplitude of 15–150 cents is vibrato.
    - Depth: the trend is measured from the means of the last two full periods
      (each removes the sinusoid exactly), so the depth is exact even on a glide.
    - Centre = mean of the last vibrato period (removes a sinusoid exactly);
      vibrato = pitch − centre. A smoothed weight (attack ~60 ms, release
      ~120 ms) fades the split in and out, so note changes and glides (no
      periodicity) are never treated as vibrato.

    Real-time safe: fixed-size buffers, no allocation. */
class VibratoAnalyzer
{
public:
    struct Result
    {
        float centreCents = 0.0f;  ///< pitch without vibrato (cents, same reference as the input)
        float vibratoCents = 0.0f; ///< weighted oscillation: pitch ≈ centre + vibrato
        float weight = 0.0f;       ///< 0..1, how sure we are that this is vibrato
        float rateHz = 0.0f;       ///< detected rate (0 if none)
        float depthCents = 0.0f;   ///< detected peak depth (± cents, 0 if none)
    };

    static constexpr float kMinRateHz = 3.5f, kMaxRateHz = 8.0f;
    static constexpr float kMinDepthCents = 15.0f, kMaxDepthCents = 150.0f;
    static constexpr float kWindowSeconds = 0.60f; // >= 2 periods at 3.5 Hz
    static constexpr float kMinCorrelation = 0.45f;

    void prepare (double frameRateHz) noexcept;
    void setFrameRate (double frameRateHz) noexcept;
    void reset() noexcept;

    /** One analysis frame. pitchCents: detected pitch in cents (e.g. MIDI × 100). */
    Result process (bool voiced, float pitchCents) noexcept;

    const Result& getLast() const noexcept { return last; }

private:
    static constexpr int kCapacity = 1024; // frames (> 0.45 s at any frame rate we use)

    float at (int ago) const noexcept; // 0 = newest
    void analyse() noexcept;

    double frameRate = 375.0;
    int windowFrames = 225;
    std::array<float, kCapacity> history {};
    std::array<float, kCapacity> residual {};
    int write = 0, count = 0;
    int framesSinceAnalysis = 0;

    // Latest periodicity estimate
    bool detected = false;
    int periodFrames = 0;
    float rate = 0.0f, depth = 0.0f;
    float weight = 0.0f;
    Result last;
};

} // namespace novapitch::dsp
