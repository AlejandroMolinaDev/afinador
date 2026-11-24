// Graphical mode driven with the mouse and keyboard: capture with a host
// transport, import, every tool, undo / redo, inspector, view controls, the
// time tool and the clear menu. End to end: a note drawn with the mouse on a
// captured A3 makes the output sing that note.

#include "UiHarness.h"
#include "dsp/PitchDetector.h"
#include "dsp/PitchUtils.h"
#include "plugin/graph/GraphModel.h"
#include "signals/SignalGenerators.h"

#include <catch2/catch_approx.hpp>

#include <algorithm>

using namespace novapitch;
using namespace novapitch::uitest;
using Catch::Approx;

namespace
{
float medianMidi (const std::vector<float>& x, double t0, double t1)
{
    const double sr = Harness::kSampleRate;
    if (x.size() < static_cast<size_t> (t1 * sr))
        return -1.0f;
    std::vector<float> seg (x.begin() + static_cast<long> (t0 * sr), x.begin() + static_cast<long> (t1 * sr));
    struct C final : dsp::PitchDetector::Listener
    {
        std::vector<float> midi;
        void pitchFrameReady (const dsp::PitchEstimate& e) noexcept override
        {
            if (e.voiced)
                midi.push_back (dsp::hzToMidi (e.frequencyHz, 440.0f));
        }
    } c;
    dsp::PitchDetector d;
    d.prepare (sr);
    d.setListener (&c);
    d.process (seg.data(), static_cast<int> (seg.size()));
    if (c.midi.empty())
        return -1.0f;
    std::sort (c.midi.begin(), c.midi.end());
    return c.midi[c.midi.size() / 2];
}

juce::ValueTree onlyObject (GraphModel& m, const juce::Identifier& type)
{
    juce::ValueTree found;
    for (const auto& o : m.getObjects())
        if (o.hasType (type))
            found = o;
    return found;
}

int count (GraphModel& m, const juce::Identifier& type)
{
    int n = 0;
    for (const auto& o : m.getObjects())
        n += o.hasType (type) ? 1 : 0;
    return n;
}

double prop (const juce::ValueTree& o, const juce::Identifier& id) { return static_cast<double> (o.getProperty (id)); }

/** Graph view with a captured, sung A3 (2.4 s), recorded through the UI. */
struct GraphSession
{
    Harness h;
    Bot bot;
    test::TestPlayHead head;
    std::vector<float> voice = signals::sawtooth (220.0, Harness::kSampleRate, 2.4);

    GraphEditor& graph() { return UiTestAccess::graph (h.ed()); }
    GraphModel& model() { return h.p().getGraphModel(); }
    juce::Component& roll() { return UiTestAccess::roll (graph()); }
    juce::Point<float> at (double t, float pitch) { return UiTestAccess::rollPoint (graph(), t, pitch); }
    void tool (int index) { bot.click (h.get<juce::Button> ("tool" + juce::String (index))); }

    void capture()
    {
        bot.click (h.get<juce::Button> ("modeGraph"));
        REQUIRE (UiTestAccess::graphView (h.ed()));
        bot.click (h.get<juce::Button> ("capture"));
        REQUIRE (h.p().isGraphCapturing());
        CHECK (UiTestAccess::infoText (graph()).startsWith (u8 ("Esperando reproducción"))); // transport stopped
        const std::vector<float> first (voice.begin(), voice.begin() + static_cast<long> (voice.size() / 2));
        h.feed (first, &head);
        CHECK (UiTestAccess::infoText (graph()).startsWith (u8 ("Capturando")));
        h.feed (voice, &head); // the second pass replaces the first, same range
        bot.click (h.get<juce::Button> ("capture"));
        CHECK_FALSE (h.p().isGraphCapturing());
        pump (300);
    }

    /** Plays the voice from the start with the transport and returns the output. */
    std::vector<float> play()
    {
        h.lastOutput.clear();
        h.feed (voice, &head);
        return h.lastOutput;
    }
};
} // namespace

