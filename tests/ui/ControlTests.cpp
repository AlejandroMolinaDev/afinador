// Editor controls driven like a user: tabs, the Auto / Gráfico switch, every
// knob / combo / switch against its parameter (both ways, with host gestures),
// typed values, presets, A/B, the options menu, MIDI Learn, Escuchar,
// Sincronizar, and the live displays.

#include "LayoutChecks.h"
#include "UiHarness.h"
#include "dsp/PitchUtils.h"
#include "dsp/Scales.h"
#include "signals/SignalGenerators.h"

#include <catch2/catch_approx.hpp>

#include <set>

using namespace novapitch;
using namespace novapitch::uitest;
using Catch::Approx;

namespace
{
/** Shows the page (and harmony voice) that contains `c`. */
void reveal (Harness& h, Bot& bot, juce::Component& c)
{
    for (int page = 0; page < UiTestAccess::numPages(); ++page)
        if (UiTestAccess::page (h.ed(), page).isParentOf (&c))
        {
            if (UiTestAccess::currentPage (h.ed()) != page)
                bot.click (h.get<juce::Button> ("tab" + juce::String (page)));
            for (int v = 0; v < 4; ++v)
                if (UiTestAccess::voicePanel (h.ed(), v).isParentOf (&c) && ! c.isShowing())
                    bot.click (h.get<juce::Button> ("voice" + juce::String (v + 1)));
        }
}

std::vector<float> sungPhrase (const std::vector<int>& melody, double cents, double noteSeconds = 0.28)
{
    std::vector<float> sig;
    for (int note : melody)
    {
        const auto tone = signals::sawtooth (dsp::midiToHz (static_cast<float> (note) + static_cast<float> (cents / 100.0), 440.0f),
                                             Harness::kSampleRate, noteSeconds);
        sig.insert (sig.end(), tone.begin(), tone.end());
        sig.insert (sig.end(), static_cast<size_t> (0.02 * Harness::kSampleRate), 0.0f);
    }
    return sig;
}
} // namespace

//==============================================================================
TEST_CASE ("Tabs and the Auto / Grafico switch change the view; the view follows host automation", "[ui][controls]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    HostListener host (h.p());

    for (int page = UiTestAccess::numPages(); --page >= 0;)
    {
        bot.click (h.get<juce::Button> ("tab" + juce::String (page)));
        CHECK (UiTestAccess::currentPage (h.ed()) == page);
        CHECK (h.get<juce::Button> ("tab" + juce::String (page)).getToggleState());
    }

    bot.click (h.get<juce::Button> ("modeGraph"));
    CHECK (h.param (ParamIDs::graphMode) > 0.5f);
    CHECK (host.sawGesture (paramIndex (h.p(), ParamIDs::graphMode))); // FL records the switch as automation
    CHECK (UiTestAccess::graph (h.ed()).isShowing());
    CHECK_FALSE (h.get ("tab0").isShowing());
    CHECK_FALSE (h.get ("meter").isShowing());

    bot.click (h.get<juce::Button> ("modeAuto"));
    CHECK (h.param (ParamIDs::graphMode) < 0.5f);
    CHECK_FALSE (UiTestAccess::graph (h.ed()).isShowing());
    CHECK (UiTestAccess::currentPage (h.ed()) == 0); // back on the page that was open
    CHECK (UiTestAccess::page (h.ed(), 0).isShowing());

    // Host automation of "Modo gráfico" switches the view (editor timer).
    h.setParam (ParamIDs::graphMode, 1.0f);
    CHECK (waitFor ([&] { return UiTestAccess::graphView (h.ed()); }));
    CHECK (h.get<juce::Button> ("modeGraph").getToggleState());
    h.setParam (ParamIDs::graphMode, 0.0f);
    CHECK (waitFor ([&] { return ! UiTestAccess::graphView (h.ed()); }));
    CHECK (h.get<juce::Button> ("modeAuto").getToggleState());
}

