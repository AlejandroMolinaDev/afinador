// Phase 8: graphical mode without ARA (capture by playback, objects, playback).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"
#include "dsp/graph/CorrectionTrack.h"
#include "plugin/graph/GraphModel.h"
#include "signals/SignalGenerators.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace novapitch;
using namespace novapitch::test;
using Catch::Approx;

namespace
{
const juce::Identifier kPitchId ("pitch");

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

/** Worst deviation (cents) from targetHz over [from, to) of the analysed signal, plus voiced count. */
float worstCents (const std::vector<Frame>& frames, double from, double to, float targetHz, int& count)
{
    float worst = 0.0f;
    count = 0;
    for (const auto& f : frames)
        if (f.t >= from && f.t < to && f.hz > 0.0f)
        {
            worst = std::max (worst, std::abs (dsp::centsBetween (f.hz, targetHz)));
            ++count;
        }
    return worst;
}

/** Plays `sig` through the processor with the transport running from `startSample`. */
std::vector<float> play (NovaPitchProcessor& p, TestPlayHead& head, const std::vector<float>& sig, int block = 256,
                         int64_t startSample = 0)
{
    std::vector<float> out (sig.size());
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < sig.size(); pos += static_cast<size_t> (block))
    {
        const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (block), sig.size() - pos));
        buffer.setSize (2, n, false, false, true);
        buffer.copyFrom (0, 0, sig.data() + pos, n);
        buffer.copyFrom (1, 0, sig.data() + pos, n);
        head.timeInSamples = startSample + static_cast<int64_t> (pos);
        p.processBlock (buffer, midi);
        std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, out.begin() + static_cast<long> (pos));
        if ((pos / static_cast<size_t> (block)) % 16 == 0)
            p.processGraphMessages(); // the processor's 30 Hz timer in a real host
    }
    p.processGraphMessages();
    return out;
}
} // namespace

//==============================================================================
TEST_CASE ("CorrectionTrack lookup: notes, lines, curves and gaps", "[graph]")
{
    dsp::CorrectionTrack track;
    using K = dsp::CorrectionTrack::Kind;
    dsp::CorrectionTrack::Segment note;
    note.start = 0.0;
    note.end = 1.0;
    note.kind = K::Note;
    note.startPitch = 60.0f;
    note.retuneMs = 50.0f;
    dsp::CorrectionTrack::Segment line;
    line.start = 1.5;
    line.end = 2.5;
    line.kind = K::Line;
    line.startPitch = 60.0f;
    line.endPitch = 64.0f;
    dsp::CorrectionTrack::Segment curve;
    curve.start = 3.0;
    curve.end = 4.0;
    curve.kind = K::Curve;
    curve.curveOffset = 0;
    curve.curveCount = 3;
    track.curvePoints = { { 3.0, 62.0f }, { 3.5, 66.0f }, { 4.0, 62.0f } };
    track.segments = { note, line, curve };

    dsp::CorrectionTrack::Result r;
    REQUIRE (track.lookup (0.5, r));
    CHECK (r.pitch == Approx (60.0f));
    CHECK (r.retuneMs == Approx (50.0f));
    CHECK_FALSE (track.lookup (1.2, r)); // gap: original voice
    REQUIRE (track.lookup (2.0, r));
    CHECK (r.pitch == Approx (62.0f));
    CHECK (r.kind == K::Line);
    REQUIRE (track.lookup (3.25, r));
    CHECK (r.pitch == Approx (64.0f));
    REQUIRE (track.lookup (3.75, r));
    CHECK (r.pitch == Approx (64.0f));
    CHECK_FALSE (track.lookup (-0.1, r));
    CHECK_FALSE (track.lookup (4.0, r));
}

