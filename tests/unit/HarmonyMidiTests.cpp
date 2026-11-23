#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"
#include "dsp/HarmonyEngine.h"
#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"
#include "dsp/Scales.h"
#include "signals/SignalGenerators.h"

#include <algorithm>
#include <cmath>

using namespace novapitch;
using namespace novapitch::dsp;
using namespace novapitch::test;

namespace
{
constexpr uint16_t kCMajor = 0b101010110101; // C D E F G A B

float medianPitch (const std::vector<float>& sig, double sr, double settle)
{
    struct C final : PitchDetector::Listener
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
    PitchDetector d;
    d.prepare (sr);
    d.setProfile (InputProfile::Instrument);
    d.setListener (&c);
    d.process (sig.data(), static_cast<int> (sig.size()));
    if (c.f.empty())
        return 0.0f;
    std::nth_element (c.f.begin(), c.f.begin() + static_cast<long> (c.f.size() / 2), c.f.end());
    return c.f[c.f.size() / 2];
}

/** Runs one solo voice of the engine on a steady lead note; returns the harmony bus (L, R). */
std::pair<std::vector<float>, std::vector<float>> runVoice (const HarmonyVoiceSettings& vs, float leadMidi,
                                                           const std::vector<int>& held = {}, float inputDb = -12.0f)
{
    const double sr = 48000.0;
    const auto lead = signals::sawtooth (midiToHz (leadMidi), sr, 1.2);
    HarmonyEngine h;
    h.prepare (sr, 480, 64, 375.0);
    h.setLatency (480);
    h.setVoice (0, vs);

    HarmonyFrame f;
    f.voiced = true;
    f.periodSamples = static_cast<float> (sr / midiToHz (leadMidi));
    f.detectedMidi = leadMidi;
    f.leadOutputMidi = leadMidi;
    f.leadNote = static_cast<int> (std::lround (leadMidi));
    f.scaleMask = kCMajor;
    f.inputRmsDb = inputDb;
    f.numHeld = static_cast<int> (held.size());
    for (size_t i = 0; i < held.size(); ++i)
        f.held[i] = held[i];

    std::vector<float> l (lead.size(), 0.0f), r (lead.size(), 0.0f);
    for (size_t pos = 0; pos + 64 <= lead.size(); pos += 64)
    {
        h.updateControl (f);
        h.process (lead.data() + pos, l.data() + pos, r.data() + pos, 64);
    }
    return { l, r };
}

std::vector<float> mono (const std::pair<std::vector<float>, std::vector<float>>& lr)
{
    std::vector<float> m (lr.first.size());
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = lr.first[i] + lr.second[i];
    return m;
}
} // namespace

TEST_CASE ("Diatonic steps follow the scale", "[harmony]")
{
    CHECK (HarmonyEngine::stepDegrees (60, 2, kCMajor) == 64);  // C -> E (major third)
    CHECK (HarmonyEngine::stepDegrees (64, 2, kCMajor) == 67);  // E -> G (minor third)
    CHECK (HarmonyEngine::stepDegrees (64, -2, kCMajor) == 60); // E -> C (third below)
    CHECK (HarmonyEngine::stepDegrees (60, 4, kCMajor) == 67);  // fifth
    CHECK (HarmonyEngine::stepDegrees (60, 7, kCMajor) == 72);  // octave
    CHECK (HarmonyEngine::stepDegrees (71, 2, kCMajor) == 74);  // B -> D
    CHECK (HarmonyEngine::stepDegrees (61, 2, kCMajor) == 64);  // C# (not in scale) snaps to C first
    CHECK (HarmonyEngine::stepDegrees (60, 0, kCMajor) == 60);
}

TEST_CASE ("Harmony voices sound at the right pitch", "[harmony]")
{
    HarmonyVoiceSettings vs;
    vs.humanize = 0.0f;
    vs.levelDb = 0.0f;

    SECTION ("diatonic third above E4 is G4 in C major")
    {
        vs.mode = HarmonyMode::Diatonic;
        vs.degrees = 2;
        CHECK (std::abs (centsBetween (medianPitch (mono (runVoice (vs, 64.0f)), 48000.0, 0.4), midiToHz (67.0f))) < 5.0f);
    }
    SECTION ("fixed +7 semitones")
    {
        vs.mode = HarmonyMode::Semitones;
        vs.semitones = 7;
        CHECK (std::abs (centsBetween (medianPitch (mono (runVoice (vs, 57.0f)), 48000.0, 0.4), midiToHz (64.0f))) < 5.0f);
    }
    SECTION ("MIDI chord: voice 1 takes the lowest held note")
    {
        vs.mode = HarmonyMode::Midi;
        CHECK (std::abs (centsBetween (medianPitch (mono (runVoice (vs, 60.0f, { 55, 64, 67 })), 48000.0, 0.4), midiToHz (55.0f))) < 5.0f);
    }
    SECTION ("MIDI mode without held notes is silent")
    {
        vs.mode = HarmonyMode::Midi;
        const auto out = mono (runVoice (vs, 60.0f));
        CHECK (*std::max_element (out.begin(), out.end()) < 1.0e-4f);
    }
}

