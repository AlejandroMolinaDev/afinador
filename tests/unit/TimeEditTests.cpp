// Phase 9: audio capture + non-destructive time correction.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"
#include "dsp/graph/TimeMap.h"
#include "plugin/graph/AudioCapture.h"
#include "plugin/graph/TimeRenderer.h"
#include "signals/SignalGenerators.h"

#include <cmath>
#include <vector>

using namespace novapitch;
using namespace novapitch::test;
using Catch::Approx;

namespace
{
/** A temporary folder deleted at the end of the test. */
struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("NovaPitchTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

/** Writes `sig` (mono) as a capture pass starting at host sample `start`. */
AudioPass writePass (const juce::File& dir, const juce::String& name, const std::vector<float>& sig, int64_t start, double sr)
{
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (dir.getChildFile (name));
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24));
    const float* ch[] = { sig.data() };
    writer->writeFromFloatArrays (ch, 1, static_cast<int> (sig.size()));
    writer.reset();
    AudioPass p;
    p.fileName = name;
    p.start = start;
    p.length = static_cast<int64_t> (sig.size());
    p.sampleRate = sr;
    p.channels = 1;
    return p;
}

/** A tone with silent gaps: onsets (attacks) at the given times. */
std::vector<float> burstSignal (double sr, double seconds, const std::vector<std::pair<double, double>>& bursts, double hz = 220.0)
{
    std::vector<float> s (static_cast<size_t> (seconds * sr), 0.0f);
    for (const auto& [t0, t1] : bursts)
        for (auto i = static_cast<size_t> (t0 * sr); i < static_cast<size_t> (t1 * sr) && i < s.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double env = std::min (1.0, (t - t0) / 0.002) * std::min (1.0, (t1 - t) / 0.005);
            s[i] = static_cast<float> (0.5 * env * std::sin (2.0 * juce::MathConstants<double>::pi * hz * t));
        }
    return s;
}

/** First sample (from `from`) whose magnitude exceeds `threshold`. */
double onsetAfter (const float* x, int64_t n, double sr, double from, float threshold = 0.1f)
{
    for (auto i = static_cast<int64_t> (from * sr); i < n; ++i)
        if (std::abs (x[i]) > threshold)
            return static_cast<double> (i) / sr;
    return -1.0;
}
} // namespace

TEST_CASE ("TimeMap maps both ways and is the identity outside its anchors", "[time]")
{
    dsp::TimeMap m;
    CHECK (m.isIdentity());
    CHECK (m.toSource (1.234) == Approx (1.234));
    m.anchors = { { 0.5, 0.5 }, { 1.1, 1.0 }, { 1.8, 1.8 } };
    REQUIRE (m.isValid());
    CHECK_FALSE (m.isIdentity());
    CHECK (m.toSource (1.1) == Approx (1.0));
    CHECK (m.toOutput (1.0) == Approx (1.1));
    CHECK (m.toSource (0.8) == Approx (0.75)); // 0.5 -> 1.1 out spans 0.5 -> 1.0 src
    for (double t : { 0.1, 0.6, 0.95, 1.3, 1.79, 2.5 })
        CHECK (m.toOutput (m.toSource (t)) == Approx (t).margin (1.0e-9));
    CHECK (m.toSource (0.2) == Approx (0.2));
    CHECK (m.toSource (3.0) == Approx (3.0));
    const auto ranges = m.warpedRanges();
    REQUIRE (ranges.size() == 1);
    CHECK (ranges[0].first == Approx (0.5));
    CHECK (ranges[0].second == Approx (1.8));

    m.anchors = { { 0.5, 0.5 }, { 0.51, 0.6 } };
    CHECK_FALSE (m.isValid()); // closer than 20 ms on the output axis
}

