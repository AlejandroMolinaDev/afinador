// Feature audit (0.9.x): MIDI note output, exact-octave MIDI target, vibrato
// shapes, graph import options.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"
#include "dsp/VibratoSynth.h"
#include "plugin/graph/GraphModel.h"
#include "signals/SignalGenerators.h"

#include <cmath>
#include <vector>

using namespace novapitch;
using namespace novapitch::test;
using Catch::Approx;

namespace
{
struct NoteEvent
{
    int note;
    bool on;
    double time;
};

/** Runs `sig` through the processor and returns the output (channel 0) and the MIDI it produced. */
std::vector<float> run (NovaPitchProcessor& p, const std::vector<float>& sig, double sr, std::vector<NoteEvent>* events,
                        const std::vector<std::pair<double, juce::MidiMessage>>& midiIn = {})
{
    const int block = 256;
    p.prepareToPlay (sr, block);
    std::vector<float> out (sig.size());
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (2048);
    size_t nextIn = 0;
    for (size_t pos = 0; pos + block <= sig.size(); pos += block)
    {
        buffer.copyFrom (0, 0, sig.data() + pos, block);
        buffer.copyFrom (1, 0, sig.data() + pos, block);
        midi.clear();
        while (nextIn < midiIn.size() && midiIn[nextIn].first * sr < static_cast<double> (pos + block))
            midi.addEvent (midiIn[nextIn++].second, 0);
        p.processBlock (buffer, midi);
        for (const auto m : midi)
            if (events != nullptr && (m.getMessage().isNoteOn() || m.getMessage().isNoteOff()))
                events->push_back ({ m.getMessage().getNoteNumber(), m.getMessage().isNoteOn(),
                                     static_cast<double> (pos + static_cast<size_t> (m.samplePosition)) / sr });
        std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + block, out.begin() + static_cast<long> (pos));
    }
    return out;
}

float medianPitch (const std::vector<float>& x, double sr, double t0, double t1)
{
    std::vector<float> seg (x.begin() + static_cast<long> (t0 * sr), x.begin() + static_cast<long> (t1 * sr));
    struct C final : dsp::PitchDetector::Listener
    {
        std::vector<float> hz;
        void pitchFrameReady (const dsp::PitchEstimate& e) noexcept override { if (e.voiced) hz.push_back (e.frequencyHz); }
    } c;
    dsp::PitchDetector d;
    d.prepare (sr);
    d.setListener (&c);
    d.process (seg.data(), static_cast<int> (seg.size()));
    if (c.hz.empty())
        return 0.0f;
    std::sort (c.hz.begin(), c.hz.end());
    return c.hz[c.hz.size() / 2];
}
} // namespace

TEST_CASE ("MIDI out sends the corrected notes, one at a time, with note offs", "[audit][midi]")
{
    JuceFixture fixture;
    const double sr = 48000.0;
    auto p = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
    REQUIRE (p != nullptr);
    REQUIRE (p->producesMidi());
    setParam (*p, ParamIDs::midiOut, 1.0f);
    setParam (*p, ParamIDs::transpose, 0.0f);
    // A3 +30 c, silence, C4 -20 c, silence
    const auto sig = signals::concat ({ signals::sawtooth (220.0 * std::exp2 (30.0 / 1200.0), sr, 0.4), signals::silence (sr, 0.2),
                                        signals::sawtooth (261.63 * std::exp2 (-20.0 / 1200.0), sr, 0.4), signals::silence (sr, 0.3) });
    std::vector<NoteEvent> ev;
    run (*p, sig, sr, &ev);
    REQUIRE (ev.size() == 4);
    CHECK (ev[0].on);
    CHECK (ev[0].note == 57);
    CHECK_FALSE (ev[1].on);
    CHECK (ev[1].note == 57);
    CHECK (ev[2].on);
    CHECK (ev[2].note == 60);
    CHECK_FALSE (ev[3].on);
    CHECK (ev[3].note == 60);
    CHECK (ev[0].time < 0.06);
    CHECK (ev[2].time == Approx (0.6).margin (0.06));

    // Off: no MIDI at all (and the input MIDI is not echoed).
    setParam (*p, ParamIDs::midiOut, 0.0f);
    std::vector<NoteEvent> none;
    run (*p, sig, sr, &none, { { 0.1, juce::MidiMessage::noteOn (1, 64, 0.8f) } });
    CHECK (none.empty());
}

