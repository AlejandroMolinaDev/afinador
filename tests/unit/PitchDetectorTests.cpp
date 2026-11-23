#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"
#include "signals/SignalGenerators.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace novapitch;
using namespace novapitch::dsp;

namespace
{
struct FrameCollector final : PitchDetector::Listener
{
    std::vector<PitchEstimate> frames;
    void pitchFrameReady (const PitchEstimate& e) noexcept override { frames.push_back (e); }
};

/** Feeds `signal` in fixed chunks and returns every analysis frame. */
std::vector<PitchEstimate> runDetector (PitchDetector& det, const signals::Buffer& signal, int chunk = 256)
{
    FrameCollector collector;
    collector.frames.reserve (signal.size() / 32 + 16);
    det.setListener (&collector);
    for (size_t pos = 0; pos < signal.size(); pos += static_cast<size_t> (chunk))
    {
        const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (chunk), signal.size() - pos));
        det.process (signal.data() + pos, n);
    }
    det.setListener (nullptr);
    return std::move (collector.frames);
}

struct ErrorStats
{
    int voicedFrames = 0;
    int consideredFrames = 0;
    float maxAbsCents = 0.0f;
};

/** Error of voiced frames whose window lies after `settleSeconds`. */
ErrorStats measure (const std::vector<PitchEstimate>& frames, double inputRate, double settleSeconds, float trueHz)
{
    ErrorStats s;
    for (const auto& f : frames)
    {
        if (static_cast<double> (f.sampleIndex) / inputRate < settleSeconds)
            continue;
        ++s.consideredFrames;
        if (! f.voiced)
            continue;
        ++s.voicedFrames;
        s.maxAbsCents = std::max (s.maxAbsCents, std::abs (centsBetween (f.frequencyHz, trueHz)));
    }
    return s;
}

std::vector<float> testFrequencies (InputProfile p)
{
    const auto r = getF0Range (p);
    const float lo = r.minHz * 1.08f, hi = r.maxHz * 0.95f;
    return { lo, std::sqrt (lo * hi), hi };
}
} // namespace

//==============================================================================
TEST_CASE ("Pitch utils", "[pitch][utils]")
{
    CHECK_THAT (hzToMidi (440.0f), Catch::Matchers::WithinAbs (69.0, 1e-4));
    CHECK_THAT (hzToMidi (261.6256f), Catch::Matchers::WithinAbs (60.0, 1e-3));
    CHECK_THAT (midiToHz (57.0f), Catch::Matchers::WithinAbs (220.0, 1e-3));
    CHECK_THAT (hzToMidi (432.0f, 432.0f), Catch::Matchers::WithinAbs (69.0, 1e-4));
    CHECK_THAT (centsBetween (880.0f, 440.0f), Catch::Matchers::WithinAbs (1200.0, 1e-2));
    CHECK_THAT (centsFromNearestNote (69.25f), Catch::Matchers::WithinAbs (25.0, 1e-3));
    CHECK (midiNoteName (69) == "A4");
    CHECK (midiNoteName (60) == "C4");
    CHECK (midiNoteName (61) == "C#4");
    CHECK (midiNoteName (23) == "B0");
}

TEST_CASE ("FFT difference function equals the direct YIN sum", "[pitch]")
{
    PitchDetector det;
    det.prepare (48000.0);
    det.setProfile (InputProfile::AltoTenor);

    const double ar = det.getAnalysisSampleRate();
    const auto sig = signals::sawtooth (196.0, ar, 0.2);
    det.analyseBuffer (sig.data(), static_cast<int> (sig.size()));

    const int N = det.getWindowLength();
    const auto& d = det.getDifferenceFunction();
    const int tauMax = static_cast<int> (std::ceil (ar / 80.0)) + 1;
    const int W = tauMax;
    // The detector high-passes in process(), but analyseBuffer() uses raw samples.
    const float* x = sig.data() + (sig.size() - static_cast<size_t> (N));

    double maxRel = 0.0;
    for (int tau = 1; tau <= tauMax; tau += 7)
    {
        double direct = 0.0;
        for (int j = 0; j < W; ++j)
        {
            const double e = static_cast<double> (x[j]) - x[j + tau];
            direct += e * e;
        }
        const double rel = std::abs (direct - d[static_cast<size_t> (tau)]) / (direct + 1.0);
        maxRel = std::max (maxRel, rel);
    }
    CHECK (maxRel < 1.0e-3);
}

TEST_CASE ("Sines are detected within 5 cents across profiles and sample rates", "[pitch][accuracy]")
{
    const double sr = GENERATE (44100.0, 48000.0, 96000.0, 192000.0);
    const int profileIndex = GENERATE (0, 1, 2, 3, 4);
    const auto profile = inputProfileFromIndex (profileIndex);

    for (const float f : testFrequencies (profile))
    {
        PitchDetector det;
        det.prepare (sr);
        det.setProfile (profile);
        const auto frames = runDetector (det, signals::sine (f, sr, 0.5));
        const auto s = measure (frames, sr, 0.2, f);

        INFO ("sr=" << sr << " profile=" << profileIndex << " f=" << f << " maxCents=" << s.maxAbsCents);
        CHECK (s.consideredFrames > 10);
        CHECK (s.voicedFrames == s.consideredFrames);
        CHECK (s.maxAbsCents < 5.0f);
    }
}

