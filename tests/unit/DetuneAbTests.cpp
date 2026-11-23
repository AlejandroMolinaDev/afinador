// Per-note detune (microtonal scales) and A/B comparison of sound settings.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "dsp/HarmonyEngine.h"
#include "dsp/NoteMapper.h"
#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"
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
std::vector<float> runMono (NovaPitchProcessor& p, const std::vector<float>& sig, double sr)
{
    const int block = 256;
    p.prepareToPlay (sr, block);
    std::vector<float> out (sig.size());
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + block <= sig.size(); pos += block)
    {
        buffer.copyFrom (0, 0, sig.data() + pos, block);
        buffer.copyFrom (1, 0, sig.data() + pos, block);
        p.processBlock (buffer, midi);
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

float value (NovaPitchProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}
} // namespace

//==============================================================================
TEST_CASE ("NoteMapper: per-note detune moves the target and the decision", "[detune]")
{
    dsp::NoteMapper m;
    m.setKeyAndScale (0, dsp::ScaleType::Major);
    m.setNoteDetune (4, -14.0f); // E a just major third (-14 c)
    auto t = m.map (63.9f);
    REQUIRE (t.hasTarget);
    CHECK (t.note == 64);
    CHECK (t.targetMidi == Approx (63.86f).margin (0.001f));

    // Only D and E valid, E a quarter tone flat (63.5): 62.85 is now closer to E.
    dsp::NoteMapper q;
    q.setKeyAndScale (0, dsp::ScaleType::Custom);
    for (int pc = 0; pc < 12; ++pc)
        q.setNoteState (pc, (pc == 2 || pc == 4) ? dsp::NoteState::Active : dsp::NoteState::Remove);
    CHECK (q.map (62.85f).note == 62); // equal temperament: D
    q.reset();
    q.setNoteDetune (4, -50.0f);
    t = q.map (62.85f);
    CHECK (t.note == 64);
    CHECK (t.targetMidi == Approx (63.5f));
    q.setNoteDetune (4, -80.0f); // clamped to -50
    CHECK (q.getNoteDetune (4) == Approx (-0.5f));
}

TEST_CASE ("Per-note detune through the processor: the target follows the detuned note", "[detune][e2e]")
{
    JuceFixture fixture;
    const double sr = 48000.0;
    const float e4 = dsp::midiToHz (64.0f, 440.0f);
    const float e4Just = e4 * std::exp2 (-14.0f / 1200.0f);

    for (const bool detuned : { false, true })
    {
        auto p = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
        REQUIRE (p != nullptr);
        setParam (*p, ParamIDs::retuneSpeed, 0.0f);
        setParam (*p, ParamIDs::vibratoPreserve, 0.0f);
        if (detuned)
            setParam (*p, ParamIDs::noteDetune (4), -14.0f);
        // Sung E4 +10 c: corrected to E4 (equal) or to E4 -14 c (detuned).
        const auto out = runMono (*p, signals::sawtooth (e4 * std::exp2 (10.0 / 1200.0), sr, 1.0), sr);
        const float hz = medianPitch (out, sr, 0.4, 0.95);
        const float expected = detuned ? e4Just : e4;
        INFO ((detuned ? "detuned" : "equal") << ": " << hz << " Hz, expected " << expected);
        CHECK (std::abs (dsp::centsBetween (hz, expected)) < 3.0f);
    }
}

TEST_CASE ("Harmony voices land on their own note's detune", "[detune][harmony]")
{
    dsp::HarmonyEngine h;
    h.prepare (48000.0, 480, 32, 375.0);
    dsp::HarmonyVoiceSettings v;
    v.mode = dsp::HarmonyMode::Diatonic;
    v.degrees = 2; // third above
    h.setVoice (0, v);

    dsp::HarmonyFrame f;
    f.voiced = true;
    f.leadNote = 60;
    f.detune[0] = 0.10f;  // C +10 c
    f.detune[4] = -0.14f; // E -14 c
    f.leadTargetMidi = 60.10f;
    f.leadOutputMidi = 60.15f; // lead 5 c above its (detuned) target
    f.scaleMask = dsp::scaleMask (dsp::ScaleType::Major);
    float midi = 0.0f;
    REQUIRE (h.voiceTargetMidi (0, f, midi));
    CHECK (midi == Approx (63.86f + 0.05f).margin (0.001f)); // E -14 c, plus the lead's expression
}