TEST_CASE ("GraphModel edits are undoable and compile to a sorted, non-overlapping track", "[graph]")
{
    JuceFixture fixture;
    juce::UndoManager undo;
    GraphModel model (undo);
    juce::ValueTree state ("NovaPitchState");
    model.bind (state);

    int changes = 0;
    model.onChanged = [&] { ++changes; };

    model.beginEdit();
    auto a = model.addNote (1.0, 2.0, 60.0f);
    model.beginEdit();
    auto b = model.addNote (0.0, 1.2, 62.0f); // overlaps a: the later start wins
    CHECK (model.getObjects().getNumChildren() == 2);
    CHECK (changes > 0);

    auto track = model.compile();
    REQUIRE (track->segments.size() == 2);
    CHECK (track->segments[0].startPitch == Approx (62.0f));
    CHECK (track->segments[0].end == Approx (1.0));
    CHECK (track->segments[1].start == Approx (1.0));

    model.beginEdit();
    model.moveObject (a, 0.5, 2.0f);
    CHECK (static_cast<double> (a.getProperty ("start")) == Approx (1.5));
    CHECK (static_cast<float> (a.getProperty (kPitchId)) == Approx (62.0f));

    model.beginEdit();
    REQUIRE (model.split (a, 2.0));
    CHECK (model.getObjects().getNumChildren() == 3);
    CHECK (static_cast<double> (a.getProperty ("end")) == Approx (2.0));

    model.beginEdit();
    auto second = model.getObjects().getChild (2);
    REQUIRE (model.merge (a, second));
    CHECK (model.getObjects().getNumChildren() == 2);
    CHECK (static_cast<double> (a.getProperty ("end")) == Approx (2.5));

    REQUIRE (undo.undo()); // merge
    CHECK (model.getObjects().getNumChildren() == 3);
    REQUIRE (undo.undo()); // split
    CHECK (model.getObjects().getNumChildren() == 2);
    CHECK (static_cast<double> (a.getProperty ("end")) == Approx (2.5));
    REQUIRE (undo.undo()); // move
    CHECK (static_cast<double> (a.getProperty ("start")) == Approx (1.0));
    CHECK (static_cast<float> (a.getProperty (kPitchId)) == Approx (60.0f));
    REQUIRE (undo.redo());
    CHECK (static_cast<float> (a.getProperty (kPitchId)) == Approx (62.0f));

    model.beginEdit();
    model.remove (b);
    CHECK (model.getObjects().getNumChildren() == 1);
    REQUIRE (undo.undo());
    CHECK (model.getObjects().getNumChildren() == 2);

    // Lines and curves
    model.beginEdit();
    model.addLine (5.0, 4.0, 60.0f, 65.0f); // drawn right-to-left
    model.addCurve ({ { 6.0, 60.0f }, { 6.2, 61.0f }, { 6.1, 60.5f } });
    track = model.compile();
    dsp::CorrectionTrack::Result r;
    REQUIRE (track->lookup (4.0, r));
    CHECK (r.pitch == Approx (65.0f));
    REQUIRE (track->lookup (6.15, r));
    CHECK (r.pitch == Approx (60.75f));
    CHECK (r.vibratoKeep == Approx (0.0f)); // drawn pitch is the output pitch
}