TEST_CASE ("Band-limited sawtooths are detected within 5 cents", "[pitch][accuracy]")
{
    const double sr = GENERATE (44100.0, 48000.0, 96000.0);
    const int profileIndex = GENERATE (0, 1, 2, 3, 4);
    const auto profile = inputProfileFromIndex (profileIndex);

    for (const float f : testFrequencies (profile))
    {
        PitchDetector det;
        det.prepare (sr);
        det.setProfile (profile);
        const auto frames = runDetector (det, signals::sawtooth (f, sr, 0.5));
        const auto s = measure (frames, sr, 0.2, f);

        INFO ("sr=" << sr << " profile=" << profileIndex << " f=" << f << " maxCents=" << s.maxAbsCents);
        CHECK (s.voicedFrames == s.consideredFrames);
        CHECK (s.maxAbsCents < 5.0f);
    }
}

TEST_CASE ("YIN and MPM alone are both accurate on sines", "[pitch][accuracy]")
{
    const auto algo = GENERATE (PitchDetector::Algorithm::Yin, PitchDetector::Algorithm::Mpm);
    for (const float f : { 110.0f, 220.0f, 440.0f, 800.0f })
    {
        PitchDetector det;
        PitchDetector::Config cfg;
        cfg.algorithm = algo;
        det.prepare (48000.0, cfg);
        const auto s = measure (runDetector (det, signals::sine (f, 48000.0, 0.4)), 48000.0, 0.15, f);
        INFO ("algo=" << static_cast<int> (algo) << " f=" << f << " maxCents=" << s.maxAbsCents);
        CHECK (s.voicedFrames == s.consideredFrames);
        CHECK (s.maxAbsCents < 5.0f);
    }
}

TEST_CASE ("Weak fundamental does not cause octave errors", "[pitch][octave]")
{
    for (const float f : { 110.0f, 165.0f, 220.0f, 330.0f })
    {
        PitchDetector det;
        det.prepare (48000.0);
        const auto s = measure (runDetector (det, signals::weakFundamental (f, 48000.0, 0.6)), 48000.0, 0.2, f);
        INFO ("f=" << f << " maxCents=" << s.maxAbsCents);
        CHECK (s.voicedFrames == s.consideredFrames);
        CHECK (s.maxAbsCents < 10.0f);
    }
}

TEST_CASE ("Noise and silence are unvoiced", "[pitch][voicing]")
{
    PitchDetector det;
    det.prepare (48000.0);

    SECTION ("silence")
    {
        const auto frames = runDetector (det, signals::silence (48000.0, 0.5));
        CHECK (std::none_of (frames.begin(), frames.end(), [] (const auto& f) { return f.voiced; }));
    }
    SECTION ("white noise (sibilant / breath)")
    {
        const auto frames = runDetector (det, signals::whiteNoise (48000.0, 0.5, 0.3f, 1u));
        const auto voiced = std::count_if (frames.begin(), frames.end(), [] (const auto& f) { return f.voiced; });
        CHECK (voiced <= static_cast<long> (frames.size() / 50));
    }
    SECTION ("very quiet sine below the gate")
    {
        const auto frames = runDetector (det, signals::sine (220.0, 48000.0, 0.5, 0.0005f));
        CHECK (std::none_of (frames.begin(), frames.end(), [] (const auto& f) { return f.voiced; }));
    }
}

TEST_CASE ("Sine in noise (~17 dB SNR) stays voiced and accurate", "[pitch][voicing]")
{
    PitchDetector det;
    det.prepare (48000.0);
    auto sig = signals::sine (220.0, 48000.0, 0.6, 0.5f);
    signals::addInPlace (sig, signals::whiteNoise (48000.0, 0.6, 0.5f * 0.1f * 1.732f, 7u));

    const auto s = measure (runDetector (det, sig), 48000.0, 0.2, 220.0f);
    CHECK (s.voicedFrames == s.consideredFrames);
    CHECK (s.maxAbsCents < 5.0f);
}

