// End-to-end correction tests: synthetic signals through the whole processor,
// output pitch measured with the PitchDetector.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"
#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"
#include "signals/SignalGenerators.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

#include <juce_dsp/juce_dsp.h>

using namespace novapitch;
using namespace novapitch::test;

namespace
{
struct Frame
{
    double t;
    float hz;
};

std::vector<Frame> analyse (const std::vector<float>& sig, double sr)
{
    struct Collector final : dsp::PitchDetector::Listener
    {
        std::vector<Frame> frames;
        double sr = 48000.0;
        void pitchFrameReady (const dsp::PitchEstimate& e) noexcept override
        {
            frames.push_back ({ static_cast<double> (e.sampleIndex) / sr, e.voiced ? e.frequencyHz : 0.0f });
        }
    } c;
    c.sr = sr;
    dsp::PitchDetector det;
    det.prepare (sr);
    det.setListener (&c);
    det.process (sig.data(), static_cast<int> (sig.size()));
    return c.frames;
}

/** Runs a mono signal (copied to both channels) through the processor; returns channel 0. */
std::vector<float> runProcessor (NovaPitchProcessor& p, const std::vector<float>& sig, double sr, int block = 256)
{
    p.prepareToPlay (sr, block);
    std::vector<float> out (sig.size());
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < sig.size(); pos += static_cast<size_t> (block))
    {
        const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (block), sig.size() - pos));
        buffer.setSize (2, n, false, false, true);
        buffer.copyFrom (0, 0, sig.data() + pos, n);
        buffer.copyFrom (1, 0, sig.data() + pos, n);
        p.processBlock (buffer, midi);
        std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, out.begin() + static_cast<long> (pos));
    }
    return out;
}

float maxAbsCentsAfter (const std::vector<Frame>& frames, double from, float targetHz, int* voicedCount = nullptr)
{
    float worst = 0.0f;
    int count = 0;
    for (const auto& f : frames)
        if (f.t >= from && f.hz > 0.0f)
        {
            worst = std::max (worst, std::abs (dsp::centsBetween (f.hz, targetHz)));
            ++count;
        }
    if (voicedCount != nullptr)
        *voicedCount = count;
    return worst;
}

std::vector<float> withFades (std::vector<float> v, double sr, double fadeSeconds = 0.005)
{
    const size_t n = static_cast<size_t> (fadeSeconds * sr);
    for (size_t i = 0; i < n && i < v.size(); ++i)
    {
        const float g = static_cast<float> (i) / static_cast<float> (n);
        v[i] *= g;
        v[v.size() - 1 - i] *= g;
    }
    return v;
}
} // namespace

TEST_CASE ("A note +35 cents sharp is corrected to < 5 cents", "[correction]")
{
    JuceFixture fixture;
    const double sr = GENERATE (44100.0, 48000.0, 96000.0);
    const int mode = GENERATE (0, 1, 2, 3); // Live, Tracking, HQ, Vintage
    const float speed = GENERATE (0.0f, 20.0f);

    NovaPitchProcessor p;
    setParam (p, ParamIDs::qualityMode, static_cast<float> (mode));
    setParam (p, ParamIDs::retuneSpeed, speed);

    const float sharp = 220.0f * std::exp2 (35.0f / 1200.0f);
    const auto out = runProcessor (p, signals::sawtooth (sharp, sr, 1.2), sr);
    int voiced = 0;
    const float err = maxAbsCentsAfter (analyse (out, sr), 0.4, 220.0f, &voiced);

    INFO ("sr=" << sr << " mode=" << mode << " speed=" << speed << " max error " << err << " cents");
    CHECK (voiced > 50);
    CHECK (err < 5.0f);
}

TEST_CASE ("Without correction (Ignore) the pitch is unchanged", "[correction]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    setParam (p, ParamIDs::noteState (9), 2.0f); // A -> Ignore
    const float sharp = 220.0f * std::exp2 (30.0f / 1200.0f);
    const auto out = runProcessor (p, signals::sawtooth (sharp, 48000.0, 1.0), 48000.0);
    CHECK (maxAbsCentsAfter (analyse (out, 48000.0), 0.3, sharp) < 3.0f);
}

