// Phase 9: key detection (chromagram + Krumhansl-Schmuckler) and instance sync.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "dsp/KeyDetector.h"
#include "dsp/PitchUtils.h"
#include "signals/SignalGenerators.h"

#include <vector>

using namespace novapitch;
using namespace novapitch::test;
using Catch::Approx;

namespace
{
/** Sums the notes of a melody (MIDI numbers, equal durations) into a detector. */
dsp::KeyDetector::Result detectMelody (const std::vector<int>& melody)
{
    dsp::KeyDetector d;
    for (int note : melody)
        d.add (static_cast<float> (note), 0.3f);
    return d.estimate();
}

bool sameNotes (const dsp::KeyDetector::Result& r, int key, bool minor)
{
    // Relative keys share their notes: C major == A minor for a tuner.
    const int majorTonic = r.minor ? (r.key + 3) % 12 : r.key;
    const int expected = minor ? (key + 3) % 12 : key;
    return majorTonic == expected;
}

int paramIndex (NovaPitchProcessor& p, const char* id)
{
    return juce::roundToInt (p.getValueTreeState().getRawParameterValue (id)->load());
}
} // namespace

TEST_CASE ("KeyDetector finds major and minor keys of sung melodies", "[key]")
{
    // C major: do-re-mi... with tonic / dominant emphasis
    auto r = detectMelody ({ 60, 62, 64, 65, 67, 67, 64, 60, 72, 71, 69, 67, 65, 64, 62, 60, 60, 67, 64, 60 });
    REQUIRE (r.valid);
    CHECK (r.key == 0);
    CHECK_FALSE (r.minor);

    // D major
    r = detectMelody ({ 62, 66, 69, 74, 73, 71, 69, 66, 64, 62, 62, 69, 66, 62, 67, 66, 64, 62 });
    CHECK (r.key == 2);
    CHECK_FALSE (r.minor);

    // A natural minor (A, C, E heavy; G natural)
    r = detectMelody ({ 69, 72, 76, 69, 71, 72, 74, 72, 71, 69, 67, 69, 64, 69, 72, 69, 76, 74, 72, 71, 69 });
    CHECK (sameNotes (r, 9, true));
    CHECK (r.key == 9);
    CHECK (r.minor);

    // G minor (Bb, Eb)
    r = detectMelody ({ 67, 70, 74, 67, 69, 70, 72, 74, 75, 74, 72, 70, 69, 67, 62, 67, 70, 67 });
    CHECK (sameNotes (r, 7, true));
    CHECK (r.scale() == (r.minor ? dsp::ScaleType::NaturalMinor : dsp::ScaleType::Major));
    CHECK (r.confidence > 0.0f);
}

TEST_CASE ("KeyDetector weights stable notes above glides and quiet frames", "[key]")
{
    CHECK (dsp::KeyDetector::frameWeight (60.0f, 0.01f, -20.0f) > dsp::KeyDetector::frameWeight (60.45f, 0.01f, -20.0f));
    CHECK (dsp::KeyDetector::frameWeight (60.0f, 0.01f, -10.0f) > dsp::KeyDetector::frameWeight (60.0f, 0.01f, -50.0f));
    CHECK (dsp::KeyDetector::frameWeight (60.5f, 0.01f, -10.0f) == Approx (0.0f));
    dsp::KeyDetector d;
    CHECK_FALSE (d.estimate().valid); // nothing heard
}