//==============================================================================
TEST_CASE ("Every parameter has a control, bound both ways with host gestures", "[ui][controls]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    HostListener host (h.p());
    auto& state = h.p().getValueTreeState();
    std::set<juce::String> covered;

    // Knobs: host -> knob, then a mouse drag -> parameter (with a gesture).
    int knobs = 0;
    for (auto* knob : UiTestAccess::knobs (h.ed()))
    {
        const auto id = knob->getComponentID();
        INFO ("knob " << id);
        auto* param = state.getParameter (id);
        REQUIRE (param != nullptr);
        reveal (h, bot, *knob);
        if (! knob->isShowing() && id.startsWith ("hvSemitones")) // shown in "Semitonos" mode only
        {
            h.setParam ("hvMode" + id.fromFirstOccurrenceOf ("hvSemitones", false, false), 2.0f);
            pump (60);
        }
        REQUIRE (knob->isShowing());

        param->setValueNotifyingHost (0.3f);
        pump (30);
        CHECK (knob->getSlider().getValue() == Approx (param->convertFrom0to1 (0.3f)).margin (1.0e-3));

        host.clear();
        const float before = param->getValue();
        auto& slider = knob->getSlider();
        const auto centre = slider.getLocalBounds().toFloat().withTrimmedBottom (18.0f).getCentre();
        bot.drag (slider, centre, centre - juce::Point<float> (0.0f, 70.0f));
        CHECK (param->getValue() > before);
        CHECK (slider.getValue() == Approx (param->convertFrom0to1 (param->getValue())).margin (1.0e-3));
        CHECK (host.sawGesture (param->getParameterIndex()));
        covered.insert (id);
        ++knobs;
    }
    CHECK (knobs >= 45);

    // Combo boxes: host -> combo, a choice -> parameter; a click opens the list.
    std::vector<juce::ComboBox*> combos;
    forEach (*h.window, [&] (juce::Component& c) {
        if (auto* cb = dynamic_cast<juce::ComboBox*> (&c); cb != nullptr && state.getParameter (cb->getComponentID()) == nullptr)
            if (auto* parent = cb->getParentComponent(); parent != nullptr && state.getParameter (parent->getComponentID()) != nullptr)
                cb->setComponentID (parent->getComponentID() + "/combo");
    });
    forEach (*h.window, [&] (juce::Component& c) {
        if (auto* cb = dynamic_cast<juce::ComboBox*> (&c); cb != nullptr && cb->getComponentID() != "presetBox")
            combos.push_back (cb);
    });
    for (auto* combo : combos)
    {
        const auto id = combo->getComponentID().upToFirstOccurrenceOf ("/", false, false);
        INFO ("combo " << id);
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id));
        REQUIRE (choice != nullptr);
        reveal (h, bot, *combo);
        REQUIRE (combo->isShowing());
        REQUIRE (combo->getNumItems() == choice->choices.size());

        choice->setValueNotifyingHost (choice->convertTo0to1 (1.0f));
        pump (30);
        CHECK (combo->getSelectedItemIndex() == 1);

        host.clear();
        combo->setSelectedItemIndex (choice->choices.size() - 1, juce::sendNotificationSync); // what picking an item does
        pump (20);
        CHECK (choice->getIndex() == choice->choices.size() - 1);
        CHECK (host.sawGesture (choice->getParameterIndex()));

        CHECK (h.clickOpensMenu (bot, *combo)); // the list opens
        choice->setValueNotifyingHost (static_cast<juce::RangedAudioParameter*> (choice)->getDefaultValue());
        covered.insert (id);
    }
    CHECK (combos.size() >= 9);

    // Switches: a click toggles the parameter (with a gesture), the host moves the switch.
    int toggles = 0;
    forEach (*h.window, [&] (juce::Component& c) {
        auto* labeled = dynamic_cast<LabeledToggle*> (&c);
        if (labeled == nullptr)
            return;
        const auto id = labeled->getComponentID();
        INFO ("switch " << id);
        auto* param = state.getParameter (id);
        REQUIRE (param != nullptr);
        reveal (h, bot, *labeled);
        REQUIRE (labeled->isShowing());
        auto& button = labeled->getButton();
        const bool before = param->getValue() > 0.5f;
        host.clear();
        bot.click (button, juce::Point<float> (18.0f, button.getHeight() * 0.5f)); // on the switch itself
        CHECK ((param->getValue() > 0.5f) != before);
        CHECK (button.getToggleState() == (param->getValue() > 0.5f));
        CHECK (host.sawGesture (param->getParameterIndex()));
        param->setValueNotifyingHost (before ? 1.0f : 0.0f);
        pump (30);
        CHECK (button.getToggleState() == before);
        covered.insert (id);
        ++toggles;
    });
    CHECK (toggles >= 13);

    // Everything else has its own control: the note keyboard (states and detunes,
    // tested in NotesHarmonyTests) and the Auto / Gráfico switch.
    for (auto* p : h.p().getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = ranged->getParameterID();
            const bool ownControl = id.startsWith ("noteState") || id.startsWith ("noteDetune") || id == ParamIDs::graphMode;
            INFO ("parameter without a control: " << id);
            CHECK ((covered.count (id) > 0 || ownControl));
        }
}