TEST_CASE ("MIDI target exact octave follows the played octave", "[audit][midi]")
{
    JuceFixture fixture;
    const double sr = 48000.0;
    const auto sig = signals::sawtooth (261.63 * std::exp2 (20.0 / 1200.0), sr, 1.0); // sung C4 +20 c
    const std::vector<std::pair<double, juce::MidiMessage>> held { { 0.0, juce::MidiMessage::noteOn (1, 72, 0.8f) } }; // C5

    for (const bool exact : { false, true })
    {
        auto p = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
        REQUIRE (p != nullptr);
        setParam (*p, ParamIDs::midiTarget, 1.0f);
        setParam (*p, ParamIDs::midiTargetExact, exact ? 1.0f : 0.0f);
        setParam (*p, ParamIDs::retuneSpeed, 0.0f);
        const auto out = run (*p, sig, sr, nullptr, held);
        const float hz = medianPitch (out, sr, 0.4, 0.95);
        const float expected = exact ? 523.25f : 261.63f;
        INFO ((exact ? "exact" : "any octave") << ": " << hz << " Hz");
        CHECK (std::abs (dsp::centsBetween (hz, expected)) < 10.0f);
    }
}

TEST_CASE ("Vibrato shapes: sine, triangle, square and sawtooth", "[audit][vibrato]")
{
    constexpr double pi = 3.14159265358979323846;
    for (int shape = 0; shape < 4; ++shape)
    {
        CHECK (std::abs (dsp::VibratoSynth::shapeValue (shape, 0.0)) < 0.05f); // starts at 0: no jump at the onset
        float lo = 1.0f, hi = -1.0f;
        double meanAbs = 0.0;
        for (int i = 0; i < 1000; ++i)
        {
            const float v = dsp::VibratoSynth::shapeValue (shape, 2.0 * pi * i / 1000.0);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
            meanAbs += std::abs (v) / 1000.0;
        }
        CHECK (hi == Approx (1.0f).margin (0.03f));
        CHECK (lo == Approx (-1.0f).margin (0.03f));
        if (shape == 0)
            CHECK (meanAbs == Approx (2.0 / pi).margin (0.01));
        if (shape == 1)
            CHECK (meanAbs == Approx (0.5).margin (0.01));
        if (shape == 2)
            CHECK (meanAbs > 0.85);
        if (shape == 3)
            CHECK (meanAbs == Approx (0.5).margin (0.03));
    }
}

TEST_CASE ("Graph import: detail levels and import as curves", "[audit][graph]")
{
    JuceFixture fixture;
    juce::UndoManager undo;
    GraphModel model (undo);
    juce::ValueTree state ("NovaPitchState");
    model.bind (state);

    // A fast run: 8 notes of 70 ms (C D E F G A B C) then a sustained E with vibrato.
    std::vector<CaptureFrame> frames;
    const float run[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
    double t = 0.0;
    for (float n : run)
        for (int k = 0; k < 7; ++k, t += 0.01)
            frames.push_back ({ t, n + 0.1f, -18.0f });
    for (int k = 0; k < 80; ++k, t += 0.01)
        frames.push_back ({ t, 64.0f + 0.4f * static_cast<float> (std::sin (2.0 * 3.14159265 * 5.5 * k * 0.01)), -18.0f });
    model.appendCaptured (frames.data(), static_cast<int> (frames.size()));
    model.commitCapture();

    dsp::NoteMapper mapper;
    mapper.setKeyAndScale (0, dsp::ScaleType::Major);
    const int normal = model.importFromAuto (mapper, 0.08, 0.04);
    mapper.setHysteresis (0.08f);
    const int detailed = model.importFromAuto (mapper, 0.04, 0.015);
    INFO ("normal " << normal << ", detailed " << detailed);
    CHECK (detailed >= 9); // every note of the run + the sustained one
    CHECK (detailed > normal);

    // Curves follow the captured pitch exactly: nothing changes until edited.
    const int curves = model.importAsCurves();
    CHECK (curves == 1);
    auto track = model.compile();
    dsp::CorrectionTrack::Result r;
    REQUIRE (track->lookup (0.025, r));
    CHECK (r.kind == dsp::CorrectionTrack::Kind::Curve);
    CHECK (r.pitch == Approx (60.1f).margin (0.02f));

    // Vibrato of a curve scaled to 0: the sustained part becomes flat at its centre.
    auto curve = model.getObjects().getChild (0);
    model.beginEdit(); // one undo step per gesture, as in the editor
    model.setObjectProperty (curve, GraphIds::vibratoKeep, 0.0f);
    track = model.compile();
    float lo = 1000.0f, hi = -1000.0f;
    for (double s = t - 0.6; s < t - 0.15; s += 0.005)
        if (track->lookup (s, r))
        {
            lo = std::min (lo, r.pitch);
            hi = std::max (hi, r.pitch);
        }
    CHECK (hi - lo < 0.1f);
    CHECK (0.5f * (hi + lo) == Approx (64.0f).margin (0.1f));
    REQUIRE (undo.undo()); // vibrato scale
    REQUIRE (undo.undo()); // import as curves
    CHECK (model.getObjects().getNumChildren() == detailed);
}
