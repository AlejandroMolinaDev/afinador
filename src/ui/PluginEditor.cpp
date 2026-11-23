#include "ui/PluginEditor.h"

#include "dsp/InputProfile.h"
#include "dsp/PitchUtils.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

namespace novapitch
{

namespace
{
/** UI strings are Spanish and may contain non-ASCII characters: always build
    them from UTF-8 explicitly (juce::String(const char*) asserts on non-ASCII). */
juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

juce::String binaryArchitecture()
{
   #if JUCE_ARM && JUCE_64BIT
    return "arm64";
   #elif JUCE_INTEL && JUCE_64BIT
    return "x86_64";
   #elif JUCE_INTEL
    return "x86 (32-bit)";
   #else
    return "unknown";
   #endif
}

juce::String channelName (int n)
{
    switch (n)
    {
        case 0:  return u8 ("—");
        case 1:  return "Mono";
        case 2:  return u8 ("Estéreo");
        default: return juce::String (n) + " ch";
    }
}

constexpr const char* kAccentProperty = "accentColour";

struct AccentOption { const char* name; juce::uint32 argb; };
constexpr AccentOption kAccents[] = {
    { "Coral", 0xffff6b4a }, { "Ámbar", 0xfff2a93b }, { "Turquesa", 0xff2ec4b6 },
    { "Azul", 0xff4c8dff }, { "Violeta", 0xff9b6bff }, { "Rosa", 0xffff5c9a },
};

// Layout (base coordinates)
const juce::Rectangle<int> kModeSwitch    { 200, 26, 124, 28 };
const juce::Rectangle<int> kPresetBar     { 334, 26, 306, 28 };
const juce::Rectangle<int> kGraphBounds   { 20, 70, 720, 540 };
const juce::Rectangle<int> kMeterBounds   { 20, 76, 470, 222 };
const juce::Rectangle<int> kSidePanel     { 506, 76, 234, 222 };
const juce::Rectangle<int> kTabBar        { 20, 310, 720, 28 };
const juce::Rectangle<int> kPagePanel     { 20, 342, 720, 142 };
const juce::Rectangle<int> kTraceBounds   { 20, 496, 720, 112 };
const juce::Rectangle<int> kFooterBounds  { 20, 616, 720, 16 };
} // namespace

//==============================================================================
PluginEditor::PluginEditor (NovaPitchProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    const juce::PluginHostType host;
    hostIsFlStudio = host.isFruityLoops();
    if (hostIsFlStudio)
        hostDescription = "FL Studio";
    else if (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        hostDescription = u8 ("Aplicación independiente");
    else
        hostDescription = host.getHostDescription();

    addAndMakeVisible (content);
    content.addAndMakeVisible (meter);
    content.addAndMakeVisible (trace);

    auto& state = p.getValueTreeState();

    // Key / scale / mode / profile ------------------------------------------------
    combos.push_back (std::make_unique<LabeledCombo> (state, ParamIDs::key, u8 ("Tonalidad"),
        u8 ("Nota de la tonalidad. Junto con la escala define las notas destino.")));
    combos.push_back (std::make_unique<LabeledCombo> (state, ParamIDs::scale, u8 ("Escala"),
        u8 ("Notas válidas. Cromática = las 12 notas. Las notas fuera de escala se redirigen a la vecina válida.")));
    combos.push_back (std::make_unique<LabeledCombo> (state, ParamIDs::qualityMode, u8 ("Modo"),
        u8 ("Live: latencia mínima (~3 ms) para directo. Tracking: ~10 ms, preciso para grabar. "
            "HQ: motor de alta calidad (~55 ms) para mezcla y exportación. "
            "Vintage: efecto hard tune clásico, granos secos y formantes sin procesar.")));
    combos.push_back (std::make_unique<LabeledCombo> (state, ParamIDs::inputProfile, u8 ("Perfil de entrada"),
        u8 ("Rango de frecuencias en el que se busca la nota. Un rango ajustado reduce errores de octava.")));
    for (auto& c : combos)
        content.addAndMakeVisible (*c);

    // Knobs ----------------------------------------------------------------------
    knobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::retuneSpeed, u8 ("Velocidad"),
        u8 ("Tiempo que tarda la corrección (0–400 ms). 0 = salto instantáneo (efecto hard tune); "
            "valores altos = corrección gradual y natural que deja pasar el vibrato.")));
    knobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::tolerance, u8 ("Tolerancia"),
        u8 ("Solo corrige cuando la voz ya está cerca de la nota (0–100 cents). Deja pasar glissandos, "
            "scoops y adornos. 0 = siempre corrige.")));
    knobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::naturalHold, u8 ("Sostenido"),
        u8 ("Sostenido natural: en notas largas y estables suaviza la corrección para conservar las "
            "micro-variaciones, manteniendo precisos los ataques.")));
    knobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::transpose, u8 ("Transposición"),
        u8 ("Desplaza la voz corregida ±12 semitonos.")));
    knobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::mix, u8 ("Mezcla"),
        u8 ("Proporción entre la señal original (con su retardo compensado) y la corregida.")));
    knobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::sensitivity, u8 ("Sensibilidad"),
        u8 ("Cuánto ruido o aire tolera la detección (0–100 %). Súbela para voces susurradas o tomas "
            "ruidosas; bájala si la detección salta en respiraciones o sibilantes.")));
    knobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::referenceA4, u8 ("Referencia A4"),
        u8 ("Frecuencia del La 4 (400–480 Hz). 440 Hz es el estándar.")));
    for (auto& k : knobs)
        pages[0].addAndMakeVisible (*k);

    // Formants -------------------------------------------------------------------
    formantToggles.push_back (std::make_unique<LabeledToggle> (state, ParamIDs::formantPreserve, u8 ("Preservar formantes"),
        u8 ("Mantiene el timbre de la voz al cambiar el tono (evita la voz de \"ardilla\" o de \"monstruo\"). "
            "Desactívalo para que los formantes sigan al tono. En modo Vintage siempre está desactivado.")));
    formantToggles.push_back (std::make_unique<LabeledToggle> (state, ParamIDs::formantLink, u8 ("Enlazar a transposición"),
        u8 ("Los formantes se mueven junto con la Transposición (diseño vocal creativo), mientras la "
            "corrección de afinación sigue sin alterar el timbre.")));
    formantKnobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::formantShift, u8 ("Desplazar"),
        u8 ("Desplaza los formantes ±12 semitonos sin cambiar la afinación: voz más aguda/infantil (+) "
            "o más grave/oscura (−).")));
    formantKnobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::character, u8 ("Carácter"),
        u8 ("Escala la envolvente espectral ±50 %: simula un tracto vocal más corto (+) o más largo (−).")));
    for (auto& t : formantToggles)
        pages[2].addAndMakeVisible (*t);
    for (auto& k : formantKnobs)
        pages[2].addAndMakeVisible (*k);

    // Vibrato ------------------------------------------------------------------------
    vibratoKnobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::vibratoPreserve, u8 ("Conservar"),
        u8 ("Conservar vibrato (0–200 %): la corrección se aplica al centro de la nota y el vibrato natural "
            "se mantiene (100 %), se elimina (0 %) o se exagera (200 %). En Vintage siempre se elimina.")));
    vibratoKnobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::vibRate, u8 ("Velocidad"),
        u8 ("Vibrato artificial: frecuencia de la oscilación (0,1–10 Hz).")));
    vibratoKnobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::vibDepth, u8 ("Profundidad"),
        u8 ("Vibrato artificial: amplitud en cents (0 = desactivado).")));
    vibratoKnobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::vibDelay, u8 ("Retardo"),
        u8 ("Tiempo desde el inicio de cada nota hasta que entra el vibrato artificial (aparece con un fundido).")));
    vibratoKnobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::vibVariation, u8 ("Variación"),
        u8 ("Variación aleatoria de velocidad y profundidad para un vibrato menos mecánico.")));
    vibratoKnobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::vibAmpMod, u8 ("Amplitud"),
        u8 ("Modulación de volumen sincronizada con el vibrato (hasta ±25 %).")));
    vibratoKnobs.push_back (std::make_unique<LabeledKnob> (state, ParamIDs::vibFormantMod, u8 ("Formante"),
        u8 ("Cuánto siguen los formantes al vibrato artificial (0 = timbre fijo, 100 % = timbre sigue al tono).")));
    for (auto& k : vibratoKnobs)
        pages[3].addAndMakeVisible (*k);
    pages[3].addAndMakeVisible (vibratoDisplay);
    vibShapeBox.addItemList ({ u8 ("Seno"), u8 ("Triángulo"), u8 ("Cuadrada"), u8 ("Sierra") }, 1);
    vibShapeBox.setTooltip (u8 ("Forma del vibrato artificial."));
    vibShapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ParamIDs::vibShape, vibShapeBox);
    pages[3].addAndMakeVisible (vibShapeBox);
    vibratoDisplay.setAccent (lookAndFeel.getAccent());

    // Harmonies ----------------------------------------------------------------------
    for (int v = 0; v < 4; ++v)
    {
        auto& vp = voicePanels[static_cast<size_t> (v)];
        auto id = [v] (const char* field) { return ParamIDs::harmonyVoice (field, v + 1); };
        vp.mode = std::make_unique<LabeledCombo> (state, id ("hvMode"), u8 ("Modo"),
            u8 ("Diatónica: intervalo en grados de la escala actual (la 3ª será mayor o menor según la nota). "
                "Semitonos: intervalo fijo. MIDI: la voz canta la nota del acorde que tocas en el piano roll."));
        vp.degrees = std::make_unique<LabeledKnob> (state, id ("hvDegrees"), u8 ("Intervalo"),
            u8 ("Grados de la escala desde la nota de la voz principal: 3ª arriba, 5ª, 6ª abajo, 8ª..."));
        vp.semitones = std::make_unique<LabeledKnob> (state, id ("hvSemitones"), u8 ("Semitonos"),
            u8 ("Intervalo fijo en semitonos (±24)."));
        vp.formant = std::make_unique<LabeledKnob> (state, id ("hvFormant"), u8 ("Formante"),
            u8 ("Timbre de esta voz (±12 st) para diferenciarla de la principal."));
        vp.pan = std::make_unique<LabeledKnob> (state, id ("hvPan"), u8 ("Panorama"), u8 ("Posición estéreo de la voz."));
        vp.level = std::make_unique<LabeledKnob> (state, id ("hvLevel"), u8 ("Nivel"), u8 ("Volumen de la voz."));
        vp.humanize = std::make_unique<LabeledKnob> (state, id ("hvHumanize"), u8 ("Humanizar"),
            u8 ("Micro-desafinación lenta (hasta ±12 ¢) y retardo (hasta 30 ms) para que no suene a copia exacta."));
        vp.mute = std::make_unique<LabeledToggle> (state, id ("hvMute"), "Mute", u8 ("Silencia esta voz."));
        vp.solo = std::make_unique<LabeledToggle> (state, id ("hvSolo"), "Solo", u8 ("Escucha solo las voces en solo."));
        for (auto* c : std::initializer_list<juce::Component*> { vp.mode.get(), vp.degrees.get(), vp.semitones.get(), vp.formant.get(),
                                                                 vp.pan.get(), vp.level.get(), vp.humanize.get(), vp.mute.get(), vp.solo.get() })
            vp.panel.addAndMakeVisible (c);
        vp.mode->getCombo().onChange = [this] { updateVoiceVisibility(); };
        pages[4].addChildComponent (vp.panel);

        auto& b = voiceButtons[static_cast<size_t> (v)];
        b.setButtonText (u8 ("Voz ") + juce::String (v + 1));
        b.setRadioGroupId (2002);
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, Palette::panelRaised);
        b.setColour (juce::TextButton::textColourOffId, Palette::textMuted);
        b.setColour (juce::TextButton::textColourOnId, Palette::background);
        b.onClick = [this, v] { if (voiceButtons[static_cast<size_t> (v)].getToggleState()) selectVoice (v); };
        pages[4].addAndMakeVisible (b);
    }
    voiceButtons[0].setToggleState (true, juce::dontSendNotification);
    selectVoice (0);

    const std::pair<const char*, const char*> busControls[] = {
        { ParamIDs::hBusHpf, "Paso-alto" }, { ParamIDs::hBusLpf, "Paso-bajo" }, { ParamIDs::hBusEqFreq, "EQ frec." },
        { ParamIDs::hBusEqGain, "EQ ganancia" }, { ParamIDs::hBusAttack, "Ataque" }, { ParamIDs::hBusRelease, "Liberación" },
        { ParamIDs::hBusGate, "Gate" }, { ParamIDs::hBusWidth, "Ancho" } };
    const char* busTips[] = { "Filtro paso-alto del bus de armonías.", "Filtro paso-bajo del bus de armonías.",
                              "Frecuencia del ecualizador de campana.", "Ganancia de la campana (±12 dB).",
                              "Entrada de las voces al empezar una nota.", "Salida de las voces al terminar una nota.",
                              "Por debajo de este nivel de entrada las armonías se callan (evita armonías de ruido).",
                              "Ancho estéreo del bus (0 = mono, 200 % = muy abierto)." };
    for (size_t i = 0; i < std::size (busControls); ++i)
    {
        busKnobs.push_back (std::make_unique<LabeledKnob> (state, busControls[i].first, u8 (busControls[i].second), u8 (busTips[i])));
        pages[5].addAndMakeVisible (*busKnobs.back());
    }

    midiToggles.push_back (std::make_unique<LabeledToggle> (state, ParamIDs::midiTarget, u8 ("MIDI: nota objetivo"),
        u8 ("La nota MIDI sostenida (en la octava más cercana a la voz) es el objetivo de la corrección.")));
    midiToggles.push_back (std::make_unique<LabeledToggle> (state, ParamIDs::midiScaleLearn, u8 ("Aprender escala por MIDI"),
        u8 ("Mientras está activo, las notas que toques definen una escala personalizada (Escala = Personalizada).")));
    midiToggles.push_back (std::make_unique<LabeledToggle> (state, ParamIDs::midiTargetExact, u8 ("Octava exacta"),
        u8 ("Con «nota objetivo»: usa la nota MIDI en la octava tocada (para forzar saltos de octava). "
            "Desactivado: la nota más cercana a la voz en cualquier octava.")));
    midiToggles.push_back (std::make_unique<LabeledToggle> (state, ParamIDs::midiOut, u8 ("Enviar notas MIDI"),
        u8 ("Envía las notas corregidas como MIDI (para doblar la voz con un sintetizador). En FL Studio: "
            "⚙ → MIDI → Output port, y en el sintetizador el mismo puerto como entrada.")));
    for (auto& t : midiToggles)
        pages[6].addAndMakeVisible (*t);

    // Notes of the scale (Escala / Activa / Ignorar / Eliminar) ---------------------
    noteKeyboard = std::make_unique<NoteKeyboard> (state);
    pages[1].addAndMakeVisible (*noteKeyboard);

    // Tabs --------------------------------------------------------------------------
    const char* tabNames[] = { "Corrección", "Notas", "Formantes", "Vibrato", "Armonías", "Bus", "MIDI" };
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        auto& t = tabs[i];
        t.setButtonText (u8 (tabNames[i]));
        t.setRadioGroupId (1001);
        t.setClickingTogglesState (true);
        t.setColour (juce::TextButton::buttonColourId, Palette::panel);
        t.setColour (juce::TextButton::textColourOffId, Palette::textMuted);
        t.setColour (juce::TextButton::textColourOnId, Palette::background);
        t.onClick = [this, i] { if (tabs[i].getToggleState()) showPage (static_cast<Page> (i)); };
        content.addAndMakeVisible (t);
        content.addChildComponent (pages[i]);
    }
    tabs[0].setToggleState (true, juce::dontSendNotification);
    showPage (Page::Correction);

    // Preset bar ------------------------------------------------------------------
    presetBox.setTooltip (u8 ("Presets de fábrica y de usuario. Cambian el sonido, nunca la tonalidad, "
                              "la escala, las notas ni la referencia A4."));
    presetBox.onChange = [this] {
        const auto name = presetBox.getText();
        if (name.isNotEmpty() && name != processor.getPresetManager().getCurrentPresetName())
            processor.getPresetManager().loadPreset (name);
        shownPresetName = {};
    };
    prevPreset.setTooltip (u8 ("Preset anterior"));
    nextPreset.setTooltip (u8 ("Preset siguiente"));
    prevPreset.onClick = [this] { processor.getPresetManager().loadNext (-1); };
    nextPreset.onClick = [this] { processor.getPresetManager().loadNext (+1); };
    savePreset.setButtonText (u8 ("Guardar"));
    savePreset.setTooltip (u8 ("Guardar los ajustes actuales como preset de usuario"));
    savePreset.onClick = [this] { showSaveDialog(); };
    optionsButton.setButtonText (u8 ("···"));
    optionsButton.setTooltip (u8 ("Opciones: color de acento, carpeta de presets, borrar preset"));
    optionsButton.onClick = [this] { showOptionsMenu(); };
    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &prevPreset, &nextPreset, &savePreset, &optionsButton })
        content.addAndMakeVisible (c);
    refreshPresetBox();

    // Auto / Graphical mode -------------------------------------------------------
    autoButton.setButtonText ("Auto");
    autoButton.setTooltip (u8 ("Modo automático: corrige en tiempo real según tonalidad y escala (o MIDI)."));
    graphButton.setButtonText (u8 ("Gráfico"));
    graphButton.setTooltip (u8 ("Modo gráfico: captura la afinación reproduciendo la pista en FL Studio y edita notas, "
                                "líneas y curvas. Durante la reproducción manda lo dibujado; fuera de los objetos pasa la voz original."));
    for (auto* b : { &autoButton, &graphButton })
    {
        b->setRadioGroupId (1003);
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId, Palette::panelRaised);
        b->setColour (juce::TextButton::textColourOffId, Palette::textMuted);
        b->setColour (juce::TextButton::textColourOnId, Palette::background);
        content.addAndMakeVisible (b);
    }
    autoButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    graphButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    autoButton.onClick = [this] { if (autoButton.getToggleState()) setGraphMode (false); };
    graphButton.onClick = [this] { if (graphButton.getToggleState()) setGraphMode (true); };
    graphEditor = std::make_unique<GraphEditor> (p);
    content.addChildComponent (*graphEditor);

    // Key detection + instance sync (over the Tonalidad / Escala labels) -------------
    listenButton.setButtonText (u8 ("Escuchar"));
    listenButton.setTooltip (u8 ("Detecta la tonalidad escuchando la voz unos segundos (o desde la captura del modo "
                                 "gráfico) y ajusta Tonalidad y Escala."));
    listenButton.onClick = [this] { showListenMenu(); };
    syncButton.setButtonText (u8 ("Sincronizar"));
    syncButton.onClick = [this] {
        const int n = processor.syncToOtherInstances();
        showStatus (n > 0 ? u8 ("Tonalidad, escala, notas y A4 enviadas a ") + juce::String (n)
                                + (n == 1 ? u8 (" instancia más") : u8 (" instancias más"))
                          : u8 ("No hay otras instancias de NovaPitch en este proceso (¿plugin en modo «bridged»?)"));
    };
    for (auto* b : { &listenButton, &syncButton })
    {
        b->setColour (juce::TextButton::buttonColourId, Palette::panelRaised);
        b->setColour (juce::TextButton::textColourOffId, Palette::textPrimary.withAlpha (0.85f));
        content.addAndMakeVisible (b);
    }

    // MIDI Learn on every knob ---------------------------------------------------
    for (auto* k : allKnobs())
        k->onContextMenu = [this] (LabeledKnob& knob) { showKnobMenu (knob); };

    // Accent colour saved with the project
    const auto stored = p.getValueTreeState().state.getProperty (kAccentProperty).toString();
    if (stored.isNotEmpty())
        applyAccent (juce::Colour::fromString (stored));

    meter.setAccent (lookAndFeel.getAccent());
    trace.setAccent (lookAndFeel.getAccent());
    graphEditor->setAccent (lookAndFeel.getAccent());
    noteKeyboard->setAccent (lookAndFeel.getAccent());

    setResizable (true, true);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);
    if (auto* sizeConstrainer = getConstrainer())
        sizeConstrainer->setFixedAspectRatio (static_cast<double> (kBaseWidth) / kBaseHeight);
    setSize (kBaseWidth, kBaseHeight);

    footerText = buildFooterText();
    updateView();
    startTimerHz (30);
}

