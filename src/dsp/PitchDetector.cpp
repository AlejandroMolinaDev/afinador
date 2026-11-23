#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"

#include <algorithm>
#include <cmath>

namespace novapitch::dsp
{

namespace
{
constexpr float kTiny = 1.0e-12f;
constexpr int kOctaveConfirmFrames = 8;     // ~21 ms at 48 kHz / hop 128
constexpr float kOctaveToleranceCents = 80.0f;
constexpr int kVoicedOnsetFrames = 2;
constexpr int kVoicedReleaseFrames = 3;
constexpr int kMaxMpmPeaks = 256;
constexpr double kMinAnalysisRate = 20000.0;

int nextPow2Order (int n) noexcept
{
    int order = 0;
    while ((1 << order) < n)
        ++order;
    return order;
}

/** Sub-sample position of a local minimum (or maximum) by parabolic fit. */
float parabolicVertex (const std::vector<float>& f, int i, int lo, int hi) noexcept
{
    if (i <= lo || i >= hi)
        return static_cast<float> (i);

    const float s0 = f[static_cast<size_t> (i - 1)];
    const float s1 = f[static_cast<size_t> (i)];
    const float s2 = f[static_cast<size_t> (i + 1)];
    const float denom = s0 - 2.0f * s1 + s2;

    if (std::abs (denom) < kTiny)
        return static_cast<float> (i);

    const float delta = std::clamp (0.5f * (s0 - s2) / denom, -1.0f, 1.0f);
    return static_cast<float> (i) + delta;
}
} // namespace

PitchDetector::PitchDetector() = default;
PitchDetector::~PitchDetector() = default;

//==============================================================================
void PitchDetector::prepare (double sampleRate, Config newConfig)
{
    config = newConfig;
    config.medianLength = std::clamp (config.medianLength, 1, kMaxMedian);

    inputRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    decimation = 1;
    while (inputRate / (decimation * 2) >= kMinAnalysisRate)
        decimation *= 2;
    analysisRate = inputRate / decimation;
    baseHopSize = std::clamp (static_cast<int> (std::lround (analysisRate * config.hopMs / 1000.0)), 16, 512);
    hopSize = baseHopSize;

    if (decimation > 1)
    {
        // 4th-order Butterworth low-pass at 80 % of the analysis Nyquist.
        const double cutoff = 0.4 * analysisRate;
        antiAlias[0].setLowPass (inputRate, cutoff, 0.54119610);
        antiAlias[1].setLowPass (inputRate, cutoff, 1.30656296);
    }

    // Worst case geometry: lowest f0 of any profile.
    float lowestHz = 1.0e9f;
    for (int i = 0; i < kNumInputProfiles; ++i)
        lowestHz = std::min (lowestHz, getF0Range (inputProfileFromIndex (i)).minHz);

    const int maxTau = static_cast<int> (std::ceil (analysisRate / lowestHz)) + 2;
    const int maxFrame = 2 * maxTau + 2;
    const int maxOrder = std::clamp (nextPow2Order (maxFrame), kMinFftOrder, kMaxFftOrder);
    jassert (nextPow2Order (maxFrame) <= kMaxFftOrder);

    ffts.clear();
    for (int order = kMinFftOrder; order <= maxOrder; ++order)
        ffts.push_back (std::make_unique<juce::dsp::FFT> (order));

    const size_t fftFloats = static_cast<size_t> (2 << maxOrder);
    fftA.assign (fftFloats, 0.0f);
    fftB.assign (fftFloats, 0.0f);
    frame.assign (static_cast<size_t> (maxFrame), 0.0f);
    energyPrefix.assign (static_cast<size_t> (maxFrame) + 1, 0.0);
    diff.assign (static_cast<size_t> (maxTau) + 2, 0.0f);
    cmnd.assign (static_cast<size_t> (maxTau) + 2, 1.0f);
    nsdf.assign (static_cast<size_t> (maxTau) + 2, 0.0f);

    const int ringSize = 1 << nextPow2Order (maxFrame + 1024 + 1);
    ring.assign (static_cast<size_t> (ringSize), 0.0f);
    ringMask = ringSize - 1;

    setSensitivity (sensitivity);
    configureForProfile();
    reset();
}

void PitchDetector::reset() noexcept
{
    for (auto& f : antiAlias)
        f.reset();
    highPass.reset();
    for (auto& f : analysisLowPass)
        f.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    hopCrossings.fill (0);
    hopCrossingsWrite = crossingsThisHop = 0;
    previousHp = 0.0f;
    ringWrite = samplesSinceHop = samplesInRing = decimationPhase = 0;
    inputSamplesSeen = 0;

    current = {};
    lastRawTau = 0.0f;
    octaveChallengeFrames = 0;
    medianCount = medianWrite = 0;
    voicedRun = unvoicedRun = 0;
    voicedState = false;
}

void PitchDetector::setProfile (InputProfile newProfile) noexcept
{
    if (newProfile == profile)
        return;

    profile = newProfile;
    configureForProfile();

    // New search range: forget pitch tracking history (keep the audio ring).
    lastRawTau = 0.0f;
    octaveChallengeFrames = 0;
    medianCount = medianWrite = 0;
}

void PitchDetector::setSensitivity (float sensitivity0to100) noexcept
{
    sensitivity = std::clamp (sensitivity0to100, 0.0f, 100.0f);
    const float s = sensitivity / 100.0f;

    // 0 -> strict (0.06), 50 -> classic YIN (0.15), 100 -> permissive (0.40).
    yinThreshold = s < 0.5f ? 0.06f + (0.15f - 0.06f) * (s * 2.0f)
                            : 0.15f + (0.40f - 0.15f) * ((s - 0.5f) * 2.0f);
    voicingThreshold = std::min (0.6f, yinThreshold + 0.12f);
    gateDb = -45.0f - 20.0f * s;          // -45 dBFS (strict) .. -65 dBFS (permissive)
    zcrLimitHz = 2500.0f + 3000.0f * s;   // breath / sibilant rejection
}

void PitchDetector::configureForProfile() noexcept
{
    if (ffts.empty())
        return; // not prepared yet

    const auto range = getF0Range (profile);
    tauMin = std::max (2, static_cast<int> (std::floor (analysisRate / range.maxHz)));
    tauMax = static_cast<int> (std::ceil (analysisRate / range.minHz)) + 1;
    tauMax = std::min (tauMax, static_cast<int> (diff.size()) - 2);
    integrationLength = tauMax;
    frameLength = integrationLength + tauMax + 2;

    // Very low ranges (long windows) don't need a frame every 2.7 ms: cap the
    // overlap at ~12 frames per window to bound the CPU cost.
    hopSize = std::clamp (std::max (baseHopSize, frameLength / 12), 16, 1024);
    samplesSinceHop = std::min (samplesSinceHop, hopSize - 1);

    const int order = std::clamp (nextPow2Order (frameLength), kMinFftOrder,
                                  kMinFftOrder + static_cast<int> (ffts.size()) - 1);
    fftIndex = order - kMinFftOrder;

    highPass.setHighPass (analysisRate, std::max (15.0, 0.5 * static_cast<double> (range.minHz)), 0.70710678);

    // Only the first few harmonics matter for f0: low-pass the analysis signal to
    // keep broadband noise (breath, room, hiss) out of the difference function.
    const double lpCutoff = std::clamp (4.0 * static_cast<double> (range.maxHz), 2000.0, 0.45 * analysisRate);
    analysisLowPass[0].setLowPass (analysisRate, lpCutoff, 0.54119610);
    analysisLowPass[1].setLowPass (analysisRate, lpCutoff, 1.30656296);
}

//==============================================================================
int PitchDetector::process (const float* mono, int numSamples) noexcept
{
    if (ffts.empty() || mono == nullptr)
        return 0;

    int frames = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        float x = mono[i];
        if (! std::isfinite (x))
            x = 0.0f;

        ++inputSamplesSeen;

        if (decimation > 1)
        {
            x = antiAlias[1].process (antiAlias[0].process (x));
            if (++decimationPhase < decimation)
                continue;
            decimationPhase = 0;
        }

        const float hp = highPass.process (x);
        if ((hp >= 0.0f) != (previousHp >= 0.0f))
            ++crossingsThisHop;
        previousHp = hp;

        const float filtered = analysisLowPass[1].process (analysisLowPass[0].process (hp));
        ring[static_cast<size_t> (ringWrite)] = filtered;
        ringWrite = (ringWrite + 1) & ringMask;
        samplesInRing = std::min (samplesInRing + 1, ringMask + 1);

        if (++samplesSinceHop >= hopSize)
        {
            samplesSinceHop = 0;
            hopCrossings[static_cast<size_t> (hopCrossingsWrite)] = crossingsThisHop;
            hopCrossingsWrite = (hopCrossingsWrite + 1) % kMaxHopsPerFrame;
            crossingsThisHop = 0;

            if (samplesInRing >= frameLength)
            {
                analyseFrame();
                ++frames;
            }
        }
    }
    return frames;
}