TEST_CASE ("Graph import and Sincronizar carry the per-note detune", "[detune][graph][sync]")
{
    JuceFixture fixture;
    {
        juce::UndoManager undo;
        GraphModel model (undo);
        juce::ValueTree state ("NovaPitchState");
        model.bind (state);
        std::vector<CaptureFrame> frames;
        for (int k = 0; k < 40; ++k)
            frames.push_back ({ k * 0.01, 63.9f, -18.0f });
        model.appendCaptured (frames.data(), static_cast<int> (frames.size()));
        model.commitCapture();
        dsp::NoteMapper mapper;
        mapper.setNoteDetune (4, -14.0f);
        REQUIRE (model.importFromAuto (mapper) == 1);
        CHECK (model.compile()->segments[0].startPitch == Approx (63.86f).margin (0.001f));
    }

    auto a = std::make_unique<NovaPitchProcessor>();
    auto b = std::make_unique<NovaPitchProcessor>();
    setParam (*a, ParamIDs::noteDetune (10), -31.0f); // a harmonic seventh
    REQUIRE (a->syncToOtherInstances() >= 1);
    b->applyPendingSync();
    CHECK (value (*b, ParamIDs::noteDetune (10)) == Approx (-31.0f).margin (0.05f));
}

//==============================================================================
TEST_CASE ("A/B comparison keeps two sounds and never touches the song settings", "[ab][presets]")
{
    JuceFixture fixture;
    auto p = std::make_unique<NovaPitchProcessor>();
    auto& pm = p->getPresetManager();
    REQUIRE (pm.getActiveSlot() == 0);

    setParam (*p, ParamIDs::retuneSpeed, 35.0f); // sound A
    setParam (*p, ParamIDs::key, 9.0f);          // song setting

    pm.selectSlot (1); // B starts as a copy of A
    CHECK (pm.getActiveSlot() == 1);
    CHECK (value (*p, ParamIDs::retuneSpeed) == Approx (35.0f));
    setParam (*p, ParamIDs::retuneSpeed, 0.0f); // sound B
    setParam (*p, ParamIDs::vibratoPreserve, 0.0f);
    setParam (*p, ParamIDs::key, 2.0f); // the song changes while on B

    pm.selectSlot (0);
    CHECK (value (*p, ParamIDs::retuneSpeed) == Approx (35.0f));
    CHECK (value (*p, ParamIDs::vibratoPreserve) == Approx (100.0f));
    CHECK (value (*p, ParamIDs::key) == Approx (2.0f)); // untouched by A/B

    pm.selectSlot (1);
    CHECK (value (*p, ParamIDs::retuneSpeed) == Approx (0.0f));
    CHECK (value (*p, ParamIDs::vibratoPreserve) == Approx (0.0f));

    // Saved with the project: reopen, switch, same sounds.
    juce::MemoryBlock saved;
    p->getStateInformation (saved);
    auto q = std::make_unique<NovaPitchProcessor>();
    q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (q->getPresetManager().getActiveSlot() == 1);
    CHECK (value (*q, ParamIDs::retuneSpeed) == Approx (0.0f));
    q->getPresetManager().selectSlot (0);
    CHECK (value (*q, ParamIDs::retuneSpeed) == Approx (35.0f));

    // Copy: A -> B makes both equal.
    q->getPresetManager().copyActiveToOther();
    q->getPresetManager().selectSlot (1);
    CHECK (value (*q, ParamIDs::retuneSpeed) == Approx (35.0f));
}

TEST_CASE ("The modified marker follows sound edits, not song edits", "[ab][presets]")
{
    JuceFixture fixture;
    auto p = std::make_unique<NovaPitchProcessor>();
    auto& pm = p->getPresetManager();
    REQUIRE (pm.loadPreset (juce::String::fromUTF8 ("Pop moderno")));
    CHECK_FALSE (pm.isModified());
    setParam (*p, ParamIDs::key, 5.0f);
    setParam (*p, ParamIDs::noteDetune (3), 20.0f);
    CHECK_FALSE (pm.isModified()); // song settings don't count
    setParam (*p, ParamIDs::retuneSpeed, 90.0f);
    CHECK (pm.isModified());
    REQUIRE (pm.loadPreset (juce::String::fromUTF8 ("Pop moderno"))); // choosing it again reverts
    CHECK_FALSE (pm.isModified());
    CHECK (value (*p, ParamIDs::key) == Approx (5.0f));
}