std::vector<LabeledKnob*> PluginEditor::allKnobs() const
{
    std::vector<LabeledKnob*> out;
    for (const auto* group : { &knobs, &formantKnobs, &vibratoKnobs, &busKnobs })
        for (const auto& k : *group)
            out.push_back (k.get());
    for (const auto& vp : voicePanels)
        for (auto* k : { vp.degrees.get(), vp.semitones.get(), vp.formant.get(), vp.pan.get(), vp.level.get(), vp.humanize.get() })
            if (k != nullptr)
                out.push_back (k);
    return out;
}

void PluginEditor::refreshPresetBox()
{
    auto& pm = processor.getPresetManager();
    const auto presets = pm.getPresets();
    presetBox.clear (juce::dontSendNotification);

    bool userHeading = false;
    presetBox.addSectionHeading (u8 ("Fábrica"));
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (! presets[i].factory && ! userHeading)
        {
            presetBox.addSeparator();
            presetBox.addSectionHeading (u8 ("Usuario"));
            userHeading = true;
        }
        presetBox.addItem (presets[i].name, static_cast<int> (i) + 1);
    }

    shownPresetName = pm.getCurrentPresetName();
    const int index = pm.getCurrentIndex();
    if (index >= 0)
        presetBox.setSelectedId (index + 1, juce::dontSendNotification);
    else
        presetBox.setText (shownPresetName, juce::dontSendNotification);
}