//==============================================================================
void PitchDetector::computeDifferenceFunctions (const float* x) noexcept
{
    const int fftSize = 1 << (fftIndex + kMinFftOrder);
    auto& fft = *ffts[static_cast<size_t> (fftIndex)];
    const int W = integrationLength;
    const int N = frameLength;

    // Cross-correlation r(τ) = Σ_{j<W} x[j] x[j+τ] via FFT: IFFT(conj(A) · B).
    std::fill (fftA.begin(), fftA.begin() + 2 * fftSize, 0.0f);
    std::fill (fftB.begin(), fftB.begin() + 2 * fftSize, 0.0f);
    std::copy (x, x + W, fftA.begin());
    std::copy (x, x + N, fftB.begin());

    fft.performRealOnlyForwardTransform (fftA.data(), true);
    fft.performRealOnlyForwardTransform (fftB.data(), true);

    for (int k = 0; k <= fftSize / 2; ++k)
    {
        const size_t re = static_cast<size_t> (2 * k), im = re + 1;
        const float ar = fftA[re], ai = fftA[im];
        const float br = fftB[re], bi = fftB[im];
        fftA[re] = ar * br + ai * bi;
        fftA[im] = ar * bi - ai * br;
    }
    fft.performRealOnlyInverseTransform (fftA.data()); // JUCE normalises by 1/N

    energyPrefix[0] = 0.0;
    for (int i = 0; i < N; ++i)
        energyPrefix[static_cast<size_t> (i + 1)] = energyPrefix[static_cast<size_t> (i)]
                                                    + static_cast<double> (x[i]) * x[i];

    const double e1 = energyPrefix[static_cast<size_t> (W)];
    double running = 0.0;
    cmnd[0] = 1.0f;

    for (int tau = 0; tau <= tauMax + 1; ++tau)
    {
        const size_t t = static_cast<size_t> (tau);
        const double e2 = energyPrefix[t + static_cast<size_t> (W)] - energyPrefix[t];
        const double m = e1 + e2;
        const double d = std::max (0.0, m - 2.0 * static_cast<double> (fftA[t]));

        diff[t] = static_cast<float> (d);
        nsdf[t] = m > kTiny ? static_cast<float> (1.0 - d / m) : 0.0f;

        if (tau > 0)
        {
            running += d;
            cmnd[t] = running > kTiny ? static_cast<float> (d * tau / running) : 1.0f;
        }
    }
}