//==============================================================================
TEST_CASE ("Graph: capture, import, undo / redo, and a note drawn with the mouse is sung", "[ui][graph][e2e]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    GraphSession s;
    s.capture();
    CHECK (s.model().getCaptured().size() > 200);
    CHECK (UiTestAccess::infoText (s.graph()).startsWith (u8 ("Captura 0:02")));
    s.h.shot ("grafico-captura");

    // "Desde la captura" in Escuchar is now available.
    bool fromCapture = false;
    itemId (UiTestAccess::listenMenu (s.h.ed()), u8 ("Desde la captura"), &fromCapture);
    CHECK (fromCapture);

    // Import: the menu offers three detail levels and curves.
    CHECK (s.h.clickOpensMenu (s.bot, s.h.get<juce::Button> ("import"), {}, false, "menu-importar"));
    CHECK (s.h.clickOpensMenu (s.bot, s.h.get<juce::Button> ("clear")));
    const auto menu = UiTestAccess::importMenu (s.graph());
    const int normal = itemId (menu, u8 ("Notas — detalle normal"));
    const int more = itemId (menu, u8 ("Notas — más detalle"));
    const int less = itemId (menu, u8 ("Notas — menos detalle"));
    const int curves = itemId (menu, u8 ("Curvas"));
    REQUIRE ((normal > 0 && more > 0 && less > 0 && curves > 0));
    for (const int detail : { less, more, normal })
    {
        UiTestAccess::chooseImport (s.graph(), detail);
        pump (60);
        CHECK (count (s.model(), GraphIds::note) == 1); // one sustained A3
        CHECK (prop (onlyObject (s.model(), GraphIds::note), GraphIds::pitch) == Approx (57.0));
    }
    UiTestAccess::chooseImport (s.graph(), curves);
    pump (60);
    CHECK (count (s.model(), GraphIds::curve) >= 1);
    CHECK (count (s.model(), GraphIds::note) == 0);

    // Undo / redo buttons walk through the imports.
    auto& undo = s.h.get<juce::Button> ("undo");
    auto& redo = s.h.get<juce::Button> ("redo");
    CHECK (undo.isEnabled());
    s.bot.click (undo);
    CHECK (count (s.model(), GraphIds::note) == 1);
    CHECK (redo.isEnabled());
    s.bot.click (redo);
    CHECK (count (s.model(), GraphIds::curve) >= 1);

    // Clear objects from the menu (the captured curve stays).
    bool enabled = false;
    const int clearObjects = itemId (UiTestAccess::clearMenu (s.graph()), u8 ("Borrar todos los objetos"), &enabled);
    CHECK (enabled);
    UiTestAccess::chooseClear (s.graph(), clearObjects);
    pump (60);
    CHECK (s.model().getObjects().getNumChildren() == 0);
    CHECK_FALSE (s.model().getCaptured().empty());

    // Draw a B3 note from 0.3 s to 1.9 s with the Note tool.
    UiTestAccess::setView (s.graph(), 0.0, 3.0, 66.0f, 16.0f);
    s.tool (1);
    CHECK (s.graph().getTool() == GraphEditor::Tool::Note);
    s.bot.drag (s.roll(), s.at (0.3, 59.0f), s.at (1.9, 59.0f));
    REQUIRE (count (s.model(), GraphIds::note) == 1);
    const auto note = onlyObject (s.model(), GraphIds::note);
    const double secondsPerPixel = 3.0 / s.roll().getWidth();
    CHECK (prop (note, GraphIds::pitch) == Approx (59.0));
    CHECK (prop (note, GraphIds::start) == Approx (0.3).margin (3.0 * secondsPerPixel));
    CHECK (prop (note, GraphIds::end) == Approx (1.9).margin (3.0 * secondsPerPixel));
    s.h.shot ("grafico-nota-dibujada");

    // Play: inside the note the A3 is sung as B3; before it, the voice passes.
    const auto out = s.play();
    const float inside = medianMidi (out, 0.6, 1.7);
    const float before = medianMidi (out, 0.05, 0.25);
    INFO ("inside " << inside << ", before " << before);
    CHECK (inside == Approx (59.0f).margin (0.15f));
    CHECK (before == Approx (57.0f).margin (0.15f));
}

