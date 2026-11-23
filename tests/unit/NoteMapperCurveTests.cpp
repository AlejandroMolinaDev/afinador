#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/CorrectionCurve.h"
#include "dsp/NoteMapper.h"

#include <cmath>

using namespace novapitch::dsp;
using Catch::Matchers::WithinAbs;

TEST_CASE ("Scale masks and key transposition", "[mapper]")
{
    NoteMapper m;
    m.setKeyAndScale (0, ScaleType::Major); // C major
    for (int pc : { 0, 2, 4, 5, 7, 9, 11 })
        CHECK (m.getEffectiveState (pc) == NoteState::Active);
    for (int pc : { 1, 3, 6, 8, 10 })
        CHECK (m.getEffectiveState (pc) == NoteState::Remove);

    m.setKeyAndScale (9, ScaleType::NaturalMinor); // A minor == C major notes
    for (int pc : { 0, 2, 4, 5, 7, 9, 11 })
        CHECK (m.getEffectiveState (pc) == NoteState::Active);

    m.setKeyAndScale (2, ScaleType::PentatonicMinor); // D F G A C
    for (int pc : { 2, 5, 7, 9, 0 })
        CHECK (m.getEffectiveState (pc) == NoteState::Active);
    CHECK (m.getEffectiveState (4) == NoteState::Remove);
}

TEST_CASE ("Nearest valid note, removed notes redirect", "[mapper]")
{
    NoteMapper m;
    m.setKeyAndScale (0, ScaleType::Major);

    auto t = m.map (64.3f); // E4 + 30 cents
    CHECK (t.hasTarget);
    CHECK (t.correct);
    CHECK (t.note == 64);

    m.reset();
    t = m.map (60.8f); // C#4 - 20 cents: C# is removed -> C4 (0.8 away) rather than D4 (1.2)
    CHECK (t.note == 60);

    m.reset();
    t = m.map (61.2f); // C#4 + 20 cents -> D4 (0.8 away) rather than C4 (1.2)
    CHECK (t.note == 62);
}

TEST_CASE ("Ignore notes are targets that are not corrected", "[mapper]")
{
    NoteMapper m;
    m.setKeyAndScale (0, ScaleType::Chromatic);
    m.setNoteState (9, NoteState::Ignore); // A
    const auto t = m.map (69.3f);
    CHECK (t.hasTarget);
    CHECK (t.note == 69);
    CHECK_FALSE (t.correct);
}

TEST_CASE ("All notes removed means no target", "[mapper]")
{
    NoteMapper m;
    m.setKeyAndScale (0, ScaleType::Custom);
    for (int pc = 0; pc < 12; ++pc)
        m.setNoteState (pc, NoteState::Remove);
    CHECK_FALSE (m.map (60.0f).hasTarget);
}

TEST_CASE ("Hysteresis avoids flicker around the midpoint", "[mapper]")
{
    NoteMapper m;
    m.setKeyAndScale (0, ScaleType::Chromatic);
    CHECK (m.map (60.3f).note == 60);
    CHECK (m.map (60.55f).note == 60); // past the midpoint but inside the hysteresis
    CHECK (m.map (60.62f).note == 60);
    CHECK (m.map (60.7f).note == 61);  // clearly closer to C#
    CHECK (m.map (60.45f).note == 61); // and stays there coming back
    CHECK (m.map (60.3f).note == 60);
}

//==============================================================================
namespace
{
NoteTarget targetFor (int note, bool correct = true)
{
    NoteTarget t;
    t.hasTarget = true;
    t.correct = correct;
    t.note = note;
    t.targetMidi = static_cast<float> (note);
    return t;
}
} // namespace

TEST_CASE ("Retune speed 0 corrects instantly", "[curve]")
{
    CorrectionCurve c;
    c.prepare (375.0);
    c.setSettings ({ 0.0f, 0.0f, 0.0f });
    CHECK_THAT (c.process (true, 69.35f, targetFor (69)), WithinAbs (-35.0, 1e-3));
}

TEST_CASE ("Retune speed is a time constant", "[curve]")
{
    CorrectionCurve c;
    const double frameRate = 375.0;
    c.prepare (frameRate);
    c.setSettings ({ 100.0f, 0.0f, 0.0f });

    float v = 0.0f;
    const int framesFor100ms = static_cast<int> (std::lround (0.1 * frameRate));
    for (int i = 0; i < framesFor100ms; ++i)
        v = c.process (true, 69.5f, targetFor (69)); // constant -50 cent error
    // one-pole: 1 - e^-1 = 63 % after one time constant
    CHECK_THAT (v, WithinAbs (-50.0 * (1.0 - std::exp (-1.0)), 1.5));
}

TEST_CASE ("Unvoiced resets the correction", "[curve]")
{
    CorrectionCurve c;
    c.prepare (375.0);
    c.setSettings ({ 0.0f, 0.0f, 0.0f });
    c.process (true, 69.3f, targetFor (69));
    CHECK (c.process (false, 0.0f, {}) == 0.0f);
}

TEST_CASE ("Tolerance lets far deviations through with a soft edge", "[curve]")
{
    CHECK (CorrectionCurve::toleranceWeight (10.0f, 0.0f) == 1.0f);  // disabled
    CHECK (CorrectionCurve::toleranceWeight (10.0f, 20.0f) == 1.0f); // inside
    CHECK (CorrectionCurve::toleranceWeight (45.0f, 20.0f) == 0.0f); // beyond the edge
    const float mid = CorrectionCurve::toleranceWeight (30.0f, 20.0f);
    CHECK (mid > 0.0f);
    CHECK (mid < 1.0f);

    CorrectionCurve c;
    c.prepare (375.0);
    c.setSettings ({ 0.0f, 20.0f, 0.0f });
    CHECK (c.process (true, 69.45f, targetFor (69)) == 0.0f);                // 45 cents: passes
    CHECK_THAT (c.process (true, 69.1f, targetFor (69)), WithinAbs (-10.0, 1e-3)); // 10 cents: corrected
}

TEST_CASE ("Ignore targets glide the correction back to zero", "[curve]")
{
    CorrectionCurve c;
    c.prepare (375.0);
    c.setSettings ({ 0.0f, 0.0f, 0.0f });
    CHECK (c.process (true, 69.3f, targetFor (69, false)) == 0.0f);
}

TEST_CASE ("Natural hold slows correction on long notes only", "[curve]")
{
    CorrectionCurve c;
    c.prepare (375.0);
    c.setSettings ({ 0.0f, 0.0f, 1.0f });

    c.process (true, 69.2f, targetFor (69));
    CHECK (c.getEffectiveTimeConstantMs() < 1.0f); // attack: still instant

    for (int i = 0; i < 375; ++i) // one second on the same note
        c.process (true, 69.2f, targetFor (69));
    CHECK (c.getEffectiveTimeConstantMs() > 250.0f);

    c.process (true, 71.2f, targetFor (71)); // new note: instant again
    CHECK (c.getEffectiveTimeConstantMs() < 1.0f);
}