TEST_CASE ("Knobs: typed values, double-click to default, mouse wheel", "[ui][controls]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    auto& knob = h.get<LabeledKnob> (ParamIDs::retuneSpeed);
    auto& slider = knob.getSlider();

    // Click the value, type, Enter.
    juce::Label* valueBox = nullptr;
    for (auto* c : slider.getChildren())
        if (auto* l = dynamic_cast<juce::Label*> (c))
            valueBox = l;
    REQUIRE (valueBox != nullptr);
    bot.click (*valueBox);
    auto* typing = valueBox->getCurrentTextEditor();
    REQUIRE (typing != nullptr);
    typing->setText ("123");
    bot.key (*typing, juce::KeyPress (juce::KeyPress::returnKey));
    pump (60);
    CHECK (h.param (ParamIDs::retuneSpeed) == Approx (123.0f).margin (0.6f));
    CHECK (valueBox->getText().contains ("123"));

    // Units and decimal commas are accepted too (A4 = "442,5 Hz").
    auto& a4 = h.get<LabeledKnob> (ParamIDs::referenceA4).getSlider();
    for (auto* c : a4.getChildren())
        if (auto* l = dynamic_cast<juce::Label*> (c))
        {
            bot.click (*l);
            REQUIRE (l->getCurrentTextEditor() != nullptr);
            l->getCurrentTextEditor()->setText ("442 Hz");
            bot.key (*l->getCurrentTextEditor(), juce::KeyPress (juce::KeyPress::returnKey));
        }
    pump (60);
    CHECK (h.param (ParamIDs::referenceA4) == Approx (442.0f).margin (0.05f));

    // Double-click: back to the default.
    const auto rotary = slider.getLocalBounds().toFloat().withTrimmedBottom (18.0f).getCentre();
    bot.doubleClick (slider, rotary);
    auto* param = h.p().getValueTreeState().getParameter (ParamIDs::retuneSpeed);
    CHECK (param->getValue() == Approx (param->getDefaultValue()).margin (1.0e-4));

    // Wheel up raises the value.
    const float before = param->getValue();
    bot.wheel (slider, rotary, 0.5f);
    CHECK (param->getValue() > before);
}