void PluginEditor::showSaveDialog()
{
    saveDialog = std::make_unique<juce::AlertWindow> (u8 ("Guardar preset"), u8 ("Nombre del preset de usuario:"),
                                                      juce::MessageBoxIconType::NoIcon, this);
    const auto current = processor.getPresetManager().getCurrentPresetName();
    saveDialog->addTextEditor ("name", current == PresetManager::kDefaultPresetName ? juce::String() : current);
    saveDialog->addButton (u8 ("Guardar"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton (u8 ("Cancelar"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result) {
        if (result == 1 && saveDialog != nullptr)
        {
            const auto name = saveDialog->getTextEditorContents ("name");
            if (processor.getPresetManager().saveUserPreset (name))
                refreshPresetBox();
        }
        saveDialog.reset();
    }));
}

void PluginEditor::applyAccent (juce::Colour c)
{
    lookAndFeel.setAccent (c);
    meter.setAccent (c);
    trace.setAccent (c);
    vibratoDisplay.setAccent (c);
    if (noteKeyboard != nullptr)
        noteKeyboard->setAccent (c);
    if (graphEditor != nullptr)
        graphEditor->setAccent (c);
    processor.getValueTreeState().state.setProperty (kAccentProperty, c.toString(), nullptr);
    sendLookAndFeelChange();
    repaint();
}

void PluginEditor::showOptionsMenu()
{
    auto& pm = processor.getPresetManager();
    const auto current = pm.getCurrentPresetName();
    bool currentIsUser = false;
    for (const auto& preset : pm.getPresets())
        currentIsUser = currentIsUser || (! preset.factory && preset.name == current);

    juce::PopupMenu accents;
    for (size_t i = 0; i < std::size (kAccents); ++i)
        accents.addItem (100 + static_cast<int> (i), u8 (kAccents[i].name), true,
                         lookAndFeel.getAccent() == juce::Colour (kAccents[i].argb));

    juce::PopupMenu menu;
    menu.addSubMenu (u8 ("Color de acento"), accents);
    menu.addItem (1, u8 ("Abrir carpeta de presets"));
    menu.addItem (2, u8 ("Borrar preset de usuario «") + current + u8 ("»"), currentIsUser);
    menu.addItem (4, u8 ("Abrir carpeta de capturas de audio (modo gráfico)"));
    menu.addSeparator();
    menu.addItem (3, u8 ("Quitar todas las asignaciones MIDI"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton),
                        [this, current] (int result) {
                            auto& presets = processor.getPresetManager();
                            if (result >= 100)
                                applyAccent (juce::Colour (kAccents[static_cast<size_t> (result - 100)].argb));
                            else if (result == 1)
                            {
                                presets.getUserFolder().createDirectory();
                                presets.getUserFolder().startAsProcess();
                            }
                            else if (result == 2 && presets.deleteUserPreset (current))
                                refreshPresetBox();
                            else if (result == 3)
                                processor.getMidiLearn().clearAll();
                            else if (result == 4)
                            {
                                processor.getCaptureRoot().createDirectory();
                                processor.getCaptureRoot().startAsProcess();
                            }
                        });
}

void PluginEditor::showKnobMenu (LabeledKnob& knob)
{
    auto& learn = processor.getMidiLearn();
    const int index = knob.getParameterIndex();
    const int cc = learn.getCcForParam (index);

    juce::PopupMenu menu;
    menu.addSectionHeader (knob.getCaption());
    if (learn.getLearningParam() == index)
        menu.addItem (2, u8 ("Cancelar aprendizaje MIDI"));
    else
        menu.addItem (1, u8 ("Asignar a controlador MIDI (aprender)"));
    if (cc >= 0)
        menu.addItem (3, u8 ("Quitar asignación (CC ") + juce::String (cc) + ")");
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_VST3)
    {
        menu.addSeparator();
        menu.addItem (4, u8 ("En VST3, FL Studio no envía CC al efecto: usa «Link to controller» de FL "
                             "o la versión CLAP"), false);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&knob.getSlider()), [this, index] (int result) {
        auto& l = processor.getMidiLearn();
        if (result == 1)
            l.startLearning (index);
        else if (result == 2)
            l.cancelLearning();
        else if (result == 3)
            l.clearMapping (index);
        refreshMidiLabels();
    });
}