TEST_CASE ("Capture passes merge by time range and are stored in the state", "[graph]")
{
    JuceFixture fixture;
    juce::UndoManager undo;
    GraphModel model (undo);
    juce::ValueTree state ("NovaPitchState");
    model.bind (state);

    std::vector<CaptureFrame> pass;
    for (int i = 0; i < 100; ++i)
        pass.push_back ({ i * 0.01, 60.0f, -20.0f });
    model.appendCaptured (pass.data(), static_cast<int> (pass.size()));
    model.commitCapture();
    REQUIRE (model.getCaptured().size() == 100);

    // A second pass over 0.30..0.59 s replaces that range only (punch-in).
    pass.clear();
    for (int i = 30; i < 60; ++i)
        pass.push_back ({ i * 0.01 + 0.001, 62.0f, -20.0f });
    model.appendCaptured (pass.data(), static_cast<int> (pass.size()));
    model.commitCapture();
    const auto& c = model.getCaptured();
    REQUIRE (c.size() == 100);
    CHECK (std::is_sorted (c.begin(), c.end(), [] (const auto& x, const auto& y) { return x.time < y.time; }));
    CHECK (c[10].midi == Approx (60.0f));
    CHECK (c[45].midi == Approx (62.0f));
    CHECK (c[80].midi == Approx (60.0f));

    // Round trip through XML (the FL Studio project) and a fresh model.
    auto xml = state.createXml();
    REQUIRE (xml != nullptr);
    auto restored = juce::ValueTree::fromXml (*xml);
    juce::UndoManager undo2;
    GraphModel model2 (undo2);
    model2.bind (restored);
    REQUIRE (model2.getCaptured().size() == 100);
    CHECK (model2.getCaptured()[45].midi == Approx (62.0f));
    CHECK (model2.getCaptured()[45].time == Approx (0.451));
}

TEST_CASE ("Import from automatic builds notes from the captured curve", "[graph]")
{
    JuceFixture fixture;
    juce::UndoManager undo;
    GraphModel model (undo);
    juce::ValueTree state ("NovaPitchState");
    model.bind (state);

    // 0.0-0.6 s: C4 +25 c with a 5.5 Hz / 40 c vibrato; 0.6-0.7 s unvoiced;
    // 0.7-1.2 s: D#4 +30 c (not in C major -> E4 is nearest above... D is 0.8 st away)
    std::vector<CaptureFrame> frames;
    for (int i = 0; i < 120; ++i)
    {
        const double t = i * 0.01;
        float midi = -1.0f;
        if (t < 0.6)
            midi = 60.25f + 0.4f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t));
        else if (t >= 0.7)
            midi = 63.8f;
        frames.push_back ({ t, midi, -20.0f });
    }
    model.appendCaptured (frames.data(), static_cast<int> (frames.size()));
    model.commitCapture();

    dsp::NoteMapper mapper;
    mapper.setKeyAndScale (0, dsp::ScaleType::Major);
    REQUIRE (model.importFromAuto (mapper) == 2);
    auto track = model.compile();
    REQUIRE (track->segments.size() == 2);
    CHECK (track->segments[0].startPitch == Approx (60.0f));
    CHECK (track->segments[0].start == Approx (0.0).margin (0.02));
    CHECK (track->segments[0].end == Approx (0.6).margin (0.03));
    CHECK (track->segments[1].startPitch == Approx (64.0f));
    CHECK (track->segments[1].start == Approx (0.7).margin (0.02));

    // One undo step restores the previous (empty) graph.
    REQUIRE (undo.undo());
    CHECK (model.getObjects().getNumChildren() == 0);
}

TEST_CASE ("TrackPublisher frees retired tracks only after the audio thread moved on", "[graph]")
{
    TrackPublisher publisher;
    publisher.publish (std::make_unique<dsp::CorrectionTrack>(), 0);
    const auto* first = publisher.acquire();
    REQUIRE (first != nullptr);
    publisher.publish (std::make_unique<dsp::CorrectionTrack>(), 10);
    CHECK (publisher.acquire() != first);
    CHECK (publisher.graveyardSize() == 1);
    publisher.collect (11); // the block in flight at the swap may still read it
    CHECK (publisher.graveyardSize() == 1);
    publisher.collect (12);
    CHECK (publisher.graveyardSize() == 0);
}