int PitchDetector::findFirstDip (float threshold) const noexcept
{
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (cmnd[static_cast<size_t> (tau)] < threshold)
        {
            // Minimum of the whole dip under the threshold (not just the first local
            // minimum: with noise, small ripples appear on the way down).
            int best = tau;
            while (tau + 1 <= tauMax && cmnd[static_cast<size_t> (tau + 1)] < threshold)
            {
                ++tau;
                if (cmnd[static_cast<size_t> (tau)] < cmnd[static_cast<size_t> (best)])
                    best = tau;
            }
            // ...and if the dip keeps going downhill, follow it.
            while (best + 1 <= tauMax && cmnd[static_cast<size_t> (best + 1)] < cmnd[static_cast<size_t> (best)])
                ++best;
            return best;
        }
    }
    return -1;
}

float PitchDetector::pickYin (float& aperiodicityOut) const noexcept
{
    int best = findFirstDip (yinThreshold);

    if (best < 0)
    {
        // No dip under the threshold (noisy frame). The global minimum is often a
        // sub-harmonic (2T, 3T...), so take the first dip that is nearly as deep.
        int globalMin = tauMin;
        for (int tau = tauMin + 1; tau <= tauMax; ++tau)
            if (cmnd[static_cast<size_t> (tau)] < cmnd[static_cast<size_t> (globalMin)])
                globalMin = tau;

        const float g = cmnd[static_cast<size_t> (globalMin)];
        best = findFirstDip (g * 1.3f + 0.02f);
        if (best < 0)
            best = globalMin;
    }

    // Sub-multiple check: if a period T/k is almost as periodic as the chosen one,
    // the chosen one is a sub-harmonic (octave-down error, typical in noise).
    for (int k = 4; k >= 2; --k)
    {
        const int center = static_cast<int> (std::lround (static_cast<float> (best) / static_cast<float> (k)));
        if (center < tauMin)
            continue;

        const int radius = std::max (2, center / 20);
        int candidate = std::max (tauMin, center - radius);
        for (int tau = candidate + 1; tau <= std::min (tauMax, center + radius); ++tau)
            if (cmnd[static_cast<size_t> (tau)] < cmnd[static_cast<size_t> (candidate)])
                candidate = tau;

        if (cmnd[static_cast<size_t> (candidate)] < cmnd[static_cast<size_t> (best)] * 1.25f + 0.04f)
        {
            best = candidate;
            break;
        }
    }

    aperiodicityOut = cmnd[static_cast<size_t> (best)];
    // Refine on d(τ) (unbiased by the cumulative normalisation).
    return parabolicVertex (diff, best, 1, tauMax + 1);
}