void PluginEditor::refreshMidiLabels()
{
    auto& learn = processor.getMidiLearn();
    const int learning = learn.getLearningParam();
    for (auto* k : allKnobs())
        k->setMidiState (learn.getCcForParam (k->getParameterIndex()), learning == k->getParameterIndex(), lookAndFeel.getAccent());
    shownMidiGeneration = processor.getMidiMappingGeneration();
    shownLearning = learning;
}

void PluginEditor::selectVoice (int v)
{
    selectedVoice = juce::jlimit (0, 3, v);
    for (int i = 0; i < 4; ++i)
        voicePanels[static_cast<size_t> (i)].panel.setVisible (i == selectedVoice);
    updateVoiceVisibility();
}

void PluginEditor::updateVoiceVisibility()
{
    for (int i = 0; i < 4; ++i)
    {
        auto& vp = voicePanels[static_cast<size_t> (i)];
        const int mode = vp.mode->getCombo().getSelectedId() - 1;
        vp.degrees->setVisible (mode != 2);   // diatonic (and off/MIDI: shows the default)
        vp.semitones->setVisible (mode == 2); // semitones
        static const char* modeShort[] = { "", " · Diat.", " · Semit.", " · MIDI" };
        voiceButtons[static_cast<size_t> (i)].setButtonText (u8 ("Voz ") + juce::String (i + 1)
                                                             + u8 (modeShort[juce::jlimit (0, 3, mode)]));
    }
}

