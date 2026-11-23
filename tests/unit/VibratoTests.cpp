#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"
#include "dsp/VibratoAnalyzer.h"
#include "dsp/VibratoSynth.h"
#include "signals/SignalGenerators.h"

#include <cmath>
#include <numbers>
#include <random>
#include <vector>

using namespace novapitch;
using namespace novapitch::dsp;

namespace
{
constexpr double kFrameRate = 375.0;

struct Summary
{
    float rate = 0.0f, depth = 0.0f, maxWeight = 0.0f, maxAbsVib = 0.0f, maxCentreError = 0.0f;
};

/** Feeds a pitch curve (cents per frame) and summarises the analyser output after `settle` s. */
template <typename Curve>
Summary runCurve (Curve&& curve, double seconds, double settle, float trueCentre = NAN)
{
    VibratoAnalyzer a;
    a.prepare (kFrameRate);
    Summary s;
    const int frames = static_cast<int> (seconds * kFrameRate);
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / kFrameRate;
        const auto r = a.process (true, curve (t));
        if (t < settle)
            continue;
        s.maxWeight = std::max (s.maxWeight, r.weight);
        s.maxAbsVib = std::max (s.maxAbsVib, std::abs (r.vibratoCents));
        if (std::isfinite (trueCentre))
            s.maxCentreError = std::max (s.maxCentreError, std::abs (r.centreCents - trueCentre));
        if (r.rateHz > 0.0f)
        {
            s.rate = r.rateHz;
            s.depth = r.depthCents;
        }
    }
    return s;
}
} // namespace

TEST_CASE ("Vibrato analyser measures rate and depth within 10 %", "[vibrato]")
{
    const float rate = GENERATE (4.0f, 5.5f, 7.0f);
    const float depth = GENERATE (25.0f, 50.0f, 120.0f);
    const auto s = runCurve ([&] (double t) { return 5700.0f + depth * static_cast<float> (std::sin (2.0 * std::numbers::pi * rate * t)); },
                             2.0, 0.8, 5700.0f);
    INFO ("rate " << rate << " -> " << s.rate << ", depth " << depth << " -> " << s.depth << ", centre error " << s.maxCentreError);
    CHECK (std::abs (s.rate / rate - 1.0f) < 0.10f);
    CHECK (std::abs (s.depth / depth - 1.0f) < 0.10f);
    CHECK (s.maxWeight > 0.95f);
    CHECK (s.maxCentreError < 0.2f * depth); // centre stays near the note
}

TEST_CASE ("Vibrato analyser on real audio through the pitch detector (5.5 Hz, +-50 c)", "[vibrato]")
{
    const double sr = 48000.0;
    const auto audio = signals::vibratoSine (220.0, 5.5, 50.0, sr, 2.0);

    PitchDetector det;
    det.prepare (sr);
    VibratoAnalyzer a;
    a.prepare (det.getFrameRate());

    struct L final : PitchDetector::Listener
    {
        VibratoAnalyzer* a = nullptr;
        double sr = 48000.0;
        float rate = 0.0f, depth = 0.0f;
        void pitchFrameReady (const PitchEstimate& e) noexcept override
        {
            const auto r = a->process (e.voiced, e.voiced ? 100.0f * hzToMidi (e.frequencyHz) : 0.0f);
            if (e.sampleIndex / sr > 1.0 && r.rateHz > 0.0f)
            {
                rate = r.rateHz;
                depth = r.depthCents;
            }
        }
    } l;
    l.a = &a;
    l.sr = sr;
    det.setListener (&l);
    det.process (audio.data(), static_cast<int> (audio.size()));

    INFO ("rate " << l.rate << " depth " << l.depth);
    CHECK (std::abs (l.rate / 5.5f - 1.0f) < 0.10f);
    CHECK (std::abs (l.depth / 50.0f - 1.0f) < 0.10f);
}