//==============================================================================
TEST_CASE ("Graphical mode end to end: capture, import, edit, playback, save and reopen", "[graph][e2e]")
{
    JuceFixture fixture;
    const double sr = 48000.0;
    const int block = 256;
    const float inputHz = static_cast<float> (220.0 * std::exp2 (30.0 / 1200.0)); // A3 +30 c
    const auto sig = signals::sawtooth (inputHz, sr, 1.6);
    const float editedHz = dsp::midiToHz (59.0f, 440.0f); // B3

    auto p = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
    REQUIRE (p != nullptr);
    const auto captureRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("NovaPitchGraph-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    p->setCaptureRoot (captureRoot); // never write into the user's capture folder
    TestPlayHead head;
    head.sampleRate = sr;
    p->setPlayHead (&head);
    setParam (*p, ParamIDs::retuneSpeed, 0.0f);
    p->prepareToPlay (sr, block);

    // 1) Capture pass: FL plays the clip, the plugin records the pitch curve.
    p->setGraphCapture (true);
    play (*p, head, sig, block);
    p->setGraphCapture (false);
    const auto& captured = p->getGraphModel().getCaptured();
    REQUIRE (captured.size() > 120);
    int voiced = 0;
    for (const auto& f : captured)
        if (f.midi >= 0.0f && f.time > 0.2 && f.time < 1.4)
        {
            CHECK (f.midi == Approx (57.3f).margin (0.05f));
            ++voiced;
        }
    CHECK (voiced > 100);

    // 2) Import from automatic: one A3 note covering the phrase.
    REQUIRE (p->importGraphFromAuto() == 1);
    auto note = p->getGraphModel().getObjects().getChild (0);
    CHECK (static_cast<float> (note.getProperty (kPitchId)) == Approx (57.0f));

    // 3) Edit: move the note to B3 and end it at 0.8 s (after that the original passes).
    auto& model = p->getGraphModel();
    model.beginEdit();
    model.setObjectProperty (note, kPitchId, 59.0f);
    model.setBounds (note, 0.0, 0.8);
    setParam (*p, ParamIDs::graphMode, 1.0f);

    // 4) Playback: output follows the edit inside the note, original outside.
    auto checkPlayback = [&] (NovaPitchProcessor& proc, TestPlayHead& h, const char* label) {
        const auto out = play (proc, h, sig, block);
        const auto frames = analyse (out, sr);
        int n1 = 0, n2 = 0;
        const float inNote = worstCents (frames, 0.25, 0.7, editedHz, n1);
        const float after = worstCents (frames, 1.0, 1.5, inputHz, n2);
        INFO (label << ": in note " << inNote << " c (" << n1 << " frames), after " << after << " c (" << n2 << ")");
        CHECK (n1 > 80);
        CHECK (n2 > 80);
        CHECK (inNote < 5.0f);
        CHECK (after < 5.0f);
    };
    checkPlayback (*p, head, "edited");

    // Transport stopped: no timeline -> the original voice passes.
    {
        head.playing = false;
        const auto out = play (*p, head, sig, block);
        head.playing = true;
        int n = 0;
        const float w = worstCents (analyse (out, sr), 0.25, 0.7, inputHz, n);
        INFO ("stopped: " << w << " c");
        CHECK (n > 80);
        CHECK (w < 5.0f);
    }

    // 5) Save (FL project) and reopen in a new instance: same objects, same result.
    juce::MemoryBlock saved;
    p->getStateInformation (saved);
    p->setPlayHead (nullptr);

    auto q = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
    REQUIRE (q != nullptr);
    q->setCaptureRoot (captureRoot);
    q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (q->getGraphModel().getObjects().getNumChildren() == 1);
    CHECK (static_cast<float> (q->getGraphModel().getObjects().getChild (0).getProperty (kPitchId)) == Approx (59.0f));
    CHECK (q->getGraphModel().getCaptured().size() == captured.size());
    TestPlayHead head2;
    head2.sampleRate = sr;
    q->setPlayHead (&head2);
    q->prepareToPlay (sr, block);
    checkPlayback (*q, head2, "reopened");

    // 6) Undo in the original instance restores the imported note.
    REQUIRE (model.getUndoManager().undo());
    CHECK (static_cast<float> (note.getProperty (kPitchId)) == Approx (57.0f));
    q->setPlayHead (nullptr);
    p->flushGraphCapture();
    captureRoot.deleteRecursively();
}