void PluginEditor::setGraphMode (bool on)
{
    if (auto* param = processor.getValueTreeState().getParameter (ParamIDs::graphMode))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
    updateView();
}

void PluginEditor::showStatus (const juce::String& text)
{
    statusText = text;
    statusUntil = juce::Time::getMillisecondCounter() + 7000;
    content.repaint (kFooterBounds);
}

void PluginEditor::showListenMenu()
{
    juce::PopupMenu menu;
    if (processor.isKeyListening())
    {
        menu.addItem (1, u8 ("Detener y aplicar"));
        menu.addItem (2, u8 ("Cancelar"));
    }
    else
    {
        menu.addSectionHeader (u8 ("Detectar la tonalidad"));
        menu.addItem (10, u8 ("Escuchar 10 s de voz"));
        menu.addItem (20, u8 ("Escuchar 20 s de voz"));
        menu.addItem (30, u8 ("Escuchar 30 s de voz"));
        menu.addSeparator();
        menu.addItem (100, u8 ("Desde la captura del modo gráfico"), ! processor.getGraphModel().getCaptured().empty());
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (listenButton), [this] (int result) {
        if (result == 1)
            processor.finishKeyListening (true);
        else if (result == 2)
            processor.finishKeyListening (false);
        else if (result == 10 || result == 20 || result == 30)
        {
            processor.startKeyListening (static_cast<double> (result));
            showStatus (u8 ("Escuchando: canta o reproduce la voz (cuentan solo los tramos con voz)…"));
        }
        else if (result == 100)
            processor.detectKeyFromCapture (true);
        updateKeyControls();
    });
}