float PitchDetector::pickMpm (float& clarityOut) const noexcept
{
    std::array<int, kMaxMpmPeaks> peaks {};
    int numPeaks = 0;

    bool started = false, inLobe = false;
    int lobeMax = -1;
    float globalMax = 0.0f;

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        const float v = nsdf[static_cast<size_t> (tau)];
        const float prev = nsdf[static_cast<size_t> (tau - 1)];

        if (! started)
        {
            if (v < 0.0f)
                started = true;
            continue;
        }

        if (! inLobe && prev <= 0.0f && v > 0.0f)
        {
            inLobe = true;
            lobeMax = tau;
        }
        else if (inLobe && v > 0.0f)
        {
            if (v > nsdf[static_cast<size_t> (lobeMax)])
                lobeMax = tau;
        }
        else if (inLobe && v <= 0.0f)
        {
            inLobe = false;
            if (lobeMax >= tauMin && numPeaks < kMaxMpmPeaks)
            {
                peaks[static_cast<size_t> (numPeaks++)] = lobeMax;
                globalMax = std::max (globalMax, nsdf[static_cast<size_t> (lobeMax)]);
            }
        }
    }
    if (inLobe && lobeMax >= tauMin && numPeaks < kMaxMpmPeaks)
    {
        peaks[static_cast<size_t> (numPeaks++)] = lobeMax;
        globalMax = std::max (globalMax, nsdf[static_cast<size_t> (lobeMax)]);
    }

    clarityOut = 0.0f;
    if (numPeaks == 0 || globalMax <= 0.0f)
        return 0.0f;

    const float threshold = 0.9f * globalMax;
    for (int i = 0; i < numPeaks; ++i)
    {
        const int p = peaks[static_cast<size_t> (i)];
        if (nsdf[static_cast<size_t> (p)] >= threshold)
        {
            clarityOut = nsdf[static_cast<size_t> (p)];
            return parabolicVertex (nsdf, p, 1, tauMax + 1);
        }
    }
    return 0.0f;
}

float PitchDetector::aperiodicityAt (float tau) const noexcept
{
    const int lo = std::clamp (static_cast<int> (std::floor (tau)), 1, tauMax + 1);
    const int hi = std::clamp (lo + 1, 1, tauMax + 1);
    return std::min (cmnd[static_cast<size_t> (lo)], cmnd[static_cast<size_t> (hi)]);
}