TEST_CASE ("Removed notes redirect to the nearest valid note of the scale", "[correction]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    setParam (p, ParamIDs::key, 0.0f);   // C
    setParam (p, ParamIDs::scale, 0.0f); // major
    setParam (p, ParamIDs::retuneSpeed, 0.0f);
    // C#4 - 25 cents -> C4 (C# not in C major)
    const float in = dsp::midiToHz (60.75f);
    const auto out = runProcessor (p, signals::sawtooth (in, 48000.0, 1.0), 48000.0);
    CHECK (maxAbsCentsAfter (analyse (out, 48000.0), 0.3, dsp::midiToHz (60.0f)) < 5.0f);
}

TEST_CASE ("Reference A4 = 432 Hz is respected", "[correction]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    setParam (p, ParamIDs::referenceA4, 432.0f);
    setParam (p, ParamIDs::retuneSpeed, 0.0f);
    const float in = 432.0f * std::exp2 (-20.0f / 1200.0f) / 2.0f; // A3 (432 ref) - 20 cents
    const auto out = runProcessor (p, signals::sawtooth (in, 48000.0, 1.0), 48000.0);
    CHECK (maxAbsCentsAfter (analyse (out, 48000.0), 0.3, 216.0f) < 5.0f);
}

TEST_CASE ("Transposition shifts the corrected pitch", "[correction]")
{
    JuceFixture fixture;
    const int mode = GENERATE (1, 2); // Tracking (PSOLA), HQ
    for (const int st : { -12, -5, 7, 12 })
    {
        NovaPitchProcessor p;
        setParam (p, ParamIDs::qualityMode, static_cast<float> (mode));
        setParam (p, ParamIDs::transpose, static_cast<float> (st));
        const auto out = runProcessor (p, signals::sawtooth (220.0, 48000.0, 1.0), 48000.0);
        const float expected = 220.0f * std::exp2 (static_cast<float> (st) / 12.0f);
        const auto frames = analyse (out, 48000.0);
        const float err = maxAbsCentsAfter (frames, 0.3, expected);

        std::vector<float> errors;
        for (const auto& f : frames)
            if (f.t >= 0.3 && f.hz > 0.0f)
                errors.push_back (std::abs (dsp::centsBetween (f.hz, expected)));
        REQUIRE_FALSE (errors.empty());
        std::nth_element (errors.begin(), errors.begin() + static_cast<long> (errors.size() / 2), errors.end());
        const float median = errors[errors.size() / 2];

        INFO ("mode " << mode << " transpose " << st << " max error " << err << " median " << median);
        // HQ (phase vocoder) keeps a small systematic bias on large upward shifts
        // (~+2 c at +12 st, see docs/DSP_NOTES.md); PSOLA is exact.
        CHECK (median < (mode == 2 ? 3.0f : 1.0f));
        CHECK (err < 5.0f);
    }
}

TEST_CASE ("Retune speed 0 gives a stepwise (hard tune) glissando", "[correction]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    setParam (p, ParamIDs::retuneSpeed, 0.0f);
    setParam (p, ParamIDs::qualityMode, 3.0f); // Vintage
    const auto out = runProcessor (p, signals::sawtooth (220.0, 48000.0, 0.01), 48000.0); // warm-up
    juce::ignoreUnused (out);

    const auto gliss = signals::glissando (196.0, 392.0, 48000.0, 3.0, 0.4f);
    const auto frames = analyse (runProcessor (p, gliss, 48000.0), 48000.0);

    int onNote = 0, total = 0;
    for (const auto& f : frames)
        if (f.t > 0.3 && f.hz > 0.0f)
        {
            ++total;
            const float midi = dsp::hzToMidi (f.hz);
            onNote += std::abs (dsp::centsFromNearestNote (midi)) < 8.0f ? 1 : 0;
        }
    INFO (onNote << " of " << total << " frames on a semitone");
    CHECK (total > 300);
    CHECK (onNote > total * 85 / 100); // the rest are the jumps between steps
}

