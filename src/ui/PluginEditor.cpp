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

// Layout (base coordinates)
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
        pages[1].addAndMakeVisible (*t);
    for (auto& k : formantKnobs)
        pages[1].addAndMakeVisible (*k);

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
        pages[2].addAndMakeVisible (*k);
    pages[2].addAndMakeVisible (vibratoDisplay);
    vibratoDisplay.setAccent (lookAndFeel.getAccent());

    // Tabs --------------------------------------------------------------------------
    const char* tabNames[] = { "Corrección", "Formantes", "Vibrato" };
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

    meter.setAccent (lookAndFeel.getAccent());
    trace.setAccent (lookAndFeel.getAccent());

    setResizable (true, true);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);
    if (auto* sizeConstrainer = getConstrainer())
        sizeConstrainer->setFixedAspectRatio (static_cast<double> (kBaseWidth) / kBaseHeight);
    setSize (kBaseWidth, kBaseHeight);

    footerText = buildFooterText();
    startTimerHz (30);
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

    // Tabs
    auto bar = kTabBar;
    for (auto& t : owner.tabs)
        t.setBounds (bar.removeFromLeft (120).reduced (2, 0));

    for (auto& page : owner.pages)
        page.setBounds (kPagePanel);
    const auto local = kPagePanel.withZeroOrigin();

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

    // Page 3: [display][keep] | [synth knobs x6]
    {
        auto row = local.reduced (10, 10);
        owner.vibratoDisplay.setBounds (row.removeFromLeft (190));
        row.removeFromLeft (8);
        owner.vibratoKnobs[0]->setBounds (row.removeFromLeft (84));
        row.removeFromLeft (14); // divider
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
    const auto badgeText = owner.hostIsFlStudio ? u8 ("FL Studio detectado") : owner.hostDescription;
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    const float badgeWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), badgeText) + 24.0f;
    const auto badge = juce::Rectangle<float> (badgeWidth, 24.0f).withPosition (header.getRight() - badgeWidth, header.getCentreY() - 12.0f);
    g.setColour (badgeColour.withAlpha (0.15f));
    g.fillRoundedRectangle (badge, 12.0f);
    g.setColour (badgeColour);
    g.drawRoundedRectangle (badge, 12.0f, 1.0f);
    g.drawText (badgeText, badge, juce::Justification::centred);

    g.setColour (Palette::textMuted);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (u8 ("v") + NOVAPITCH_VERSION_STRING + u8 ("  ·  Fase 5: vibrato"),
                header.withRight (badge.getX() - 14.0f), juce::Justification::centredRight);

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

    // Footer: host diagnostics ------------------------------------------------
    g.setColour (Palette::textMuted.withAlpha (0.8f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (owner.footerText, kFooterBounds.toFloat(), juce::Justification::centredLeft);
}

} // namespace novapitch