//==============================================================================
TEST_CASE ("Presets: choose, previous / next, modified marker, save in the dialog, delete", "[ui][presets]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    auto& box = h.get<juce::ComboBox> ("presetBox");
    auto& pm = h.p().getPresetManager();

    // Factory presets listed under their heading.
    juce::StringArray items;
    for (int i = 0; i < box.getNumItems(); ++i)
        items.add (box.getItemText (i));
    CHECK (items.contains (u8 ("Pop moderno")));
    CHECK (items.contains (u8 ("Hard tune")));
    CHECK (box.getText() == u8 ("Por defecto"));

    // Choose one in the list.
    box.setSelectedItemIndex (items.indexOf (u8 ("Hard tune")), juce::sendNotificationSync);
    pump (60);
    CHECK (pm.getCurrentPresetName() == u8 ("Hard tune"));
    CHECK (h.param (ParamIDs::retuneSpeed) == Approx (0.0f));

    // Next / previous wrap around the list.
    bot.click (h.get<juce::Button> ("presetNext"));
    CHECK (box.getText() == items[items.indexOf (u8 ("Hard tune")) + 1]);
    bot.click (h.get<juce::Button> ("presetPrev"));
    bot.click (h.get<juce::Button> ("presetPrev"));
    CHECK (box.getText() == items[items.indexOf (u8 ("Hard tune")) - 1]);
    for (int i = 0; i < items.size(); ++i)
        bot.click (h.get<juce::Button> ("presetNext"));
    CHECK (box.getText() == items[items.indexOf (u8 ("Hard tune")) - 1]);

    // A sound edit marks it "*", a song edit doesn't; choosing it again reverts.
    const auto current = box.getText();
    h.setParam (ParamIDs::key, 7.0f);
    pump (300); // several UI ticks: still no "*"
    CHECK (box.getText() == current);
    h.setParam (ParamIDs::tolerance, 77.0f);
    CHECK (waitFor ([&] { return box.getText() == current + " *"; }));
    box.setSelectedItemIndex (items.indexOf (current), juce::sendNotificationSync);
    CHECK (waitFor ([&] { return box.getText() == current; }));
    CHECK (h.param (ParamIDs::tolerance) != Approx (77.0f));
    CHECK (h.param (ParamIDs::key) == Approx (7.0f)); // the song setting stays

    // Save: the dialog takes a name, "Guardar" writes the file and lists it.
    h.setParam (ParamIDs::retuneSpeed, 42.0f);
    bot.click (h.get<juce::Button> ("presetSave"));
    auto* dialog = UiTestAccess::saveDialog (h.ed());
    REQUIRE (dialog != nullptr);
    REQUIRE (dialog->isShowing());
    dialog->getTextEditor ("name")->setText (u8 ("Mi voz ñ"));
    bot.click (dialogButton (*dialog, u8 ("Guardar")));
    CHECK (waitFor ([&] { return UiTestAccess::saveDialog (h.ed()) == nullptr; }));
    CHECK (pm.getUserFolder().getChildFile (u8 ("Mi voz ñ") + PresetManager::kFileExtension).existsAsFile());
    CHECK (box.getText() == u8 ("Mi voz ñ"));
    juce::StringArray after;
    for (int i = 0; i < box.getNumItems(); ++i)
        after.add (box.getItemText (i));
    CHECK (after.contains (u8 ("Mi voz ñ")));

    // It reloads: change the sound, choose it, the saved value is back.
    h.setParam (ParamIDs::retuneSpeed, 300.0f);
    CHECK (waitFor ([&] { return box.getText() == u8 ("Mi voz ñ *"); })); // at the next UI tick
    box.setSelectedItemIndex (after.indexOf (u8 ("Mi voz ñ")), juce::sendNotificationSync);
    pump (60);
    CHECK (h.param (ParamIDs::retuneSpeed) == Approx (42.0f));

    // Cancel leaves everything as it was.
    bot.click (h.get<juce::Button> ("presetSave"));
    dialog = UiTestAccess::saveDialog (h.ed());
    REQUIRE (dialog != nullptr);
    dialog->getTextEditor ("name")->setText ("No guardar");
    bot.click (dialogButton (*dialog, u8 ("Cancelar")));
    CHECK (waitFor ([&] { return UiTestAccess::saveDialog (h.ed()) == nullptr; }));
    CHECK_FALSE (pm.getUserFolder().getChildFile (juce::String ("No guardar") + PresetManager::kFileExtension).existsAsFile());

    // Delete the user preset from the options menu.
    bool enabled = false;
    const int del = itemId (UiTestAccess::optionsMenu (h.ed()), u8 ("Borrar preset de usuario «Mi voz ñ»"), &enabled);
    REQUIRE (del > 0);
    CHECK (enabled);
    UiTestAccess::chooseOption (h.ed(), del);
    pump (60);
    CHECK_FALSE (pm.getUserFolder().getChildFile (u8 ("Mi voz ñ") + PresetManager::kFileExtension).existsAsFile());
    box.setSelectedItemIndex (0, juce::sendNotificationSync); // a factory preset: deleting is disabled
    pump (60);
    itemId (UiTestAccess::optionsMenu (h.ed()), u8 ("Borrar preset de usuario"), &enabled);
    CHECK_FALSE (enabled);
    h.shot ("presets");
}