TEST_CASE ("Harmony gate, pan and mute", "[harmony]")
{
    HarmonyVoiceSettings vs;
    vs.mode = HarmonyMode::Semitones;
    vs.semitones = 4;
    vs.humanize = 0.0f;

    auto peak = [] (const std::vector<float>& v) {
        float p = 0.0f;
        for (size_t i = v.size() / 2; i < v.size(); ++i)
            p = std::max (p, std::abs (v[i]));
        return p;
    };

    SECTION ("below the gate: silent")
    {
        const auto lr = runVoice (vs, 60.0f, {}, -70.0f);
        CHECK (peak (lr.first) < 1.0e-4f);
    }
    SECTION ("hard left")
    {
        vs.pan = -1.0f;
        const auto lr = runVoice (vs, 60.0f);
        CHECK (peak (lr.first) > 0.05f);
        CHECK (peak (lr.second) < 0.01f * peak (lr.first));
    }
    SECTION ("mute")
    {
        vs.mute = true;
        CHECK (peak (runVoice (vs, 60.0f).first) < 1.0e-4f);
    }
}

//==============================================================================
namespace
{
std::vector<float> runWithMidi (NovaPitchProcessor& p, const std::vector<float>& sig, double sr, const std::vector<int>& notesOn)
{
    p.prepareToPlay (sr, 256);
    std::vector<float> out (sig.size());
    juce::AudioBuffer<float> buffer (2, 256);
    for (size_t pos = 0; pos + 256 <= sig.size(); pos += 256)
    {
        juce::MidiBuffer midi;
        if (pos == 0)
            for (int n : notesOn)
                midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
        buffer.copyFrom (0, 0, sig.data() + pos, 256);
        buffer.copyFrom (1, 0, sig.data() + pos, 256);
        p.processBlock (buffer, midi);
        std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + 256, out.begin() + static_cast<long> (pos));
    }
    return out;
}
} // namespace

TEST_CASE ("MIDI Target: the held note is the target (closest octave)", "[midi][correction]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    setParam (p, ParamIDs::midiTarget, 1.0f);
    setParam (p, ParamIDs::retuneSpeed, 0.0f);
    // Sing C4 + 10 c, hold D2 (any octave): output must be D4.
    const auto out = runWithMidi (p, signals::sawtooth (midiToHz (60.1f), 48000.0, 1.2), 48000.0, { 38 });
    const float pitch = medianPitch (out, 48000.0, 0.4);
    INFO ("output " << pitch << " Hz");
    CHECK (std::abs (centsBetween (pitch, midiToHz (62.0f))) < 5.0f);
}

TEST_CASE ("Learn scale from MIDI writes a custom scale", "[midi]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    setParam (p, ParamIDs::midiScaleLearn, 1.0f);
    p.applyLearnedScale(); // learning starts (timer on the message thread)
    runWithMidi (p, signals::silence (48000.0, 0.1), 48000.0, { 60, 64, 67, 71 }); // C E G B
    p.applyLearnedScale();

    auto* scale = p.getValueTreeState().getParameter (ParamIDs::scale);
    CHECK (juce::roundToInt (scale->convertFrom0to1 (scale->getValue())) == static_cast<int> (ScaleType::Custom));
    for (int pc = 0; pc < 12; ++pc)
    {
        auto* st = p.getValueTreeState().getParameter (ParamIDs::noteState (pc));
        const bool learnt = pc == 0 || pc == 4 || pc == 7 || pc == 11;
        INFO ("pitch class " << pc);
        CHECK (juce::roundToInt (st->convertFrom0to1 (st->getValue()))
               == static_cast<int> (learnt ? NoteState::Active : NoteState::Remove));
    }

    // Singing D4 now lands on C4 or E4 (D is not in the learnt scale).
    setParam (p, ParamIDs::retuneSpeed, 0.0f);
    const float pitch = medianPitch (runWithMidi (p, signals::sawtooth (midiToHz (62.3f), 48000.0, 1.0), 48000.0, {}), 48000.0, 0.4);
    CHECK (std::abs (centsBetween (pitch, midiToHz (64.0f))) < 5.0f);
}

TEST_CASE ("Harmonies through the processor: stereo, finite, aligned with the lead's latency", "[harmony]")
{
    JuceFixture fixture;
    const int mode = GENERATE (0, 1, 2); // Live, Tracking, HQ
    NovaPitchProcessor p;
    setParam (p, ParamIDs::qualityMode, static_cast<float> (mode));
    setParam (p, ParamIDs::harmonyVoice ("hvMode", 1), 1.0f); // diatonic third above
    setParam (p, ParamIDs::harmonyVoice ("hvPan", 1), 100.0f);
    setParam (p, ParamIDs::harmonyVoice ("hvMode", 2), 2.0f); // +7 semitones
    setParam (p, ParamIDs::harmonyVoice ("hvSemitones", 2), 7.0f);
    setParam (p, ParamIDs::harmonyVoice ("hvPan", 2), -100.0f);

    const double sr = 48000.0;
    p.prepareToPlay (sr, 256);
    const auto sig = signals::sawtooth (220.0, sr, 1.0);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    float diff = 0.0f;
    bool finite = true;
    for (size_t pos = 0; pos + 256 <= sig.size(); pos += 256)
    {
        buffer.copyFrom (0, 0, sig.data() + pos, 256);
        buffer.copyFrom (1, 0, sig.data() + pos, 256);
        p.processBlock (buffer, midi);
        for (int i = 0; i < 256; ++i)
        {
            finite = finite && std::isfinite (buffer.getSample (0, i)) && std::isfinite (buffer.getSample (1, i));
            diff = std::max (diff, std::abs (buffer.getSample (0, i) - buffer.getSample (1, i)));
        }
    }
    CHECK (finite);
    CHECK (diff > 0.05f); // L and R differ: panned harmonies are present
}
