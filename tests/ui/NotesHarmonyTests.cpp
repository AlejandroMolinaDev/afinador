// "Notas" tab (12-note keyboard: states and per-note detune) and "Armonías" tab
// (voice selector, mode-dependent interval knob), driven with the mouse.

#include "UiHarness.h"
#include "dsp/NoteMapper.h"

#include <catch2/catch_approx.hpp>

using namespace novapitch;
using namespace novapitch::uitest;
using Catch::Approx;

namespace
{
juce::Point<float> keyPoint (NoteKeyboard& k, int pc)
{
    // Lower part of white keys (below the black keys), middle of black keys.
    const auto r = k.keyBounds (pc);
    const bool black = r.getHeight() < k.keyBounds (0).getHeight() * 0.9f;
    return { r.getCentreX(), black ? r.getCentreY() : r.getBottom() - 0.2f * r.getHeight() };
}

int state (Harness& h, int pc) { return juce::roundToInt (h.param (ParamIDs::noteState (pc))); }
} // namespace

TEST_CASE ("Notas: clicking a key cycles its state, black and white keys are hit correctly", "[ui][notes]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    HostListener host (h.p());
    bot.click (h.get<juce::Button> ("tab1"));
    auto& k = UiTestAccess::keyboard (h.ed());
    REQUIRE (k.isShowing());

    // Every key is reachable where it is drawn.
    for (int pc = 0; pc < 12; ++pc)
    {
        INFO ("pitch class " << pc);
        CHECK (k.pitchClassAt (keyPoint (k, pc)) == pc);
        for (int other = 0; other < 12; ++other)
            if (other != pc)
                CHECK (state (h, other) == 0);
        bot.click (k, keyPoint (k, pc));
        CHECK (state (h, pc) == 1); // Escala -> Activa
        CHECK (host.sawGesture (paramIndex (h.p(), ParamIDs::noteState (pc))));
        h.setParam (ParamIDs::noteState (pc), 0.0f);
    }

    // Full cycle: Escala -> Activa -> Ignorar -> Eliminar -> Escala.
    for (const int expected : { 1, 2, 3, 0 })
    {
        bot.click (k, keyPoint (k, 6));
        CHECK (state (h, 6) == expected);
    }

    // The effective state follows key / scale: C major, F# removed, then forced active.
    h.setParam (ParamIDs::scale, static_cast<float> (dsp::ScaleType::Major));
    pump (40);
    CHECK (k.getEffectiveState (6) == static_cast<int> (dsp::NoteState::Remove));
    bot.click (k, keyPoint (k, 6));
    CHECK (k.getEffectiveState (6) == static_cast<int> (dsp::NoteState::Active));

    // Clicks between the keys and the buttons do nothing.
    const auto keys = k.keyBounds (11);
    bot.click (k, juce::Point<float> (keys.getRight() + 6.0f, keys.getCentreY()), {}, false, false);
    CHECK (state (h, 11) == 0);

    // "Todas activas" and "Restablecer".
    bot.click (h.get<juce::Button> ("notesAllActive"));
    for (int pc = 0; pc < 12; ++pc)
        CHECK (state (h, pc) == 1);
    bot.click (h.get<juce::Button> ("notesReset"));
    for (int pc = 0; pc < 12; ++pc)
        CHECK (state (h, pc) == 0);

    // The keys never overlap the buttons on the right.
    for (int pc = 0; pc < 12; ++pc)
        for (const auto* id : { "notesAllActive", "notesReset" })
            CHECK_FALSE (k.keyBounds (pc).intersects (h.get (id).getBounds().toFloat()));
}