TEST_CASE ("A/B buttons switch sounds; the options menu copies one into the other", "[ui][presets]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    auto& a = h.get<juce::Button> ("abA");
    auto& b = h.get<juce::Button> ("abB");
    CHECK (a.getToggleState());

    h.setParam (ParamIDs::retuneSpeed, 20.0f);
    bot.click (b);
    CHECK (b.getToggleState());
    CHECK (h.p().getPresetManager().getActiveSlot() == 1);
    h.setParam (ParamIDs::retuneSpeed, 250.0f);
    h.setParam (ParamIDs::key, 3.0f); // song: never part of A/B
    bot.click (a);
    CHECK (h.param (ParamIDs::retuneSpeed) == Approx (20.0f));
    CHECK (h.param (ParamIDs::key) == Approx (3.0f));
    bot.click (b);
    CHECK (h.param (ParamIDs::retuneSpeed) == Approx (250.0f));

    // Menu text follows the active slot; copying B -> A makes A equal.
    const int copy = itemId (UiTestAccess::optionsMenu (h.ed()), u8 ("Copiar el sonido B en A"));
    REQUIRE (copy > 0);
    UiTestAccess::chooseOption (h.ed(), copy);
    bot.click (a);
    CHECK (h.param (ParamIDs::retuneSpeed) == Approx (250.0f));
    CHECK (itemId (UiTestAccess::optionsMenu (h.ed()), u8 ("Copiar el sonido A en B")) > 0);

    // The host (e.g. a preset of FL's) changing the slot moves the buttons.
    h.p().getPresetManager().selectSlot (1);
    CHECK (waitFor ([&] { return b.getToggleState(); }));
    CHECK_FALSE (a.getToggleState());
}

TEST_CASE ("Options menu: items, accent colour saved with the project", "[ui][options]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    CHECK (h.clickOpensMenu (bot, h.get<juce::Button> ("options"), {}, false, "menu-opciones"));

    const auto menu = UiTestAccess::optionsMenu (h.ed());
    for (const char* text : { "Abrir carpeta de presets", "Abrir carpeta de capturas de audio", "Copiar el sonido A en B",
                              "Quitar todas las asignaciones MIDI", "Turquesa", "Violeta" })
    {
        INFO (text);
        CHECK (itemId (menu, u8 (text)) > 0);
    }
    const int turquoise = itemId (menu, u8 ("Turquesa"));
    UiTestAccess::chooseOption (h.ed(), turquoise);
    CHECK (UiTestAccess::accent (h.ed()) == juce::Colour (0xff2ec4b6)); // synchronous
    h.shot ("accent-turquesa");

    // Saved in the state: a reopened editor (or a reloaded project) shows it.
    juce::MemoryBlock saved;
    h.p().getStateInformation (saved);
    h.closeEditor();
    h.p().setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    h.openEditor();
    CHECK (UiTestAccess::accent (h.ed()) == juce::Colour (0xff2ec4b6));
    bool ticked = false;
    for (const auto& e : entries (UiTestAccess::optionsMenu (h.ed())))
        if (e.text == u8 ("Turquesa"))
            ticked = e.ticked;
    CHECK (ticked);
}