TEST_CASE ("Time render moves an attack and keeps the unedited audio aligned", "[time]")
{
    JuceFixture fixture;
    TempDir tmp;
    const double sr = 48000.0;
    // Audio before the edit (0.02-0.27 s), attacks at 0.30, 1.00 and 1.90 s; a breath gap at 2.05-2.20 s.
    const auto sig = burstSignal (sr, 3.0, { { 0.02, 0.27 }, { 0.30, 0.80 }, { 1.00, 1.70 }, { 1.90, 2.05 }, { 2.20, 2.80 } });
    TimeRenderer::Job job;
    job.passes = { writePass (tmp.dir, "a.wav", sig, 0, sr) };
    job.directory = tmp.dir;
    job.sampleRate = sr;
    job.channels = 1;
    // Move the attack at 1.00 s to 1.10 s; 0.30 and 1.90 stay where they are.
    job.map.anchors = { { 0.30, 0.30 }, { 1.10, 1.00 }, { 1.90, 1.90 } };

    bool missing = false;
    const auto r = TimeRenderer::render (job, &missing);
    REQUIRE (r != nullptr);
    CHECK_FALSE (missing);
    REQUIRE (r->spans.size() == 1);
    const auto& span = r->spans[0];
    CHECK (std::abs (span.start - static_cast<int64_t> (0.05 * sr)) <= 1);

    // Render the whole timeline as the processor would: live input + substitution.
    std::vector<float> out = sig;
    float* ch[] = { out.data() };
    REQUIRE (r->substitute (ch, 1, static_cast<int> (out.size()), 0));

    const double moved = onsetAfter (out.data(), static_cast<int64_t> (out.size()), sr, 0.95); // burst 1 now ends ~0.87 s
    const double first = onsetAfter (out.data(), static_cast<int64_t> (out.size()), sr, 0.28);
    const double last = onsetAfter (out.data(), static_cast<int64_t> (out.size()), sr, 1.75);
    INFO ("onsets: " << first << ", " << moved << ", " << last);
    CHECK (first == Approx (0.30).margin (0.005));
    CHECK (moved == Approx (1.10).margin (0.005));
    CHECK (last == Approx (1.90).margin (0.005));

    // Before the edit the rendered audio is the source itself; the switch back
    // to the live input happens in the quiet gap after the edit.
    double err = 0.0, ref = 0.0;
    for (auto i = static_cast<size_t> (0.07 * sr); i < static_cast<size_t> (0.26 * sr); ++i)
    {
        err += (out[i] - sig[i]) * (out[i] - sig[i]);
        ref += sig[i] * sig[i];
    }
    INFO ("residual before the edit: " << 10.0 * std::log10 (err / ref + 1e-20) << " dB");
    CHECK (err < 0.001 * ref); // < -30 dB
    const double spanEnd = static_cast<double> (span.end()) / sr;
    CHECK (spanEnd > 2.05);
    CHECK (spanEnd <= 2.20);

    // The stretched burst keeps its level.
    auto rms = [&] (const std::vector<float>& x, double t0, double t1) {
        double e = 0.0;
        for (auto i = static_cast<size_t> (t0 * sr); i < static_cast<size_t> (t1 * sr); ++i)
            e += x[i] * x[i];
        return std::sqrt (e / ((t1 - t0) * sr));
    };
    CHECK (rms (out, 0.5, 0.8) == Approx (rms (sig, 0.5, 0.7)).epsilon (0.12));

    // Pitch is not changed by the time stretch.
    std::vector<float> stretched (out.begin() + static_cast<long> (1.2 * sr), out.begin() + static_cast<long> (1.6 * sr));
    struct Collector final : dsp::PitchDetector::Listener
    {
        std::vector<float> hz;
        void pitchFrameReady (const dsp::PitchEstimate& e) noexcept override { if (e.voiced) hz.push_back (e.frequencyHz); }
    } c;
    dsp::PitchDetector det;
    det.prepare (sr);
    det.setListener (&c);
    det.process (stretched.data(), static_cast<int> (stretched.size()));
    REQUIRE (c.hz.size() > 20);
    for (size_t i = c.hz.size() / 2; i < c.hz.size(); ++i)
        CHECK (std::abs (dsp::centsBetween (c.hz[i], 220.0f)) < 5.0f);
}