TEST_CASE ("Notas: right-click menu, wheel detune, exact detune dialog", "[ui][notes][detune]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    bot.click (h.get<juce::Button> ("tab1"));
    auto& k = UiTestAccess::keyboard (h.ed());

    // Right click opens the key's menu.
    CHECK (h.clickOpensMenu (bot, k, keyPoint (k, 4), true, "menu-nota"));

    auto menu = UiTestAccess::keyMenu (k, 4);
    const int ignore = itemId (menu, u8 ("Ignorar"));
    REQUIRE (ignore > 0);
    UiTestAccess::chooseKeyItem (k, 4, ignore);
    CHECK (state (h, 4) == 2);

    const int just = itemId (menu, u8 ("-14 ¢"));
    REQUIRE (just > 0);
    UiTestAccess::chooseKeyItem (k, 4, just);
    CHECK (h.param (ParamIDs::noteDetune (4)) == Approx (-14.0f));
    // The menu shows the current detune and ticks it.
    menu = UiTestAccess::keyMenu (k, 4);
    bool found = false;
    for (const auto& e : entries (menu))
    {
        found = found || e.text == u8 ("Desafinación (-14.0 ¢)");
        if (e.text == u8 ("-14 ¢"))
            CHECK (e.ticked);
    }
    CHECK (found);

    // Mouse wheel: +-1 cent, Shift +-10.
    bot.wheel (k, keyPoint (k, 4), 0.3f);
    CHECK (h.param (ParamIDs::noteDetune (4)) == Approx (-13.0f));
    bot.wheel (k, keyPoint (k, 4), -0.3f, Bot::shift());
    CHECK (h.param (ParamIDs::noteDetune (4)) == Approx (-23.0f));
    for (int i = 0; i < 6; ++i)
        bot.wheel (k, keyPoint (k, 4), -0.3f, Bot::shift());
    CHECK (h.param (ParamIDs::noteDetune (4)) == Approx (-50.0f)); // clamped
    bot.wheel (k, keyPoint (k, 1), 0.3f); // black key
    CHECK (h.param (ParamIDs::noteDetune (1)) == Approx (1.0f));

    // "Otro valor…": a dialog, typed with a comma and a typographic minus.
    const int other = itemId (UiTestAccess::keyMenu (k, 10), u8 ("Otro valor"));
    REQUIRE (other > 0);
    UiTestAccess::chooseKeyItem (k, 10, other);
    pump (60);
    auto* dialog = UiTestAccess::detuneDialog (k);
    REQUIRE (dialog != nullptr);
    CHECK (dialog->isShowing());
    dialog->getTextEditor ("cents")->setText (u8 ("−31,2"));
    bot.click (dialogButton (*dialog, u8 ("Aceptar"))); // closes and deletes the dialog
    pump (80);
    CHECK (UiTestAccess::detuneDialog (k) == nullptr);
    CHECK (h.param (ParamIDs::noteDetune (10)) == Approx (-31.2f).margin (0.01f));
    h.shot ("notas-desafinacion");

    // "Quitar todas las desafinaciones".
    UiTestAccess::chooseKeyItem (k, 0, itemId (UiTestAccess::keyMenu (k, 0), u8 ("Quitar todas las desafinaciones")));
    for (int pc = 0; pc < 12; ++pc)
        CHECK (h.param (ParamIDs::noteDetune (pc)) == Approx (0.0f));
    CHECK (state (h, 4) == 2); // states untouched
}

TEST_CASE ("Armonias: voice buttons show one panel; the mode picks the interval knob", "[ui][harmony]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    bot.click (h.get<juce::Button> ("tab4"));

    for (int v = 1; v <= 4; ++v)
    {
        bot.click (h.get<juce::Button> ("voice" + juce::String (v)));
        for (int other = 0; other < 4; ++other)
            CHECK (UiTestAccess::voicePanel (h.ed(), other).isShowing() == (other == v - 1));
    }

    bot.click (h.get<juce::Button> ("voice2"));
    auto& mode = h.get<LabeledCombo> (ParamIDs::harmonyVoice ("hvMode", 2)).getCombo();
    auto& degrees = h.get<LabeledKnob> (ParamIDs::harmonyVoice ("hvDegrees", 2));
    auto& semitones = h.get<LabeledKnob> (ParamIDs::harmonyVoice ("hvSemitones", 2));
    mode.setSelectedItemIndex (2, juce::sendNotificationSync); // Semitonos
    pump (40);
    CHECK (juce::roundToInt (h.param (ParamIDs::harmonyVoice ("hvMode", 2))) == 2);
    CHECK (semitones.isShowing());
    CHECK_FALSE (degrees.isShowing());
    CHECK (h.get<juce::Button> ("voice2").getButtonText() == u8 ("Voz 2 · Semit."));

    h.setParam (ParamIDs::harmonyVoice ("hvMode", 2), 1.0f); // host: Diatónica
    pump (60);
    CHECK (degrees.isShowing());
    CHECK_FALSE (semitones.isShowing());
    CHECK (h.get<juce::Button> ("voice2").getButtonText() == u8 ("Voz 2 · Diat."));
    h.shot ("armonias-voz2");
}