TEST_CASE ("Listening to a sung phrase sets key and scale", "[key][e2e]")
{
    JuceFixture fixture;
    const double sr = 48000.0;
    auto p = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
    REQUIRE (p != nullptr);
    setParam (*p, ParamIDs::scale, static_cast<float> (dsp::ScaleType::Chromatic));
    p->prepareToPlay (sr, 256);

    // E major melody, slightly out of tune, 0.3 s notes (with short gaps).
    const int melody[] = { 64, 68, 71, 76, 75, 73, 71, 68, 66, 64, 64, 71, 68, 64, 69, 68, 66, 64, 59, 64 };
    std::vector<float> sig;
    for (int i = 0; i < 2; ++i)
        for (int note : melody)
        {
            const auto tone = signals::sawtooth (dsp::midiToHz (static_cast<float> (note) + 0.12f, 440.0f), sr, 0.28);
            sig.insert (sig.end(), tone.begin(), tone.end());
            sig.insert (sig.end(), static_cast<size_t> (0.02 * sr), 0.0f);
        }

    p->startKeyListening (6.0);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + 256 <= sig.size() && p->isKeyListening(); pos += 256)
    {
        buffer.copyFrom (0, 0, sig.data() + pos, 256);
        buffer.copyFrom (1, 0, sig.data() + pos, 256);
        p->processBlock (buffer, midi);
        if (p->getKeyListenSeconds() >= p->getKeyListenTarget())
            p->finishKeyListening (true); // what the processor's timer does
    }
    REQUIRE_FALSE (p->isKeyListening());
    const auto r = p->getLastKeyResult();
    INFO ("key " << r.key << (r.minor ? " minor" : " major") << ", confidence " << r.confidence);
    REQUIRE (r.valid);
    CHECK (r.key == 4);
    CHECK_FALSE (r.minor);
    CHECK (paramIndex (*p, ParamIDs::key) == 4);
    CHECK (paramIndex (*p, ParamIDs::scale) == static_cast<int> (dsp::ScaleType::Major));
}

TEST_CASE ("Key detection from the graphical mode's capture", "[key][graph]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    std::vector<CaptureFrame> frames;
    const int melody[] = { 62, 65, 69, 74, 72, 70, 69, 67, 65, 64, 62, 69, 65, 62, 67, 65, 64, 62 }; // D minor
    double t = 0.0;
    for (int note : melody)
        for (int k = 0; k < 30; ++k, t += 0.01)
            frames.push_back ({ t, static_cast<float> (note) + 0.05f, -18.0f });
    p.getGraphModel().appendCaptured (frames.data(), static_cast<int> (frames.size()));
    p.getGraphModel().commitCapture();

    const auto r = p.detectKeyFromCapture (true);
    REQUIRE (r.valid);
    CHECK (r.key == 2);
    CHECK (r.minor);
    CHECK (paramIndex (p, ParamIDs::key) == 2);
    CHECK (paramIndex (p, ParamIDs::scale) == static_cast<int> (dsp::ScaleType::NaturalMinor));
}

TEST_CASE ("Sincronizar sends key, scale, note states and A4 to the other instances", "[sync]")
{
    JuceFixture fixture;
    auto a = std::make_unique<NovaPitchProcessor>();
    auto b = std::make_unique<NovaPitchProcessor>();
    auto c = std::make_unique<NovaPitchProcessor>();
    REQUIRE (NovaPitchProcessor::getNumInstancesInProcess() >= 3);

    setParam (*a, ParamIDs::key, 9.0f);
    setParam (*a, ParamIDs::scale, static_cast<float> (dsp::ScaleType::HarmonicMinor));
    setParam (*a, ParamIDs::noteState (1), static_cast<float> (dsp::NoteState::Ignore));
    setParam (*a, ParamIDs::referenceA4, 432.0f);

    CHECK (a->syncToOtherInstances() == NovaPitchProcessor::getNumInstancesInProcess() - 1);
    for (auto* other : { b.get(), c.get() })
    {
        CHECK (paramIndex (*other, ParamIDs::key) == 0); // applied on the message thread, not before
        other->applyPendingSync();
        CHECK (paramIndex (*other, ParamIDs::key) == 9);
        CHECK (paramIndex (*other, ParamIDs::scale) == static_cast<int> (dsp::ScaleType::HarmonicMinor));
        CHECK (paramIndex (*other, ParamIDs::noteState (1).toRawUTF8()) == static_cast<int> (dsp::NoteState::Ignore));
        CHECK (other->getValueTreeState().getRawParameterValue (ParamIDs::referenceA4)->load() == Approx (432.0f));
    }
    a->applyPendingSync(); // the sender receives nothing
    CHECK (paramIndex (*a, ParamIDs::key) == 9);

    const int before = NovaPitchProcessor::getNumInstancesInProcess();
    b.reset();
    CHECK (NovaPitchProcessor::getNumInstancesInProcess() == before - 1);
    CHECK (a->syncToOtherInstances() == before - 2);
}
