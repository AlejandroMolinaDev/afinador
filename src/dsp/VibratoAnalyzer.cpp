#include "dsp/VibratoAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace novapitch::dsp
{

namespace
{
constexpr int kAnalysisEveryFrames = 4; // ~10 ms at 375 frames/s
constexpr float kAttackSeconds = 0.06f;
constexpr float kReleaseSeconds = 0.04f;
} // namespace

void VibratoAnalyzer::prepare (double frameRateHz) noexcept
{
    setFrameRate (frameRateHz);
    reset();
}

void VibratoAnalyzer::setFrameRate (double frameRateHz) noexcept
{
    if (frameRateHz <= 0.0)
        return;
    const bool changed = std::abs (frameRateHz - frameRate) > 1.0e-9;
    frameRate = frameRateHz;
    windowFrames = std::clamp (static_cast<int> (std::lround (kWindowSeconds * frameRate)), 16, kCapacity - 1);
    if (changed)
        reset(); // history spacing changed
}

void VibratoAnalyzer::reset() noexcept
{
    write = count = framesSinceAnalysis = 0;
    detected = false;
    periodFrames = 0;
    rate = depth = weight = 0.0f;
    last = {};
}

float VibratoAnalyzer::at (int ago) const noexcept
{
    return history[static_cast<size_t> ((write - 1 - ago + kCapacity) % kCapacity)];
}

void VibratoAnalyzer::analyse() noexcept
{
    detected = false;
    const int n = windowFrames;
    if (count < n)
        return;

    // Oldest -> newest, linearly detrended (least squares).
    double sumX = 0.0, sumTX = 0.0, sumTT = 0.0;
    const double mid = 0.5 * (n - 1);
    for (int i = 0; i < n; ++i)
    {
        const double x = at (n - 1 - i);
        const double t = i - mid;
        sumX += x;
        sumTX += t * x;
        sumTT += t * t;
    }
    const double mean = sumX / n;
    const double slope = sumTX / sumTT;

    double energy = 0.0;
    int crossings = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto r = static_cast<float> (at (n - 1 - i) - mean - slope * (i - mid));
        residual[static_cast<size_t> (i)] = r;
        energy += static_cast<double> (r) * r;
        if (i > 0 && ((r >= 0.0f) != (residual[static_cast<size_t> (i - 1)] >= 0.0f)))
            ++crossings;
    }
    if (energy < 1.0e-6)
        return;

    const int minLag = std::max (2, static_cast<int> (std::floor (frameRate / kMaxRateHz)));
    const int maxLag = std::min (n - 4, static_cast<int> (std::ceil (frameRate / kMinRateHz)));
    if (minLag >= maxLag)
        return;

    auto corr = [&] (int lag) {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int i = 0; i + lag < n; ++i)
        {
            const double a = residual[static_cast<size_t> (i)], b = residual[static_cast<size_t> (i + lag)];
            xy += a * b;
            xx += a * a;
            yy += b * b;
        }
        return xx > 0.0 && yy > 0.0 ? xy / std::sqrt (xx * yy) : 0.0;
    };

    int bestLag = -1;
    double best = -1.0, prev = corr (minLag - 1), cur = corr (minLag);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const double next = corr (lag + 1);
        if (cur >= prev && cur >= next && cur > best)
        {
            best = cur;
            bestLag = lag;
        }
        prev = cur;
        cur = next;
    }
    if (bestLag < 0 || best < kMinCorrelation)
        return;

    // A periodic oscillation crosses its mean about twice per period (a note
    // change or a glide does not).
    const double expectedCrossings = 2.0 * n / bestLag;
    if (crossings < std::max (3.0, 0.7 * expectedCrossings))
        return;

    // Sub-frame period by parabolic interpolation of the correlation peak.
    const double c0 = corr (bestLag - 1), c1 = best, c2 = corr (bestLag + 1);
    const double denom = c0 - 2.0 * c1 + c2;
    const double lag = bestLag + (std::abs (denom) > 1.0e-12 ? std::clamp (0.5 * (c0 - c2) / denom, -1.0, 1.0) : 0.0);

    // Depth over the last full period, around a local trend measured from the
    // means of the last two periods (each mean cancels the oscillation exactly).
    const int p = std::clamp (static_cast<int> (std::lround (lag)), 2, n / 2);
    double m1 = 0.0, m0 = 0.0;
    for (int k = 0; k < p; ++k)
    {
        m1 += at (k);
        m0 += at (k + p);
    }
    m1 /= p;
    m0 /= p;
    const double trendPerFrame = (m1 - m0) / p;
    double sq = 0.0;
    for (int k = 0; k < p; ++k)
    {
        const double line = m1 + trendPerFrame * (0.5 * (p - 1) - k);
        const double d = at (k) - line;
        sq += d * d;
    }
    const auto peak = static_cast<float> (std::sqrt (2.0 * sq / p));

    if (peak < kMinDepthCents || peak > kMaxDepthCents)
        return;

    detected = true;
    periodFrames = p;
    rate = static_cast<float> (frameRate / lag);
    depth = peak;
}

VibratoAnalyzer::Result VibratoAnalyzer::process (bool voiced, float pitchCents) noexcept
{
    if (! voiced || ! std::isfinite (pitchCents))
    {
        reset();
        return last;
    }

    history[static_cast<size_t> (write)] = pitchCents;
    write = (write + 1) % kCapacity;
    count = std::min (count + 1, kCapacity);

    if (++framesSinceAnalysis >= kAnalysisEveryFrames)
    {
        framesSinceAnalysis = 0;
        analyse();
    }

    const float seconds = detected ? kAttackSeconds : kReleaseSeconds;
    const auto coeff = static_cast<float> (1.0 - std::exp (-1.0 / (seconds * frameRate)));
    weight += coeff * ((detected ? 1.0f : 0.0f) - weight);

    float vib = 0.0f;
    if (periodFrames > 0 && count >= periodFrames && weight > 1.0e-3f)
    {
        double m = 0.0;
        for (int k = 0; k < periodFrames; ++k)
            m += at (k);
        m /= periodFrames;
        // Never let a stale period estimate turn a note change into "vibrato".
        const float limit = 1.5f * std::max (depth, kMinDepthCents);
        vib = weight * std::clamp (static_cast<float> (pitchCents - m), -limit, limit);
    }

    last.vibratoCents = vib;
    last.centreCents = pitchCents - vib;
    last.weight = weight;
    last.rateHz = weight > 0.5f ? rate : 0.0f;
    last.depthCents = weight > 0.5f ? depth : 0.0f;
    return last;
}

} // namespace novapitch::dsp