void PluginEditor::updateKeyControls()
{
    if (processor.isKeyListening())
        listenButton.setButtonText (u8 ("Escuchando ")
                                    + juce::String (juce::jmin (processor.getKeyListenSeconds(), processor.getKeyListenTarget()), 0)
                                    + "/" + juce::String (processor.getKeyListenTarget(), 0) + " s");
    else
        listenButton.setButtonText (u8 ("Escuchar"));

    if (processor.getKeyResultGeneration() != shownKeyGeneration)
    {
        shownKeyGeneration = processor.getKeyResultGeneration();
        const auto r = processor.getLastKeyResult();
        if (r.valid)
            showStatus (u8 ("Tonalidad detectada: ") + u8 (dsp::kKeyNames[static_cast<size_t> (r.key)]) + " "
                        + u8 (dsp::kScaleNames[static_cast<size_t> (r.scale())]).toLowerCase() + u8 (" · confianza ")
                        + juce::String (juce::roundToInt (r.confidence * 100.0f)) + " %");
        else
            showStatus (u8 ("No hubo suficiente voz para detectar la tonalidad."));
    }

    const int instances = NovaPitchProcessor::getNumInstancesInProcess();
    syncButton.setEnabled (instances > 1);
    syncButton.setTooltip (instances > 1
                               ? u8 ("Envía tonalidad, escala, estados de nota y A4 a las otras ") + juce::String (instances - 1)
                                     + u8 (" instancias de NovaPitch del proyecto.")
                               : u8 ("No hay otras instancias de NovaPitch en este proceso. Si FL abre el plugin en modo "
                                     "«bridged» (proceso aparte), la sincronización no está disponible."));
}

void PluginEditor::updateView()
{
    const bool wantGraph = processor.getValueTreeState().getRawParameterValue (ParamIDs::graphMode)->load() > 0.5f;
    autoButton.setToggleState (! wantGraph, juce::dontSendNotification);
    graphButton.setToggleState (wantGraph, juce::dontSendNotification);
    if (wantGraph == graphView && graphEditor->isVisible() == wantGraph)
        return;
    graphView = wantGraph;

    meter.setVisible (! graphView);
    trace.setVisible (! graphView);
    for (auto& c : combos)
        c->setVisible (! graphView);
    listenButton.setVisible (! graphView);
    syncButton.setVisible (! graphView);
    for (auto& t : tabs)
        t.setVisible (! graphView);
    if (graphView)
        for (auto& page : pages)
            page.setVisible (false);
    else
        showPage (currentPage);
    graphEditor->setVisible (graphView);
    if (graphView)
        graphEditor->refresh();
    content.repaint();
}