//==============================================================================
TEST_CASE ("Graph: select, move, resize, rubber band, line, curve, split, merge, erase, right click", "[ui][graph]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    GraphSession s;
    s.capture();
    UiTestAccess::setView (s.graph(), 0.0, 3.0, 66.0f, 16.0f);
    s.tool (1);
    s.bot.drag (s.roll(), s.at (0.3, 59.0f), s.at (1.9, 59.0f));
    const double px = 3.0 / s.roll().getWidth();
    auto note = [&] { return onlyObject (s.model(), GraphIds::note); };

    // Select tool: drag the body two semitones up (snapped), then Alt for a free move.
    s.tool (0);
    s.bot.drag (s.roll(), s.at (1.0, 59.0f), s.at (1.0, 61.2f));
    CHECK (prop (note(), GraphIds::pitch) == Approx (61.0));
    CHECK (prop (note(), GraphIds::start) == Approx (0.3).margin (3.0 * px));
    s.bot.drag (s.roll(), s.at (1.0, 61.0f), s.at (1.0, 61.3f), Bot::alt());
    CHECK (prop (note(), GraphIds::pitch) == Approx (61.3).margin (0.05));

    // Right edge: stretch to 2.2 s.
    const float pitch = static_cast<float> (prop (note(), GraphIds::pitch));
    s.bot.drag (s.roll(), s.at (1.9, pitch) - juce::Point<float> (3.0f, 0.0f), s.at (2.2, pitch) - juce::Point<float> (3.0f, 0.0f));
    CHECK (prop (note(), GraphIds::end) == Approx (2.2).margin (4.0 * px));
    CHECK (prop (note(), GraphIds::start) == Approx (0.3).margin (3.0 * px));

    // Click on empty space deselects; a rubber band selects; Escape clears.
    s.bot.click (s.roll(), s.at (2.6, 52.0f));
    CHECK (UiTestAccess::selection (s.graph()).empty());
    s.bot.drag (s.roll(), s.at (0.1, 64.0f), s.at (2.5, 58.0f));
    CHECK (UiTestAccess::selection (s.graph()).size() == 1);
    s.bot.key (s.graph(), juce::KeyPress (juce::KeyPress::escapeKey));
    CHECK (UiTestAccess::selection (s.graph()).empty());

    // Line tool.
    s.tool (2);
    s.bot.drag (s.roll(), s.at (2.4, 57.0f), s.at (2.8, 60.0f));
    const auto line = onlyObject (s.model(), GraphIds::line);
    REQUIRE (line.isValid());
    CHECK (prop (line, GraphIds::pitch) == Approx (57.0));
    CHECK (prop (line, GraphIds::endPitch) == Approx (60.0));
    CHECK (prop (line, GraphIds::start) == Approx (2.4).margin (3.0 * px));
    CHECK (prop (line, GraphIds::end) == Approx (2.8).margin (3.0 * px));

    // Curve tool (freehand).
    s.tool (3);
    s.bot.drag (s.roll(), s.at (0.02, 55.0f), s.at (0.25, 56.0f), {}, 16);
    const auto curve = onlyObject (s.model(), GraphIds::curve);
    REQUIRE (curve.isValid());
    CHECK (GraphModel::readCurvePoints (curve).size() >= 4);

    // Inspector: notes get speed / vibrato / formant; curves only vibrato.
    s.tool (0);
    s.bot.click (s.roll(), s.at (1.0, pitch));
    auto& retune = UiTestAccess::retuneSlider (s.graph());
    auto& vibrato = UiTestAccess::vibratoSlider (s.graph());
    CHECK (retune.isEnabled());
    s.bot.drag (retune, { 4.0f, retune.getHeight() * 0.5f }, { 40.0f, retune.getHeight() * 0.5f });
    CHECK (prop (note(), GraphIds::retuneMs) > 0.0);
    s.bot.click (s.roll(), s.at (0.13, 55.5f));
    CHECK (UiTestAccess::selection (s.graph()).size() == 1);
    CHECK_FALSE (retune.isEnabled());
    CHECK (vibrato.isEnabled());
    s.h.shot ("grafico-herramientas");

    // Right click deletes whatever the tool.
    s.tool (1);
    s.bot.rightClick (s.roll(), s.at (0.13, 55.5f));
    CHECK (count (s.model(), GraphIds::curve) == 0);

    // Split, then merge with Shift+click and "Unir".
    s.tool (5);
    s.bot.click (s.roll(), s.at (1.0, pitch));
    REQUIRE (count (s.model(), GraphIds::note) == 2);
    s.tool (0);
    s.bot.click (s.roll(), s.at (0.6, pitch));
    CHECK_FALSE (s.h.get<juce::Button> ("merge").isEnabled());
    s.bot.click (s.roll(), s.at (1.6, pitch), Bot::shift());
    CHECK (UiTestAccess::selection (s.graph()).size() == 2);
    CHECK (s.h.get<juce::Button> ("merge").isEnabled());
    s.bot.click (s.h.get<juce::Button> ("merge"));
    CHECK (count (s.model(), GraphIds::note) == 1);

    // Erase tool: sweep across the line.
    s.tool (4);
    s.bot.drag (s.roll(), s.at (2.6, 60.5f), s.at (2.6, 56.5f));
    CHECK (count (s.model(), GraphIds::line) == 0);
    CHECK (count (s.model(), GraphIds::note) == 1);
}