float PitchDetector::refineLocalMinimum (int tauGuess, int radius) const noexcept
{
    const int lo = std::max (1, tauGuess - radius);
    const int hi = std::min (tauMax, tauGuess + radius);
    if (lo > hi)
        return 0.0f;

    int best = lo;
    for (int tau = lo + 1; tau <= hi; ++tau)
        if (diff[static_cast<size_t> (tau)] < diff[static_cast<size_t> (best)])
            best = tau;
    return parabolicVertex (diff, best, 1, tauMax + 1);
}

PitchDetector::RawResult PitchDetector::chooseCandidate (float yinTau, float yinAp,
                                                         float mpmTau, float mpmClarity) noexcept
{
    const float yinHz = yinTau > 0.0f ? static_cast<float> (analysisRate / yinTau) : 0.0f;
    const float mpmHz = mpmTau > 0.0f ? static_cast<float> (analysisRate / mpmTau) : 0.0f;

    float tau = 0.0f;
    switch (config.algorithm)
    {
        case Algorithm::Yin:
            tau = yinTau;
            break;

        case Algorithm::Mpm:
            tau = mpmClarity > 0.0f ? mpmTau : 0.0f;
            break;

        case Algorithm::Hybrid:
        {
            tau = yinTau;

            // YIN and MPM disagree by (about) an octave or more: keep the shorter
            // period unless it is clearly less periodic than the longer one.
            if (yinTau > 0.0f && mpmTau > 0.0f
                && std::abs (centsBetween (mpmTau, yinTau)) > 600.0f)
            {
                const float a = std::min (yinTau, mpmTau);
                const float b = std::max (yinTau, mpmTau);
                tau = aperiodicityAt (a) <= aperiodicityAt (b) + 0.03f ? a : b;
            }

            // Octave hysteresis: an octave jump must persist before it is accepted.
            if (tau > 0.0f && lastRawTau > 0.0f)
            {
                const float c = centsBetween (tau, lastRawTau);
                const bool octaveJump = std::abs (std::abs (c) - 1200.0f) < kOctaveToleranceCents;

                if (octaveJump)
                {
                    const float prevTau = refineLocalMinimum (static_cast<int> (std::lround (lastRawTau)), 3);
                    if (prevTau >= static_cast<float> (tauMin) && prevTau <= static_cast<float> (tauMax)
                        && aperiodicityAt (prevTau) < voicingThreshold
                        && ++octaveChallengeFrames < kOctaveConfirmFrames)
                    {
                        tau = prevTau;
                    }
                    else
                    {
                        octaveChallengeFrames = 0;
                    }
                }
                else
                {
                    octaveChallengeFrames = 0;
                }
            }
            break;
        }
    }

    juce::ignoreUnused (yinAp);
    const float ap = tau > 0.0f ? aperiodicityAt (tau) : 1.0f;
    const float chosenHz = tau > 0.0f ? static_cast<float> (analysisRate / tau) : 0.0f;
    return { yinHz, mpmHz, chosenHz, ap };
}

float PitchDetector::medianMidi() const noexcept
{
    std::array<float, kMaxMedian> tmp {};
    const int n = std::min (medianCount, config.medianLength);
    for (int i = 0; i < n; ++i)
        tmp[static_cast<size_t> (i)] = medianBuf[static_cast<size_t> (i)];
    for (int i = 1; i < n; ++i) // insertion sort, n <= kMaxMedian
        for (int j = i; j > 0 && tmp[static_cast<size_t> (j - 1)] > tmp[static_cast<size_t> (j)]; --j)
            std::swap (tmp[static_cast<size_t> (j - 1)], tmp[static_cast<size_t> (j)]);
    return (n % 2 == 1) ? tmp[static_cast<size_t> (n / 2)]
                        : 0.5f * (tmp[static_cast<size_t> (n / 2 - 1)] + tmp[static_cast<size_t> (n / 2)]);
}