void PluginEditor::showPage (Page page)
{
    currentPage = page;
    for (size_t i = 0; i < pages.size(); ++i)
        pages[i].setVisible (static_cast<int> (i) == static_cast<int> (page));
    content.repaint (kPagePanel);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
juce::String PluginEditor::buildFooterText() const
{
    auto& d = processor.getDiagnostics();
    const double sr = d.sampleRate.load (std::memory_order_relaxed);

    juce::StringArray parts;
    parts.add (u8 ("v") + NOVAPITCH_VERSION_STRING);
    parts.add (processor.getFormatName());
    parts.add (hostDescription);
    parts.add (binaryArchitecture());
    parts.add (sr > 0.0 ? juce::String (sr / 1000.0, 1) + " kHz" : u8 ("— kHz"));
    parts.add (u8 ("bloques ") + juce::String (d.minBlockSize.load (std::memory_order_relaxed)) + u8 ("–")
               + juce::String (d.maxBlockSize.load (std::memory_order_relaxed)));
    parts.add (channelName (d.numInputChannels.load (std::memory_order_relaxed)) + u8 (" → ")
               + channelName (d.numOutputChannels.load (std::memory_order_relaxed)));
    parts.add (u8 ("latencia ") + juce::String (processor.getLatencySamples()));
    parts.add ("MIDI " + juce::String (d.midiEventsReceived.load (std::memory_order_relaxed)));
    return parts.joinIntoString (u8 ("  ·  "));
}

void PluginEditor::timerCallback()
{
    updateView(); // follows host automation of "Modo gráfico"
    updateKeyControls();
    if (graphView)
        graphEditor->refresh();

    // Drain every pending pitch frame (lock-free, single consumer).
    auto& fifo = processor.getPitchFrameFifo();
    bool gotFrames = false;
    for (;;)
    {
        const int n = fifo.pop (frameScratch.data(), static_cast<int> (frameScratch.size()));
        for (int i = 0; i < n; ++i)
        {
            const auto& f = frameScratch[static_cast<size_t> (i)];
            trace.pushFrame (f.timeSeconds,
                             f.voiced ? dsp::hzToMidi (f.frequencyHz, f.referenceA4) : -1.0f,
                             f.voiced ? dsp::hzToMidi (f.outputHz, f.referenceA4) : -1.0f);
            vibratoDisplay.pushFrame (f.voiced, f.vibratoCents, f.vibratoRateHz, f.vibratoDepthCents);
            lastFrame = f;
        }
        gotFrames = gotFrames || n > 0;
        if (n < static_cast<int> (frameScratch.size()))
            break;
    }

    meter.setTarget (lastFrame.voiced ? lastFrame.targetMidi : -1.0f, lastFrame.correctionCents, lastFrame.correcting);
    meter.setPitch (lastFrame.voiced ? lastFrame.frequencyHz : 0.0f, lastFrame.confidence, lastFrame.referenceA4);
    if (! gotFrames)
        lastFrame.voiced = false; // host stopped calling processBlock: let the meter fade
    else
        trace.repaint();
    if (currentPage == Page::Vibrato)
        vibratoDisplay.repaint();
    if (currentPage == Page::Notes)
    {
        auto pcOf = [] (float midi) { return midi < 0.0f ? -1 : ((juce::roundToInt (midi) % 12) + 12) % 12; };
        noteKeyboard->setLiveNotes (lastFrame.voiced ? pcOf (dsp::hzToMidi (lastFrame.frequencyHz, lastFrame.referenceA4)) : -1,
                                    lastFrame.voiced ? pcOf (lastFrame.targetMidi) : -1);
    }

    // Held MIDI notes (MIDI page)
    if (currentPage == Page::Midi)
    {
        juce::StringArray names;
        for (int n = 0; n < 128; ++n)
            if (processor.getMidiNotes().isHeldSnapshot (n))
                names.add (juce::String (dsp::midiNoteName (n)));
        auto text = names.joinIntoString ("  ");
        if (text != heldNotesText)
        {
            heldNotesText = text;
            content.repaint (kPagePanel);
        }
    }

    // Presets / MIDI learn state changed (host automation, preset buttons, CC learnt...)
    if (processor.getPresetManager().getCurrentPresetName() != shownPresetName)
        refreshPresetBox();
    if (processor.getMidiMappingGeneration() != shownMidiGeneration
        || processor.getMidiLearn().getLearningParam() != shownLearning)
        refreshMidiLabels();

    if (statusText.isNotEmpty() && juce::Time::getMillisecondCounter() >= statusUntil)
    {
        statusText = {};
        content.repaint (kFooterBounds);
    }
    auto newFooter = buildFooterText();
    if (newFooter != footerText)
    {
        footerText = std::move (newFooter);
        content.repaint (kFooterBounds);
    }
}

//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void PluginEditor::resized()
{
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (kBaseWidth);
    content.setBounds (0, 0, kBaseWidth, kBaseHeight);
    content.setTransform (juce::AffineTransform::scale (scale));
}

void PluginEditor::Content::resized()
{
    owner.meter.setBounds (kMeterBounds);
    owner.trace.setBounds (kTraceBounds);

    auto side = kSidePanel.reduced (14, 10);
    for (auto& c : owner.combos)
    {
        c->setBounds (side.removeFromTop (44));
        side.removeFromTop (8);
    }

    // Preset bar
    auto presets = kPresetBar;
    owner.prevPreset.setBounds (presets.removeFromLeft (26));
    presets.removeFromLeft (4);
    owner.optionsButton.setBounds (presets.removeFromRight (34));
    presets.removeFromRight (4);
    owner.savePreset.setBounds (presets.removeFromRight (70));
    presets.removeFromRight (4);
    owner.nextPreset.setBounds (presets.removeFromRight (26));
    presets.removeFromRight (4);
    owner.presetBox.setBounds (presets);

    auto modeSwitch = kModeSwitch;
    owner.autoButton.setBounds (modeSwitch.removeFromLeft (modeSwitch.getWidth() / 2));
    owner.graphButton.setBounds (modeSwitch);
    owner.graphEditor->setBounds (kGraphBounds);
    if (owner.combos.size() >= 2) // small buttons on the right of the Tonalidad / Escala labels
    {
        owner.listenButton.setBounds (owner.combos[0]->getBounds().withHeight (17).removeFromRight (96).translated (0, -2));
        owner.syncButton.setBounds (owner.combos[1]->getBounds().withHeight (17).removeFromRight (96).translated (0, -2));
    }

    // Tabs
    auto bar = kTabBar;
    for (auto& t : owner.tabs)
        t.setBounds (bar.removeFromLeft (kTabBar.getWidth() / kNumPages).reduced (2, 0));

    for (auto& page : owner.pages)
        page.setBounds (kPagePanel);
    const auto local = kPagePanel.withZeroOrigin();

    // Page 2: note states keyboard
    owner.noteKeyboard->setBounds (local);

    // Page 1: correction knobs
    {
        auto row = local.reduced (8, 10);
        const int w = row.getWidth() / static_cast<int> (owner.knobs.size());
        for (auto& k : owner.knobs)
            k->setBounds (row.removeFromLeft (w).reduced (4, 0));
    }

    // Page 2: [caption][toggles][knob][knob]
    {
        auto row = local.reduced (16, 10);
        row.removeFromLeft (150); // caption, painted by Content
        auto toggles = row.removeFromLeft (240).withSizeKeepingCentre (240, 64);
        for (auto& t : owner.formantToggles)
            t->setBounds (toggles.removeFromTop (32));
        row.removeFromLeft (20);
        for (auto& k : owner.formantKnobs)
            k->setBounds (row.removeFromLeft (100).reduced (4, 0));
    }

    // Page 4: [voice buttons] [mode + mute/solo] [interval, formant, pan, level, humanize]
    {
        auto row = local.reduced (10, 8);
        auto buttons = row.removeFromLeft (110);
        for (auto& b : owner.voiceButtons)
            b.setBounds (buttons.removeFromTop (31).reduced (0, 2));
        row.removeFromLeft (12);
        for (auto& vp : owner.voicePanels)
        {
            vp.panel.setBounds (row.translated (0, 0));
            auto r = vp.panel.getLocalBounds();
            auto left = r.removeFromLeft (150);
            vp.mode->setBounds (left.removeFromTop (44));
            left.removeFromTop (8);
            vp.mute->setBounds (left.removeFromTop (28));
            vp.solo->setBounds (left.removeFromTop (28));
            r.removeFromLeft (10);
            const int w = r.getWidth() / 5;
            auto interval = r.removeFromLeft (w);
            vp.degrees->setBounds (interval);
            vp.semitones->setBounds (interval);
            for (auto* k : { vp.formant.get(), vp.pan.get(), vp.level.get(), vp.humanize.get() })
                k->setBounds (r.removeFromLeft (w));
        }
    }

    // Page 5: harmony bus knobs
    {
        auto row = local.reduced (8, 10);
        const int w = row.getWidth() / static_cast<int> (owner.busKnobs.size());
        for (auto& k : owner.busKnobs)
            k->setBounds (row.removeFromLeft (w).reduced (2, 0));
    }

    // Page 6: MIDI toggles (held notes and routing help are painted)
    {
        auto col = local.reduced (16, 14).removeFromLeft (260);
        col.removeFromTop (20);
        for (auto& t : owner.midiToggles)
            t->setBounds (col.removeFromTop (23));
    }

    // Page 3: [display][keep] | [synth knobs x6]
    {
        auto row = local.reduced (10, 10);
        owner.vibratoDisplay.setBounds (row.removeFromLeft (190));
        row.removeFromLeft (8);
        owner.vibratoKnobs[0]->setBounds (row.removeFromLeft (84));
        row.removeFromLeft (14); // divider
        owner.vibShapeBox.setBounds (row.withHeight (20).removeFromRight (118).translated (0, -6));
        row.removeFromTop (14);  // "VIBRATO ARTIFICIAL" caption above the group
        const int w = row.getWidth() / static_cast<int> (owner.vibratoKnobs.size() - 1);
        for (size_t i = 1; i < owner.vibratoKnobs.size(); ++i)
            owner.vibratoKnobs[i]->setBounds (row.removeFromLeft (w).reduced (1, 0));
    }
}

void PluginEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
    const auto accent = owner.lookAndFeel.getAccent();

    // Header ---------------------------------------------------------------
    auto header = juce::Rectangle<float> (20.0f, 14.0f, static_cast<float> (kBaseWidth) - 40.0f, 50.0f);
    g.setColour (accent);
    g.fillEllipse (header.getX(), header.getCentreY() - 9.0f, 18.0f, 18.0f);
    g.setColour (Palette::textPrimary);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("NovaPitch", header.withTrimmedLeft (30.0f), juce::Justification::centredLeft);

    // Host badge (FL Studio first)
    const auto badgeColour = owner.hostIsFlStudio ? Palette::ok : Palette::textMuted;
    const auto badgeText = owner.hostIsFlStudio ? u8 ("FL Studio") : owner.hostDescription.substring (0, 14);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    const float badgeWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), badgeText) + 24.0f;
    const auto badge = juce::Rectangle<float> (badgeWidth, 24.0f).withPosition (header.getRight() - badgeWidth, header.getCentreY() - 12.0f);
    g.setColour (badgeColour.withAlpha (0.15f));
    g.fillRoundedRectangle (badge, 12.0f);
    g.setColour (badgeColour);
    g.drawRoundedRectangle (badge, 12.0f, 1.0f);
    g.drawText (badgeText, badge, juce::Justification::centred);


    if (owner.graphView)
    {
        owner.paintFooter (g);
        return; // the graphical editor paints itself
    }

    // Panel backgrounds -----------------------------------------------------------
    for (const auto& r : { kSidePanel.toFloat(), kPagePanel.toFloat() })
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (Palette::panelOutline);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    }

    // Page captions (pages are transparent containers)
    if (owner.currentPage == Page::Formants)
    {
        auto caption = kPagePanel.toFloat().reduced (16.0f, 16.0f).withWidth (150.0f);
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (u8 ("FORMANTES"), caption.removeFromTop (18.0f), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (u8 ("Timbre de la voz, independiente de la afinación"), caption.removeFromTop (44.0f).toNearestInt(),
                          juce::Justification::topLeft, 3, 1.0f);
    }
    else if (owner.currentPage == Page::Midi)
    {
        auto area = kPagePanel.toFloat().reduced (16.0f, 14.0f);
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (u8 ("MIDI"), area.removeFromTop (18.0f).withWidth (260.0f), juce::Justification::centredLeft);

        auto info = kPagePanel.toFloat().reduced (16.0f, 14.0f).withTrimmedLeft (290.0f);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (u8 ("NOTAS MIDI RECIBIDAS"), info.removeFromTop (16.0f), juce::Justification::centredLeft);
        g.setColour (Palette::textPrimary);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (owner.heldNotesText.isEmpty() ? u8 ("—") : owner.heldNotesText, info.removeFromTop (26.0f),
                    juce::Justification::centredLeft);
        g.setColour (Palette::textMuted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (u8 ("FL Studio: en el plugin, ⚙ → MIDI → Input port = 1. Luego crea un canal "
                              "«MIDI Out» con el mismo puerto y escribe notas o acordes en su Piano roll "
                              "(acordes = armonías en modo MIDI)."),
                          info.toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
    }
    else if (owner.currentPage == Page::Vibrato && owner.vibratoKnobs.size() > 1)
    {
        // Divider between "keep natural vibrato" and the artificial vibrato group.
        const auto first = owner.vibratoKnobs[1]->getBounds().translated (kPagePanel.getX(), kPagePanel.getY());
        const float x = static_cast<float> (first.getX()) - 8.0f;
        g.setColour (Palette::panelOutline);
        g.drawVerticalLine (juce::roundToInt (x), static_cast<float> (kPagePanel.getY() + 14), static_cast<float> (kPagePanel.getBottom() - 14));
        g.setColour (Palette::textMuted.withAlpha (0.8f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (u8 ("VIBRATO ARTIFICIAL"), juce::Rectangle<float> (x + 10.0f, static_cast<float> (kPagePanel.getY() + 8), 200.0f, 12.0f),
                    juce::Justification::centredLeft);
    }

    owner.paintFooter (g);
}

void PluginEditor::paintFooter (juce::Graphics& g) const
{
    // Footer: transient status (key detected, sync) or host diagnostics
    g.setFont (juce::FontOptions (11.0f));
    if (statusText.isNotEmpty() && juce::Time::getMillisecondCounter() < statusUntil)
    {
        g.setColour (lookAndFeel.getAccent().brighter (0.2f));
        g.drawText (statusText, kFooterBounds.toFloat(), juce::Justification::centredLeft);
        return;
    }
    g.setColour (Palette::textMuted.withAlpha (0.8f));
    g.drawText (footerText, kFooterBounds.toFloat(), juce::Justification::centredLeft);
}

} // namespace novapitch
