#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"
#include "dsp/PsolaShifter.h"
#include "signals/SignalGenerators.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace novapitch;
using namespace novapitch::dsp;

namespace
{
/** Median f0 of the voiced frames after `settle` seconds. */
float medianPitch (const std::vector<float>& sig, double sr, double settle)
{
    struct Collector final : PitchDetector::Listener
    {
        std::vector<float> f;
        double sr = 48000.0, settle = 0.0;
        void pitchFrameReady (const PitchEstimate& e) noexcept override
        {
            if (e.voiced && static_cast<double> (e.sampleIndex) / sr > settle)
                f.push_back (e.frequencyHz);
        }
    } c;
    c.sr = sr;
    c.settle = settle;

    PitchDetector det;
    det.prepare (sr);
    det.setProfile (InputProfile::Instrument);
    det.setListener (&c);
    det.process (sig.data(), static_cast<int> (sig.size()));
    if (c.f.empty())
        return 0.0f;
    std::nth_element (c.f.begin(), c.f.begin() + static_cast<long> (c.f.size() / 2), c.f.end());
    return c.f[c.f.size() / 2];
}

std::vector<float> shift (const std::vector<float>& in, double sr, int latency, bool voiced, float f0, float ratio)
{
    PsolaShifter s;
    s.prepare (sr, latency);
    s.setLatency (latency);
    s.reset();
    s.setControl (voiced, static_cast<float> (sr / f0), ratio);
    std::vector<float> out (in);
    for (size_t pos = 0; pos < out.size(); pos += 64)
    {
        const int n = static_cast<int> (std::min<size_t> (64, out.size() - pos));
        float* ch[] = { out.data() + pos };
        s.process (ch, 1, in.data() + pos, n);
    }
    return out;
}

double rms (const std::vector<float>& v, size_t from)
{
    double s = 0.0;
    for (size_t i = from; i < v.size(); ++i)
        s += static_cast<double> (v[i]) * v[i];
    return std::sqrt (s / static_cast<double> (v.size() - from));
}
} // namespace

TEST_CASE ("PSOLA shifts pitch by the requested ratio", "[psola]")
{
    const double sr = 48000.0;
    for (const float semis : { -12.0f, -5.0f, -1.0f, 0.35f, 1.0f, 7.0f, 12.0f })
    {
        const float ratio = std::exp2 (semis / 12.0f);
        const float f0 = 196.0f;
        const auto in = signals::sawtooth (f0, sr, 1.0);
        const auto out = shift (in, sr, 480, true, f0, ratio);
        const float measured = medianPitch (out, sr, 0.3);
        INFO ("semitones " << semis << " measured " << measured << " expected " << f0 * ratio);
        CHECK (std::abs (centsBetween (measured, f0 * ratio)) < 3.0f);

        // Level stays sensible (PSOLA keeps energy per pulse).
        const double ratioDb = 20.0 * std::log10 (rms (out, 24000) / rms (in, 24000));
        CHECK (ratioDb > -8.0);
        CHECK (ratioDb < 4.0);
    }
}

TEST_CASE ("PSOLA works with Live latency too", "[psola]")
{
    const double sr = 44100.0;
    const auto in = signals::sawtooth (110.0, sr, 1.0);
    const auto out = shift (in, sr, 132, true, 110.0f, std::exp2 (2.0f / 12.0f));
    CHECK (std::abs (centsBetween (medianPitch (out, sr, 0.3), 110.0f * std::exp2 (2.0f / 12.0f))) < 3.0f);
}

TEST_CASE ("Unvoiced input passes through delayed by the latency", "[psola]")
{
    const double sr = 48000.0;
    const int latency = 480;
    const auto in = signals::whiteNoise (sr, 0.5, 0.5f, 3u);
    const auto out = shift (in, sr, latency, false, 100.0f, 1.7f);
    for (size_t i = static_cast<size_t> (latency); i < in.size(); ++i)
        REQUIRE (out[i] == in[i - static_cast<size_t> (latency)]);
}