//==============================================================================
void PitchDetector::analyseFrame() noexcept
{
    const int start = (ringWrite - frameLength) & ringMask;
    for (int i = 0; i < frameLength; ++i)
        frame[static_cast<size_t> (i)] = ring[static_cast<size_t> ((start + i) & ringMask)];

    // Level of the window.
    double sumSq = 0.0;
    for (int i = 0; i < frameLength; ++i)
    {
        const float v = frame[static_cast<size_t> (i)];
        sumSq += static_cast<double> (v) * v;
    }
    frameRmsDb = static_cast<float> (10.0 * std::log10 (sumSq / frameLength + 1.0e-12));

    // Zero-crossing rate of the broadband (high-passed) signal over ~the window.
    const int hops = std::clamp ((frameLength + hopSize - 1) / hopSize, 1, kMaxHopsPerFrame);
    int crossings = 0;
    for (int h = 1; h <= hops; ++h)
        crossings += hopCrossings[static_cast<size_t> ((hopCrossingsWrite - h + kMaxHopsPerFrame) % kMaxHopsPerFrame)];
    frameZcrHz = static_cast<float> (crossings * analysisRate / (2.0 * hops * hopSize));

    computeDifferenceFunctions (frame.data());

    float yinAp = 1.0f, clarity = 0.0f;
    const float yinTau = pickYin (yinAp);
    const float mpmTau = pickMpm (clarity);
    const auto raw = chooseCandidate (yinTau, yinAp, mpmTau, clarity);

    const bool periodic = raw.chosenHz > 0.0f && raw.aperiodicity <= voicingThreshold;
    const bool loudEnough = frameRmsDb > gateDb;
    // Sibilants / breath: many zero crossings and weak periodicity.
    const bool noisy = frameZcrHz > std::max (zcrLimitHz, 3.0f * raw.chosenHz)
                       && raw.aperiodicity > 0.5f * voicingThreshold;
    const bool frameVoiced = periodic && loudEnough && ! noisy;

    if (frameVoiced)
    {
        ++voicedRun;
        unvoicedRun = 0;
        medianBuf[static_cast<size_t> (medianWrite)] = hzToMidi (raw.chosenHz);
        medianWrite = (medianWrite + 1) % config.medianLength;
        medianCount = std::min (medianCount + 1, config.medianLength);
        lastRawTau = static_cast<float> (analysisRate / raw.chosenHz);
    }
    else
    {
        ++unvoicedRun;
        voicedRun = 0;
    }

    if (! voicedState && voicedRun >= kVoicedOnsetFrames)
        voicedState = true;

    if (unvoicedRun >= kVoicedReleaseFrames)
    {
        voicedState = false;
        medianCount = medianWrite = 0;
        lastRawTau = 0.0f;
        octaveChallengeFrames = 0;
    }

    current.rawFrequencyHz = frameVoiced ? raw.chosenHz : 0.0f;
    current.voiced = voicedState && medianCount > 0;
    current.frequencyHz = current.voiced ? midiToHz (medianMidi()) : 0.0f;
    current.confidence = std::clamp (1.0f - raw.aperiodicity, 0.0f, 1.0f);
    current.rmsDb = frameRmsDb;
    current.sampleIndex = inputSamplesSeen;

    if (listener != nullptr)
        listener->pitchFrameReady (current);
}

PitchDetector::RawResult PitchDetector::analyseBuffer (const float* samples, int numSamples) noexcept
{
    if (ffts.empty() || numSamples < frameLength)
        return { 0.0f, 0.0f, 0.0f, 1.0f };

    const float* x = samples + (numSamples - frameLength);
    std::copy (x, x + frameLength, frame.begin());
    computeDifferenceFunctions (frame.data());

    float yinAp = 1.0f, clarity = 0.0f;
    const float yinTau = pickYin (yinAp);
    const float mpmTau = pickMpm (clarity);

    const float savedTau = lastRawTau;
    lastRawTau = 0.0f; // stateless analysis
    const auto result = chooseCandidate (yinTau, yinAp, mpmTau, clarity);
    lastRawTau = savedTau;
    return result;
}

} // namespace novapitch::dsp