TEST_CASE ("AudioCapture writes one WAV per contiguous pass with the exact samples", "[time][capture]")
{
    JuceFixture fixture;
    TempDir tmp;
    const double sr = 48000.0;
    AudioCapture capture;
    capture.prepare (sr, 1);
    capture.setDirectory (tmp.dir);

    const auto sig = signals::sine (330.0, sr, 0.5, 0.5f);
    auto pushRange = [&] (int64_t hostStart, size_t from, size_t to) {
        for (size_t pos = from; pos < to; pos += 300)
        {
            const int n = static_cast<int> (std::min<size_t> (300, to - pos));
            const float* ch[] = { sig.data() + pos };
            capture.push (ch, 1, n, hostStart + static_cast<int64_t> (pos - from));
            if ((pos / 300) % 10 == 0)
                capture.flush(); // the writer thread keeps up
        }
    };
    pushRange (96000, 0, sig.size());          // first pass at 2.0 s
    pushRange (0, 0, sig.size() / 2);          // loop back to 0: second pass
    capture.endPass();
    capture.flush();

    auto passes = capture.takeFinishedPasses();
    REQUIRE (passes.size() == 2);
    CHECK (passes[0].start == 96000);
    CHECK (passes[0].length == static_cast<int64_t> (sig.size()));
    CHECK (passes[1].start == 0);
    CHECK (passes[1].length == static_cast<int64_t> (sig.size() / 2));
    CHECK (capture.getDroppedBlocks() == 0);

    // Read back through the renderer's reader: exact positions, 24-bit precision.
    bool missing = false;
    const auto back = TimeRenderer::readSource (passes, tmp.dir, sr, 1, 96000 + 1000, 2000, &missing);
    CHECK_FALSE (missing);
    float worst = 0.0f;
    for (int i = 0; i < 2000; ++i)
        worst = std::max (worst, std::abs (back.getSample (0, i) - sig[static_cast<size_t> (1000 + i)]));
    CHECK (worst < 1.0e-5f);
    // Where nothing was captured: silence and "missing".
    missing = false;
    TimeRenderer::readSource (passes, tmp.dir, sr, 1, 500000, 100, &missing);
    CHECK (missing);
}

