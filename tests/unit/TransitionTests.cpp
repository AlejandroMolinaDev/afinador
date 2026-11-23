// Hard-tune note transitions: the output must follow a sung note change at
// once, not keep the previous note until the pitch detector catches up.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"
#include "dsp/PitchUtils.h"
#include "signals/SignalGenerators.h"

#include <cmath>
#include <vector>

using namespace novapitch;
using namespace novapitch::test;

namespace
{
/** Pitch from an 8 ms window (fine time resolution): normalised autocorrelation. */
double shortPitch (const std::vector<float>& x, size_t centre, double sr)
{
    const int win = static_cast<int> (0.008 * sr), minLag = static_cast<int> (sr / 600.0), maxLag = static_cast<int> (sr / 150.0);
    const size_t from = centre - static_cast<size_t> (win / 2);
    std::vector<double> r (static_cast<size_t> (maxLag + 2), 0.0);
    double best = -1.0;
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int i = 0; i < win; ++i)
        {
            const double a = x[from + static_cast<size_t> (i)], b = x[from + static_cast<size_t> (i + lag)];
            xy += a * b;
            xx += a * a;
            yy += b * b;
        }
        r[static_cast<size_t> (lag)] = xy / std::sqrt (xx * yy + 1.0e-12);
        if (lag <= maxLag)
            best = std::max (best, r[static_cast<size_t> (lag)]);
    }
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
    {
        const double a = r[static_cast<size_t> (lag - 1)], b = r[static_cast<size_t> (lag)], c = r[static_cast<size_t> (lag + 1)];
        if (b >= 0.9 * best && b >= a && b >= c)
        {
            const double d = a - 2.0 * b + c;
            return sr / (lag + (std::abs (d) > 1.0e-12 ? 0.5 * (a - c) / d : 0.0));
        }
    }
    return 0.0;
}

/** Time after (step + latency) from which the output stays within `tol` cents of targetHz for 30 ms. */
double settleTime (const std::vector<float>& out, double sr, double stepAt, double latency, double targetHz, double tol)
{
    for (double t = stepAt + latency - 0.002; t < stepAt + latency + 0.08; t += 0.0005)
    {
        bool ok = true;
        for (double u = t; u < t + 0.03 && ok; u += 0.001)
            ok = std::abs (1200.0 * std::log2 (shortPitch (out, static_cast<size_t> (u * sr), sr) / targetHz)) <= tol;
        if (ok)
            return t - (stepAt + latency);
    }
    return 1.0;
}
} // namespace

TEST_CASE ("Hard tune follows a sung note change at once (Live, Tracking, Vintage)", "[correction][transition]")
{
    JuceFixture fixture;
    const double sr = 48000.0;
    const int mode = GENERATE (0, 1, 3);
    // A3 +30 c -> C4 +30 c (same intonation error) and A3 +30 c -> C4 -20 c.
    const double secondCents = GENERATE (30.0, -20.0);

    const auto sig = signals::concat ({ signals::sawtooth (220.0 * std::exp2 (30.0 / 1200.0), sr, 0.5),
                                        signals::sawtooth (261.63 * std::exp2 (secondCents / 1200.0), sr, 0.5) });
    auto p = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
    REQUIRE (p != nullptr);
    setParam (*p, ParamIDs::qualityMode, static_cast<float> (mode));
    setParam (*p, ParamIDs::retuneSpeed, 0.0f);
    setParam (*p, ParamIDs::vibratoPreserve, 0.0f);
    p->prepareToPlay (sr, 256);

    std::vector<float> out (sig.size());
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + 256 <= sig.size(); pos += 256)
    {
        buffer.copyFrom (0, 0, sig.data() + pos, 256);
        buffer.copyFrom (1, 0, sig.data() + pos, 256);
        p->processBlock (buffer, midi);
        std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + 256, out.begin() + static_cast<long> (pos));
    }

    const double latency = static_cast<double> (p->getLatencySamples()) / sr;
    // Within a semitone of the new note almost at once (the previous ratio is
    // applied to the new note until the detector confirms it)...
    const double near = settleTime (out, sr, 0.5, latency, 261.63, 60.0);
    // ...and exactly on it once the detector has caught up.
    const double exact = settleTime (out, sr, 0.5, latency, 261.63, 15.0);
    INFO ("mode " << mode << ", second note " << secondCents << " c: within 60 c after " << near * 1000.0
                  << " ms, within 15 c after " << exact * 1000.0 << " ms");
    CHECK (near <= 0.010);
    CHECK (exact <= 0.035);
}