//==============================================================================
TEST_CASE ("MIDI Learn from the knob menu: a CC moves the knob and the caption shows it", "[ui][midi]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    auto& knob = h.get<LabeledKnob> (ParamIDs::retuneSpeed);
    juce::Label* caption = nullptr;
    for (auto* c : knob.getChildren())
        if (auto* l = dynamic_cast<juce::Label*> (c); l != nullptr && l->getComponentID() != "ccBadge")
            caption = l;
    REQUIRE (caption != nullptr);

    // Right click on the knob opens its menu.
    CHECK (h.clickOpensMenu (bot, knob.getSlider(), {}, true));

    auto menu = UiTestAccess::knobMenu (h.ed(), knob);
    const int learn = itemId (menu, u8 ("Asignar a controlador MIDI"));
    REQUIRE (learn > 0);
    UiTestAccess::chooseKnobItem (h.ed(), knob, learn);
    CHECK (waitFor ([&] { return caption->getText() == u8 ("Mueve un CC…"); }));
    {
        const auto report = checkLayout (*h.window);
        INFO (report.summary());
        CHECK (report.issues.empty()); // the learning caption fits too
    }
    CHECK (itemId (UiTestAccess::knobMenu (h.ed(), knob), u8 ("Cancelar aprendizaje MIDI")) > 0);

    juce::MidiBuffer cc;
    cc.addEvent (juce::MidiMessage::controllerEvent (1, 21, 100), 0);
    h.sendMidi (cc);
    auto* param = h.p().getValueTreeState().getParameter (ParamIDs::retuneSpeed);
    CHECK (waitFor ([&] { return std::abs (param->getValue() - 100.0f / 127.0f) < 0.01f; })); // applied by the 30 Hz timer
    CHECK (waitFor ([&] { return caption->getText() == u8 ("Velocidad"); }));
    CHECK (knob.getSlider().getValue() == Approx (param->convertFrom0to1 (100.0f / 127.0f)).margin (0.5));
    CHECK (caption->getText() == u8 ("Velocidad"));
    auto& badge = get<juce::Label> (knob, "ccBadge");
    CHECK (badge.isShowing());
    CHECK (badge.getText() == "CC21");

    juce::MidiBuffer cc2;
    cc2.addEvent (juce::MidiMessage::controllerEvent (1, 21, 0), 0);
    h.sendMidi (cc2);
    CHECK (waitFor ([&] { return param->getValue() < 0.01f; }));

    // The knob with the longest caption, mapped to a 3-digit CC: everything still fits.
    auto& a4 = h.get<LabeledKnob> (ParamIDs::referenceA4);
    UiTestAccess::chooseKnobItem (h.ed(), a4, itemId (UiTestAccess::knobMenu (h.ed(), a4), u8 ("Asignar a controlador MIDI")));
    juce::MidiBuffer cc3;
    cc3.addEvent (juce::MidiMessage::controllerEvent (1, 119, 64), 0);
    h.sendMidi (cc3);
    CHECK (waitFor ([&] { return get<juce::Label> (a4, "ccBadge").getText() == "CC119"; }));
    {
        const auto report = checkLayout (*h.window);
        INFO (report.summary());
        CHECK (report.issues.empty());
    }
    h.shot ("midi-learn");

    // The mapping is part of the project; "Quitar" in the menu removes it.
    CHECK (itemId (UiTestAccess::knobMenu (h.ed(), knob), u8 ("Quitar asignación (CC 21)")) > 0);
    UiTestAccess::chooseOption (h.ed(), itemId (UiTestAccess::optionsMenu (h.ed()), u8 ("Quitar todas las asignaciones MIDI")));
    CHECK (waitFor ([&] { return ! badge.isVisible(); }));
    CHECK (caption->getText() == u8 ("Velocidad"));
    CHECK_FALSE (badge.isVisible());
    CHECK (h.p().getMidiLearn().getCcForParam (param->getParameterIndex()) < 0);
}

TEST_CASE ("Escuchar: the button counts the voiced seconds and the key is applied", "[ui][key]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    auto& listen = h.get<juce::Button> ("listen");
    CHECK (h.clickOpensMenu (bot, listen, {}, false, "menu-escuchar"));

    auto menu = UiTestAccess::listenMenu (h.ed());
    bool fromCapture = true;
    itemId (menu, u8 ("Desde la captura"), &fromCapture);
    CHECK_FALSE (fromCapture); // nothing captured yet
    const int ten = itemId (menu, u8 ("Escuchar 10 s"));
    REQUIRE (ten > 0);
    UiTestAccess::chooseListen (h.ed(), ten);
    CHECK (waitFor ([&] { return listen.getButtonText().startsWith (u8 ("Escuchando 0/10 s")); }));
    CHECK (UiTestAccess::statusText (h.ed()).startsWith (u8 ("Escuchando")));

    // E major melody, slightly sharp.
    const std::vector<int> melody { 64, 68, 71, 76, 75, 73, 71, 68, 66, 64, 64, 71, 68, 64, 69, 68, 66, 64, 59, 64 };
    h.feed (sungPhrase (melody, 12.0));
    CHECK (waitFor ([&] { return listen.getButtonText().startsWith (u8 ("Escuchando "))
                                 && ! listen.getButtonText().startsWith (u8 ("Escuchando 0/")); }));
    INFO (listen.getButtonText());

    // Stop and apply from the menu (the menu now offers it).
    const int stop = itemId (UiTestAccess::listenMenu (h.ed()), u8 ("Detener y aplicar"));
    REQUIRE (stop > 0);
    UiTestAccess::chooseListen (h.ed(), stop);
    CHECK (waitFor ([&] { return listen.getButtonText() == u8 ("Escuchar"); }));
    const auto r = h.p().getLastKeyResult();
    REQUIRE (r.valid);
    CHECK (r.key == 4);
    CHECK_FALSE (r.minor);
    CHECK (juce::roundToInt (h.param (ParamIDs::key)) == 4);
    CHECK (h.get<LabeledCombo> (ParamIDs::key).getCombo().getText() == "E");
    CHECK (UiTestAccess::statusText (h.ed()).startsWith (u8 ("Tonalidad detectada: E mayor")));
    h.shot ("escuchar-resultado");
}