TEST_CASE ("Higher sensitivity accepts noisier signals", "[pitch][voicing]")
{
    auto sig = signals::sine (220.0, 48000.0, 0.6, 0.5f);
    signals::addInPlace (sig, signals::whiteNoise (48000.0, 0.6, 1.0f, 7u)); // ~ -6.6 dB SNR

    auto voicedRatio = [&] (float sensitivity)
    {
        PitchDetector det;
        det.prepare (48000.0);
        det.setSensitivity (sensitivity);
        const auto frames = runDetector (det, sig);
        const auto v = std::count_if (frames.begin(), frames.end(), [] (const auto& f) { return f.voiced; });
        return static_cast<double> (v) / static_cast<double> (frames.size());
    };

    CHECK (voicedRatio (0.0f) < 0.1);
    CHECK (voicedRatio (100.0f) > 0.9);

    PitchDetector det;
    det.setSensitivity (0.0f);
    const float strict = det.getYinThreshold();
    det.setSensitivity (100.0f);
    CHECK (det.getYinThreshold() > strict);
}

TEST_CASE ("Voiced / unvoiced transitions", "[pitch][voicing]")
{
    const double sr = 48000.0;
    const auto sig = signals::concat ({ signals::sine (220.0, sr, 0.4), signals::silence (sr, 0.3),
                                        signals::sine (330.0, sr, 0.4) });
    PitchDetector det;
    det.prepare (sr);
    const auto frames = runDetector (det, sig);

    auto stateAt = [&] (double t) {
        for (const auto& f : frames)
            if (static_cast<double> (f.sampleIndex) / sr >= t)
                return f;
        return frames.back();
    };

    CHECK (stateAt (0.35).voiced);
    CHECK_THAT (stateAt (0.35).frequencyHz, Catch::Matchers::WithinRel (220.0f, 0.003f));
    CHECK_FALSE (stateAt (0.6).voiced);
    CHECK (stateAt (1.05).voiced);
    CHECK_THAT (stateAt (1.05).frequencyHz, Catch::Matchers::WithinRel (330.0f, 0.003f));
}

TEST_CASE ("A real octave leap is followed quickly", "[pitch][octave]")
{
    const double sr = 48000.0;
    const auto sig = signals::concat ({ signals::sawtooth (200.0, sr, 0.4), signals::sawtooth (400.0, sr, 0.4) });
    PitchDetector det;
    det.prepare (sr);
    const auto frames = runDetector (det, sig);

    // Within 80 ms after the leap the detector must report the upper octave.
    bool reached = false;
    for (const auto& f : frames)
    {
        const double t = static_cast<double> (f.sampleIndex) / sr;
        if (t > 0.48 && f.voiced)
        {
            CHECK (std::abs (centsBetween (f.frequencyHz, 400.0f)) < 10.0f);
            reached = true;
        }
    }
    CHECK (reached);
}

TEST_CASE ("Slow glissando is tracked continuously", "[pitch]")
{
    const double sr = 48000.0;
    PitchDetector det;
    det.prepare (sr);
    const auto frames = runDetector (det, signals::glissando (150.0, 300.0, sr, 1.0));

    int checked = 0;
    for (const auto& f : frames)
    {
        const double t = static_cast<double> (f.sampleIndex) / sr;
        if (t < 0.2 || ! f.voiced)
            continue;
        // Detector reports the pitch around the centre of its window (lag ~ window / 2).
        const double lag = det.getWindowLength() * 0.5 / det.getAnalysisSampleRate();
        const double expected = 150.0 * std::pow (2.0, (t - lag) / 1.0);
        CHECK (std::abs (centsBetween (f.frequencyHz, static_cast<float> (expected))) < 15.0f);
        ++checked;
    }
    CHECK (checked > 100);
}

TEST_CASE ("Profile switch at runtime and NaN input are handled", "[pitch][robustness]")
{
    PitchDetector det;
    det.prepare (48000.0);

    auto sig = signals::sine (300.0, 48000.0, 0.3);
    sig[1000] = std::numeric_limits<float>::quiet_NaN();
    sig[2000] = std::numeric_limits<float>::infinity();
    runDetector (det, sig);

    for (int p = 0; p < kNumInputProfiles; ++p)
    {
        det.setProfile (inputProfileFromIndex (p));
        const auto frames = runDetector (det, signals::sine (300.0, 48000.0, 0.3));
        REQUIRE_FALSE (frames.empty());
        const auto& last = frames.back();
        CHECK (std::isfinite (last.frequencyHz));
        CHECK (last.voiced);
        CHECK (std::abs (centsBetween (last.frequencyHz, 300.0f)) < 5.0f);
    }
}

TEST_CASE ("Decimation keeps the analysis rate in 20-40 kHz", "[pitch]")
{
    for (const double sr : { 22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0 })
    {
        PitchDetector det;
        det.prepare (sr);
        INFO ("sr=" << sr);
        if (sr >= 40000.0)
        {
            CHECK (det.getAnalysisSampleRate() >= 20000.0);
            CHECK (det.getAnalysisSampleRate() < 40000.0);
        }
        else
        {
            CHECK (det.getDecimationFactor() == 1);
        }
        // ~2.7 ms hop regardless of the sample rate (default profile)
        CHECK (std::abs (det.getHopSize() / det.getAnalysisSampleRate() - 0.00267) < 0.0003);
    }
}