TEST_CASE ("Time edit end to end: capture, move a syllable, play it moved and corrected, reopen", "[time][e2e]")
{
    JuceFixture fixture;
    TempDir tmp;
    const double sr = 48000.0;
    const int block = 256;
    const double inputHz = 220.0 * std::exp2 (25.0 / 1200.0); // A3 +25 c
    const auto sig = burstSignal (sr, 2.6, { { 0.02, 0.27 }, { 0.30, 0.80 }, { 1.00, 1.70 }, { 1.90, 2.05 }, { 2.20, 2.50 } }, inputHz);

    auto play = [&] (NovaPitchProcessor& p, TestPlayHead& head) {
        std::vector<float> out (sig.size());
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        for (size_t pos = 0; pos < sig.size(); pos += static_cast<size_t> (block))
        {
            const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (block), sig.size() - pos));
            buffer.setSize (2, n, false, false, true);
            buffer.copyFrom (0, 0, sig.data() + pos, n);
            buffer.copyFrom (1, 0, sig.data() + pos, n);
            head.timeInSamples = static_cast<int64_t> (pos);
            p.processBlock (buffer, midi);
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, out.begin() + static_cast<long> (pos));
            if ((pos / static_cast<size_t> (block)) % 16 == 0)
                p.processGraphMessages();
        }
        p.processGraphMessages();
        return out;
    };
    auto pitchIn = [&] (const std::vector<float>& x, double t0, double t1) {
        std::vector<float> seg (x.begin() + static_cast<long> (t0 * sr), x.begin() + static_cast<long> (t1 * sr));
        struct Collector final : dsp::PitchDetector::Listener
        {
            std::vector<float> hz;
            void pitchFrameReady (const dsp::PitchEstimate& e) noexcept override { if (e.voiced) hz.push_back (e.frequencyHz); }
        } c;
        dsp::PitchDetector det;
        det.prepare (sr);
        det.setListener (&c);
        det.process (seg.data(), static_cast<int> (seg.size()));
        if (c.hz.size() < 6)
            return 0.0f;
        std::sort (c.hz.begin(), c.hz.end());
        return c.hz[c.hz.size() / 2];
    };

    auto p = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
    REQUIRE (p != nullptr);
    p->setCaptureRoot (tmp.dir);
    TestPlayHead head;
    head.sampleRate = sr;
    p->setPlayHead (&head);
    setParam (*p, ParamIDs::retuneSpeed, 0.0f);
    p->prepareToPlay (sr, block);

    // 1) Capture pitch + audio by playing.
    p->setGraphCapture (true);
    play (*p, head);
    p->setGraphCapture (false);
    p->flushGraphCapture();
    auto& model = p->getGraphModel();
    REQUIRE (model.getAudioPasses().size() == 1);
    REQUIRE (p->importGraphFromAuto() >= 4);

    // 2) Move the syllable that starts at 1.00 s to 1.10 s (neighbours absorb it)
    //    and retune it to B3.
    model.beginEdit();
    dsp::TimeMap map;
    map.anchors = { { 0.30, 0.30 }, { 1.10, 1.00 }, { 1.90, 1.90 } };
    model.setTimeMap (map);
    juce::ValueTree moved;
    for (const auto& o : model.getObjects())
        if (std::abs (static_cast<double> (o.getProperty ("start")) - 1.0) < 0.03)
            moved = o;
    REQUIRE (moved.isValid());
    model.setObjectProperty (moved, juce::Identifier ("pitch"), 59.0f);
    setParam (*p, ParamIDs::graphMode, 1.0f);
    p->renderTimeEditsNow();
    REQUIRE_FALSE (p->isTimeAudioMissing());

    // 3) Playback: the attack comes 100 ms later (plus the 10 ms latency), at B3;
    //    the stretched first syllable stays at A3 (its note).
    const double latency = static_cast<double> (p->getLatencySamples()) / sr;
    auto check = [&] (NovaPitchProcessor& proc, TestPlayHead& h, const char* label) {
        const auto out = play (proc, h);
        const double onset = onsetAfter (out.data(), static_cast<int64_t> (out.size()), sr, 0.95 + latency);
        const float movedHz = pitchIn (out, 1.25 + latency, 1.65 + latency);
        const float firstHz = pitchIn (out, 0.45 + latency, 0.80 + latency);
        INFO (label << ": onset " << onset << " s, moved syllable " << movedHz << " Hz, first " << firstHz << " Hz");
        CHECK (onset == Approx (1.10 + latency).margin (0.008));
        CHECK (std::abs (dsp::centsBetween (movedHz, dsp::midiToHz (59.0f, 440.0f))) < 8.0f);
        CHECK (std::abs (dsp::centsBetween (firstHz, 220.0f)) < 8.0f);
    };
    check (*p, head, "edited");

    // 4) Save and reopen (same machine: the captured audio is still there).
    juce::MemoryBlock saved;
    p->getStateInformation (saved);
    p->setPlayHead (nullptr);
    auto q = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
    REQUIRE (q != nullptr);
    q->setCaptureRoot (tmp.dir);
    q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (q->getGraphModel().getTimeMap().anchors.size() == 3);
    CHECK (q->getGraphModel().getAudioPasses().size() == 1);
    TestPlayHead head2;
    head2.sampleRate = sr;
    q->setPlayHead (&head2);
    q->prepareToPlay (sr, block);
    q->renderTimeEditsNow();
    check (*q, head2, "reopened");

    // 5) Undo the time edit in the original instance: identity map again.
    REQUIRE (model.getUndoManager().undo());
    CHECK (model.getTimeMap().isIdentity());

    // 6) Clearing the capture deletes the audio files.
    q->setPlayHead (nullptr);
    q->clearGraphCapture();
    CHECK (q->getGraphModel().getAudioPasses().empty());
    CHECK (tmp.dir.findChildFiles (juce::File::findFiles, true, "*.wav").isEmpty());
}