TEST_CASE ("Glides, note changes and jitter are not vibrato", "[vibrato]")
{
    SECTION ("slow glide")
    {
        const auto s = runCurve ([] (double t) { return 5700.0f + 200.0f * static_cast<float> (t); }, 1.5, 0.5);
        CHECK (s.maxWeight < 0.2f);
    }
    SECTION ("note steps")
    {
        const auto s = runCurve ([] (double t) { return t < 0.6 ? 5700.0f : (t < 1.1 ? 5900.0f : 5800.0f); }, 1.6, 0.2);
        CHECK (s.maxWeight < 0.3f);
        CHECK (s.maxAbsVib < 10.0f);
    }
    SECTION ("detector jitter (±3 cents)")
    {
        std::mt19937 rng (1u);
        std::uniform_real_distribution<float> d (-3.0f, 3.0f);
        const auto s = runCurve ([&] (double) { return 5700.0f + d (rng); }, 1.5, 0.5);
        CHECK (s.maxWeight < 0.2f);
    }
    SECTION ("unvoiced resets")
    {
        VibratoAnalyzer a;
        a.prepare (kFrameRate);
        for (int i = 0; i < 600; ++i)
            a.process (true, 5700.0f + 50.0f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 5.5 * i / kFrameRate)));
        const auto r = a.process (false, 0.0f);
        CHECK (r.weight == 0.0f);
        CHECK (r.vibratoCents == 0.0f);
    }
}

//==============================================================================
TEST_CASE ("Vibrato synth: rate, depth, delay and note restart", "[vibrato][synth]")
{
    VibratoSynth s;
    s.prepare (kFrameRate);
    VibratoSynth::Settings set;
    set.rateHz = 5.0f;
    set.depthCents = 30.0f;
    set.delayMs = 500.0f;
    set.variation = 0.0f;
    set.amplitudeMod = 1.0f;
    set.formantMod = 1.0f;
    s.setSettings (set);

    float maxBeforeDelay = 0.0f, maxAfter = 0.0f, minGain = 2.0f, maxGain = 0.0f, maxFormant = 0.0f;
    int crossings = 0;
    float prev = 0.0f;
    for (int i = 0; i < static_cast<int> (3.0 * kFrameRate); ++i)
    {
        const double t = i / kFrameRate;
        const auto o = s.process (true, i == 0);
        if (t < 0.49)
            maxBeforeDelay = std::max (maxBeforeDelay, std::abs (o.pitchCents));
        if (t > 1.0)
        {
            maxAfter = std::max (maxAfter, std::abs (o.pitchCents));
            minGain = std::min (minGain, o.gain);
            maxGain = std::max (maxGain, o.gain);
            maxFormant = std::max (maxFormant, o.formantFactor);
            if ((o.pitchCents >= 0.0f) != (prev >= 0.0f))
                ++crossings;
        }
        prev = o.pitchCents;
    }
    CHECK (maxBeforeDelay < 0.01f);
    CHECK (std::abs (maxAfter - 30.0f) < 1.0f);
    CHECK (std::abs (crossings / 2.0f / 2.0f - 5.0f) < 0.5f); // 2 s after t = 1 s
    CHECK (minGain < 0.8f);
    CHECK (maxGain > 1.2f);
    CHECK (maxFormant > 1.01f);

    // A new note restarts the delay; unvoiced is neutral.
    CHECK (std::abs (s.process (true, true).pitchCents) < 0.01f);
    const auto off = s.process (false, false);
    CHECK (off.pitchCents == 0.0f);
    CHECK (off.gain == 1.0f);
}

TEST_CASE ("Vibrato synth variation stays bounded", "[vibrato][synth]")
{
    VibratoSynth s;
    s.prepare (kFrameRate);
    VibratoSynth::Settings set;
    set.depthCents = 40.0f;
    set.delayMs = 0.0f;
    set.variation = 1.0f;
    s.setSettings (set);
    float maxAbs = 0.0f;
    for (int i = 0; i < 4000; ++i)
        maxAbs = std::max (maxAbs, std::abs (s.process (true, i == 0).pitchCents));
    CHECK (maxAbs > 30.0f);
    CHECK (maxAbs <= 40.0f * 1.3f + 0.01f);
}