//==============================================================================
TEST_CASE ("Graph: keyboard shortcuts, view controls and follow", "[ui][graph]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    GraphSession s;
    s.capture();
    UiTestAccess::setView (s.graph(), 0.0, 3.0, 66.0f, 16.0f);
    s.tool (1);
    s.bot.drag (s.roll(), s.at (0.3, 59.0f), s.at (1.9, 59.0f));
    s.tool (0);
    s.bot.click (s.roll(), s.at (1.0, 59.0f));
    auto note = [&] { return onlyObject (s.model(), GraphIds::note); };
    auto key = [&] (int code, juce::ModifierKeys mods = {}) { return s.bot.key (s.graph(), juce::KeyPress (code, mods, 0)); };

    CHECK (key (juce::KeyPress::upKey));
    CHECK (prop (note(), GraphIds::pitch) == Approx (60.0));
    key (juce::KeyPress::downKey, Bot::shift());
    CHECK (prop (note(), GraphIds::pitch) == Approx (59.9).margin (1.0e-4));
    const double start = prop (note(), GraphIds::start);
    key (juce::KeyPress::rightKey);
    CHECK (prop (note(), GraphIds::start) == Approx (start + 0.01).margin (1.0e-6));
    key (juce::KeyPress::leftKey, Bot::shift());
    CHECK (prop (note(), GraphIds::start) == Approx (start - 0.09).margin (1.0e-6));

    CHECK (key (juce::KeyPress::deleteKey));
    CHECK (count (s.model(), GraphIds::note) == 0);
    key ('z', Bot::command());
    CHECK (count (s.model(), GraphIds::note) == 1);
    key ('z', Bot::command().withFlags (juce::ModifierKeys::shiftModifier));
    CHECK (count (s.model(), GraphIds::note) == 0);
    key ('z', Bot::command());
    key ('a', Bot::command());
    CHECK (UiTestAccess::selection (s.graph()).size() == 1);

    for (int t = 0; t < GraphEditor::kNumTools; ++t)
    {
        key ('1' + t);
        CHECK (static_cast<int> (s.graph().getTool()) == t);
        CHECK (s.h.get<juce::Button> ("tool" + juce::String (t)).getToggleState());
    }

    // Zoom buttons halve / double the time range around the centre; Ajustar shows everything.
    const double length = UiTestAccess::viewLength (s.graph());
    s.bot.click (s.h.get<juce::Button> ("zoomIn"));
    CHECK (UiTestAccess::viewLength (s.graph()) == Approx (length * 0.5));
    s.bot.click (s.h.get<juce::Button> ("zoomOut"));
    s.bot.click (s.h.get<juce::Button> ("zoomOut"));
    CHECK (UiTestAccess::viewLength (s.graph()) == Approx (length * 2.0));
    s.bot.click (s.h.get<juce::Button> ("fit"));
    CHECK (UiTestAccess::viewLength (s.graph()) < length * 2.0);
    CHECK (UiTestAccess::viewStart (s.graph()) <= 0.05);

    // Ctrl + wheel zooms, Shift + wheel scrolls.
    const double before = UiTestAccess::viewLength (s.graph());
    s.bot.wheel (s.roll(), s.at (1.0, 59.0f), 0.5f, Bot::command());
    CHECK (UiTestAccess::viewLength (s.graph()) < before);

    // Follow: with the transport running past the view, the view pages along.
    auto& follow = s.h.get<juce::Button> ("follow");
    if (! follow.getToggleState())
        s.bot.click (follow);
    CHECK (UiTestAccess::following (s.graph()));
    UiTestAccess::setView (s.graph(), 0.0, 1.0, 66.0f, 16.0f);
    s.bot.click (follow); // setView turned it off: on again
    CHECK (UiTestAccess::following (s.graph()));
    s.play();
    CHECK (UiTestAccess::viewStart (s.graph()) > 1.0);
}

