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
const juce::Rectangle<int> kKnobPanel     { 20, 312, 720, 138 };
const juce::Rectangle<int> kTraceBounds   { 20, 462, 720, 110 };
const juce::Rectangle<int> kFooterBounds  { 20, 580, 720, 16 };
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
        u8 ("Live: latencia mínima (~3 ms) para directo. Tracking: ~10 ms, más preciso para grabar y mezclar. "
            "HQ y Vintage: motores de la Fase 4 (por ahora usan Tracking y Live).")));
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
        content.addAndMakeVisible (*k);

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

    auto row = kKnobPanel.reduced (8, 10);
    const int w = row.getWidth() / static_cast<int> (owner.knobs.size());
    for (auto& k : owner.knobs)
        k->setBounds (row.removeFromLeft (w).reduced (4, 0));
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
    g.drawText (u8 ("v") + NOVAPITCH_VERSION_STRING + u8 ("  ·  Fase 3: corrección"),
                header.withRight (badge.getX() - 14.0f), juce::Justification::centredRight);

    // Panel backgrounds -----------------------------------------------------------
    for (const auto& r : { kSidePanel.toFloat(), kKnobPanel.toFloat() })
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (Palette::panelOutline);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    }

    // Footer: host diagnostics ------------------------------------------------
    g.setColour (Palette::textMuted.withAlpha (0.8f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (owner.footerText, kFooterBounds.toFloat(), juce::Justification::centredLeft);
}

} // namespace novapitch
