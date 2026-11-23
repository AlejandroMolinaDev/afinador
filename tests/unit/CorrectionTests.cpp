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
#include <vector>

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
    const int mode = GENERATE (0, 1, 3); // Live, Tracking, Vintage
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
    for (const int st : { -12, -5, 7, 12 })
    {
        NovaPitchProcessor p;
        setParam (p, ParamIDs::transpose, static_cast<float> (st));
        const auto out = runProcessor (p, signals::sawtooth (220.0, 48000.0, 1.0), 48000.0);
        const float expected = 220.0f * std::exp2 (static_cast<float> (st) / 12.0f);
        const float err = maxAbsCentsAfter (analyse (out, 48000.0), 0.3, expected);
        INFO ("transpose " << st << " error " << err);
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