TEST_CASE ("Tolerance lets a slow glissando through between notes", "[correction]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    setParam (p, ParamIDs::retuneSpeed, 0.0f);
    setParam (p, ParamIDs::tolerance, 10.0f);

    const double sr = 48000.0;
    const auto gliss = signals::glissando (196.0, 262.0, sr, 3.0, 0.4f);
    const auto inFrames = analyse (gliss, sr);
    const auto outFrames = analyse (runProcessor (p, gliss, sr), sr);
    const double latency = p.getLatencySamples() / sr;

    // Compare the output with the input at the same musical position.
    int checkedFar = 0, passedFar = 0;
    for (const auto& o : outFrames)
    {
        if (o.t < 0.3 || o.hz <= 0.0f)
            continue;
        const double tIn = o.t - latency;
        const auto it = std::min_element (inFrames.begin(), inFrames.end(), [tIn] (const Frame& a, const Frame& b) {
            return std::abs (a.t - tIn) < std::abs (b.t - tIn);
        });
        if (it == inFrames.end() || it->hz <= 0.0f)
            continue;
        const float dev = std::abs (dsp::centsFromNearestNote (dsp::hzToMidi (it->hz)));
        if (dev > 35.0f) // far from any note: must pass uncorrected
        {
            ++checkedFar;
            passedFar += std::abs (dsp::centsBetween (o.hz, it->hz)) < 6.0f ? 1 : 0;
        }
    }
    INFO (passedFar << " of " << checkedFar << " far frames passed uncorrected");
    CHECK (checkedFar > 50);
    CHECK (passedFar > checkedFar * 9 / 10);
}

TEST_CASE ("No clicks at voiced / unvoiced transitions", "[correction][clicks]")
{
    JuceFixture fixture;
    const int mode = GENERATE (0, 1);
    NovaPitchProcessor p;
    setParam (p, ParamIDs::qualityMode, static_cast<float> (mode));
    setParam (p, ParamIDs::retuneSpeed, 0.0f);

    const double sr = 48000.0;
    const auto sig = signals::concat ({ withFades (signals::sine (dsp::midiToHz (57.3f), sr, 0.5, 0.5f), sr),
                                        signals::silence (sr, 0.15),
                                        withFades (signals::sine (dsp::midiToHz (64.6f), sr, 0.5, 0.5f), sr),
                                        signals::silence (sr, 0.1) });
    const auto out = runProcessor (p, sig, sr);

    // A click shows up as a large second difference; a 0.5-amplitude sine at
    // ~400 Hz has |y''| <= 0.5 * (2π·400/48000)^2 ≈ 0.0014.
    float worst = 0.0f;
    for (size_t i = 2; i < out.size(); ++i)
        worst = std::max (worst, std::abs (out[i] - 2.0f * out[i - 1] + out[i - 2]));
    INFO ("mode " << mode << " max second difference " << worst);
    CHECK (worst < 0.02f);
}

TEST_CASE ("Measured latency equals reported latency in every mode", "[correction][latency]")
{
    JuceFixture fixture;
    const double sr = GENERATE (44100.0, 48000.0, 96000.0);
    const int mode = GENERATE (0, 1, 2, 3);

    NovaPitchProcessor p;
    setParam (p, ParamIDs::qualityMode, static_cast<float> (mode));
    std::vector<float> sig (static_cast<size_t> (sr), 0.0f);
    sig[static_cast<size_t> (sr / 2)] = 0.9f;
    const auto out = runProcessor (p, sig, sr);

    const auto peak = std::max_element (out.begin(), out.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); });
    const int measured = static_cast<int> (std::distance (out.begin(), peak)) - static_cast<int> (sr / 2);
    INFO ("sr=" << sr << " mode=" << mode);
    CHECK (measured == p.getLatencySamples());
    CHECK (std::abs (*peak) > 0.8f);
}