//==============================================================================
TEST_CASE ("Graph: time tool moves a syllable, Alt+click undoes it; clear menu", "[ui][graph][time]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    GraphSession s;
    s.voice = signals::concat ({ signals::silence (Harness::kSampleRate, 0.3), signals::sawtooth (220.0, Harness::kSampleRate, 2.1) });
    s.capture();
    UiTestAccess::chooseImport (s.graph(), itemId (UiTestAccess::importMenu (s.graph()), u8 ("Notas — detalle normal")));
    pump (60);
    REQUIRE (count (s.model(), GraphIds::note) == 1);
    const auto note = onlyObject (s.model(), GraphIds::note);
    const double start = prop (note, GraphIds::start), end = prop (note, GraphIds::end);
    UiTestAccess::setView (s.graph(), 0.0, 3.2, 64.0f, 14.0f);

    bool enabled = true;
    itemId (UiTestAccess::clearMenu (s.graph()), u8 ("Quitar ediciones de tiempo"), &enabled);
    CHECK_FALSE (enabled);

    // Time tool: drag the body 100 ms later.
    s.tool (6);
    CHECK (s.graph().getTool() == GraphEditor::Tool::Time);
    const double mid = 0.5 * (start + end);
    const auto from = s.at (mid, 57.0f);
    const float dx = static_cast<float> (0.1 / 3.2 * (s.roll().getWidth() - 40));
    s.bot.drag (s.roll(), from, from + juce::Point<float> (dx, 0.0f));
    const auto map = s.model().getTimeMap();
    REQUIRE_FALSE (map.isIdentity());
    CHECK (map.toOutput (start) == Approx (start + 0.1).margin (0.01));
    CHECK (map.toOutput (end) == Approx (end + 0.1).margin (0.01));
    pump (400); // background render
    s.h.shot ("grafico-tiempo");
    itemId (UiTestAccess::clearMenu (s.graph()), u8 ("Quitar ediciones de tiempo"), &enabled);
    CHECK (enabled);

    // Alt+click on the note removes its time edit (the note is drawn at its new place).
    s.bot.click (s.roll(), s.at (mid, 57.0f) + juce::Point<float> (dx, 0.0f), Bot::alt());
    CHECK (s.model().getTimeMap().isIdentity());
    s.bot.click (s.h.get<juce::Button> ("undo"));
    CHECK_FALSE (s.model().getTimeMap().isIdentity());

    // Clear: time edits only, then everything.
    UiTestAccess::chooseClear (s.graph(), itemId (UiTestAccess::clearMenu (s.graph()), u8 ("Quitar ediciones de tiempo")));
    CHECK (s.model().getTimeMap().isIdentity());
    CHECK (count (s.model(), GraphIds::note) == 1);
    UiTestAccess::chooseClear (s.graph(), itemId (UiTestAccess::clearMenu (s.graph()), u8 ("Borrar todo")));
    pump (100);
    CHECK (s.model().getObjects().getNumChildren() == 0);
    CHECK (s.model().getCaptured().empty());
    CHECK (UiTestAccess::infoText (s.graph()) == u8 ("Captura 0:00 · 0 objetos"));
    itemId (UiTestAccess::clearMenu (s.graph()), u8 ("Borrar la curva capturada"), &enabled);
    CHECK_FALSE (enabled);
}

TEST_CASE ("Graph: a note at the very start of the song is stretched from its end by the time tool", "[ui][graph][time]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    GraphSession s; // the voice starts at 0:00.000
    s.capture();
    for (const auto& f : s.model().getCaptured())
        CHECK (f.time >= 0.0); // no frames before the timeline start
    UiTestAccess::chooseImport (s.graph(), itemId (UiTestAccess::importMenu (s.graph()), u8 ("Notas — detalle normal")));
    pump (60);
    REQUIRE (count (s.model(), GraphIds::note) == 1);
    const auto note = onlyObject (s.model(), GraphIds::note);
    const double start = prop (note, GraphIds::start), end = prop (note, GraphIds::end);
    CHECK (start >= 0.0);
    CHECK (start < dsp::TimeMap::kMinSpacing);

    UiTestAccess::setView (s.graph(), 0.0, 3.2, 64.0f, 14.0f);
    s.tool (6);
    const auto from = s.at (0.5 * (start + end), 57.0f);
    const float dx = static_cast<float> (0.1 / 3.2 * (s.roll().getWidth() - 40));
    s.bot.drag (s.roll(), from, from + juce::Point<float> (dx, 0.0f));
    const auto map = s.model().getTimeMap();
    REQUIRE_FALSE (map.isIdentity());
    CHECK (map.isValid());
    CHECK (map.toOutput (start) == Approx (start).margin (1.0e-6)); // stays at the start
    CHECK (map.toOutput (end) == Approx (end + 0.1).margin (0.01));  // the syllable is longer
}