TEST_CASE ("Sincronizar: disabled alone, enabled with another instance, sends the song settings", "[ui][sync]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    auto& sync = h.get<juce::Button> ("sync");
    pump (150);
    CHECK_FALSE (sync.isEnabled());
    CHECK (sync.getTooltip().contains (u8 ("No hay otras instancias")));

    Harness other (PluginEditor::kBaseWidth, false); // a second track, editor closed
    CHECK (waitFor ([&] { return sync.isEnabled(); }));

    h.setParam (ParamIDs::key, 9.0f);
    h.setParam (ParamIDs::scale, 1.0f);
    h.setParam (ParamIDs::noteDetune (4), -14.0f);
    bot.click (sync);
    CHECK (waitFor ([&] { return juce::roundToInt (other.param (ParamIDs::key)) == 9; })); // the other's timer applies it
    CHECK (juce::roundToInt (other.param (ParamIDs::scale)) == 1);
    CHECK (other.param (ParamIDs::noteDetune (4)) == Approx (-14.0f).margin (0.05f));
    CHECK (UiTestAccess::statusText (h.ed()).contains (u8 ("enviadas a 1 instancia más")));
}

//==============================================================================
TEST_CASE ("Meter, footer, note keyboard and MIDI page follow the audio and MIDI", "[ui][live]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    h.setParam (ParamIDs::scale, static_cast<float> (dsp::ScaleType::Major)); // C major
    bot.click (h.get<juce::Button> ("tab1")); // Notas: live highlight

    // A sung E4, 30 cents flat: detected E, target E.
    // Checked while the audio still flows: when the host stops calling the
    // plugin, the meter and the highlights fade out on purpose.
    const auto sung = signals::sawtooth (dsp::midiToHz (63.7f, 440.0f), Harness::kSampleRate, 1.2);
    h.feed (sung, nullptr, nullptr, true, false);
    const auto& frame = UiTestAccess::lastFrame (h.ed());
    CHECK (frame.voiced);
    CHECK (dsp::hzToMidi (frame.frequencyHz, 440.0f) == Approx (63.7f).margin (0.1f));
    CHECK (frame.targetMidi == Approx (64.0f).margin (0.05f));
    auto& keyboard = UiTestAccess::keyboard (h.ed());
    CHECK (UiTestAccess::detected (keyboard) == 4);
    CHECK (UiTestAccess::target (keyboard) == 4);
    h.shot ("notas-en-vivo");

    CHECK (waitFor ([&] { return ! UiTestAccess::lastFrame (h.ed()).voiced; })); // faded out
    CHECK (waitFor ([&] { return UiTestAccess::detected (keyboard) == -1; }));

    const auto footer = UiTestAccess::footerText (h.ed());
    INFO (footer);
    CHECK (footer.contains ("48.0 kHz"));
    CHECK (footer.contains (u8 ("bloques 256–256")));
    CHECK (footer.contains (u8 ("Estéreo → Estéreo")));
    CHECK (footer.contains (u8 ("latencia ") + juce::String (h.p().getLatencySamples())));
    CHECK (footer.contains (juce::String ("v") + NOVAPITCH_VERSION_STRING));

    // MIDI page: held notes are listed, the counter in the footer moves.
    bot.click (h.get<juce::Button> ("tab6"));
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    on.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0);
    h.sendMidi (on);
    CHECK (waitFor ([&] { return UiTestAccess::heldNotesText (h.ed()) == "C4  E4"; }));
    CHECK (waitFor ([&] { return UiTestAccess::footerText (h.ed()).contains ("MIDI 2"); }));
    h.shot ("midi-notas");
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    off.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
    h.sendMidi (off);
    CHECK (waitFor ([&] { return UiTestAccess::heldNotesText (h.ed()).isEmpty(); }));
}