//==============================================================================
namespace
{
/** Vowel-like test signal: harmonic source through two formant resonators. */
std::vector<float> vowel (float f0, double sr, double seconds)
{
    auto src = signals::harmonicSweep (sr, seconds, std::vector<float> (40, 0.05f), [f0] (double) { return f0; });
    std::vector<float> out (src.size(), 0.0f);
    for (const auto& [fc, bw, g] : { std::tuple { 700.0, 90.0, 1.0f }, std::tuple { 1200.0, 110.0, 0.6f } })
    {
        const double r = std::exp (-3.14159265 * bw / sr), th = 2.0 * 3.14159265 * fc / sr;
        const double a1 = -2.0 * r * std::cos (th), a2 = r * r;
        double y1 = 0.0, y2 = 0.0;
        for (size_t i = 0; i < src.size(); ++i)
        {
            const double y = (1.0 - r) * src[i] - a1 * y1 - a2 * y2;
            y2 = y1;
            y1 = y;
            out[i] += g * static_cast<float> (y);
        }
    }
    float peak = 0.0f;
    for (auto v : out)
        peak = std::max (peak, std::abs (v));
    for (auto& v : out)
        v *= 0.5f / peak;
    return out;
}

/** Spectral centroid (Hz) of the second half of the signal, up to 5 kHz. */
/** No pitch correction at all (every note "Ignore"): isolates formant / transposition behaviour. */
void ignoreAllNotes (NovaPitchProcessor& p)
{
    for (int pc = 0; pc < 12; ++pc)
        setParam (p, ParamIDs::noteState (pc), 2.0f);
}

double centroid (const std::vector<float>& x, double sr)
{
    constexpr int order = 14, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf (2 * n, 0.0f);
    const size_t start = x.size() - static_cast<size_t> (n) - 1000;
    for (int i = 0; i < n; ++i)
        buf[static_cast<size_t> (i)] = x[start + static_cast<size_t> (i)]
                                       * static_cast<float> (0.5 - 0.5 * std::cos (2.0 * 3.14159265 * i / n));
    fft.performFrequencyOnlyForwardTransform (buf.data(), true);
    double num = 0.0, den = 0.0;
    for (int k = 1; k < n / 2; ++k)
    {
        const double f = k * sr / n;
        if (f > 5000.0)
            break;
        const double p = static_cast<double> (buf[static_cast<size_t> (k)]) * buf[static_cast<size_t> (k)];
        num += f * p;
        den += p;
    }
    return num / den;
}
} // namespace

TEST_CASE ("Formants are preserved under transposition, or follow it when preservation is off", "[formant]")
{
    JuceFixture fixture;
    const int mode = GENERATE (1, 2); // PSOLA (Tracking), HQ
    const double sr = 48000.0;
    const auto in = vowel (150.0f, sr, 1.5);
    const double cIn = centroid (in, sr);

    for (const bool preserve : { true, false })
    {
        NovaPitchProcessor p;
        setParam (p, ParamIDs::qualityMode, static_cast<float> (mode));
        setParam (p, ParamIDs::transpose, 7.0f);
        setParam (p, ParamIDs::formantPreserve, preserve ? 1.0f : 0.0f);
        ignoreAllNotes (p);
        const auto out = runProcessor (p, in, sr);
        const double ratio = centroid (out, sr) / cIn;
        const double expected = preserve ? 1.0 : std::exp2 (7.0 / 12.0); // 1.0 or ~1.5
        INFO ("mode " << mode << " preserve " << preserve << " centroid ratio " << ratio);
        CHECK (std::abs (ratio / expected - 1.0) < 0.15);
        // pitch is transposed either way
        CHECK (maxAbsCentsAfter (analyse (out, sr), 0.4, 150.0f * std::exp2 (7.0f / 12.0f)) < 8.0f);
    }
}

TEST_CASE ("Formant shift moves the spectral envelope without changing pitch", "[formant]")
{
    JuceFixture fixture;
    const int mode = GENERATE (1, 2);
    const double sr = 48000.0;
    const auto in = vowel (180.0f, sr, 1.5);
    const double cIn = centroid (in, sr);

    for (const float shift : { -5.0f, 5.0f })
    {
        NovaPitchProcessor p;
        setParam (p, ParamIDs::qualityMode, static_cast<float> (mode));
        setParam (p, ParamIDs::formantShift, shift);
        ignoreAllNotes (p);
        const auto out = runProcessor (p, in, sr);
        const double ratio = centroid (out, sr) / cIn;
        INFO ("mode " << mode << " shift " << shift << " centroid ratio " << ratio);
        CHECK ((shift > 0 ? ratio > 1.12 : ratio < 0.88));
        CHECK (maxAbsCentsAfter (analyse (out, sr), 0.4, 180.0f) < 8.0f);
    }
}

TEST_CASE ("Character scales the envelope (+50 % up, -50 % down)", "[formant]")
{
    JuceFixture fixture;
    const double sr = 48000.0;
    const auto in = vowel (160.0f, sr, 1.5);
    const double cIn = centroid (in, sr);
    for (const float character : { -40.0f, 40.0f })
    {
        NovaPitchProcessor p;
        setParam (p, ParamIDs::character, character);
        ignoreAllNotes (p);
        const auto out = runProcessor (p, in, sr);
        const double ratio = centroid (out, sr) / cIn;
        INFO ("character " << character << " centroid ratio " << ratio);
        CHECK ((character > 0 ? ratio > 1.12 : ratio < 0.88));
    }
}
