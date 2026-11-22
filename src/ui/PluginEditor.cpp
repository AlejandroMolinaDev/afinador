#include "ui/PluginEditor.h"
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

juce::String channelLayoutName (int numIn, int numOut)
{
    auto name = [] (int n) -> juce::String
    {
        switch (n)
        {
            case 0:  return u8 ("—");
            case 1:  return "Mono";
            case 2:  return u8 ("Estéreo");
            default: return juce::String (n) + " ch";
        }
    };
    return name (numIn) + u8 (" → ") + name (numOut);
}
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

    // Resizable 75 %–200 % with fixed aspect ratio; drawn in base coordinates
    // and scaled, so the UI is crisp on HiDPI / Retina.
    setResizable (true, true);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);
    if (auto* sizeConstrainer = getConstrainer())
        sizeConstrainer->setFixedAspectRatio (static_cast<double> (kBaseWidth) / kBaseHeight);
    setSize (kBaseWidth, kBaseHeight);

    lines = buildDiagnosticLines();
    startTimerHz (15);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
juce::StringArray PluginEditor::buildDiagnosticLines() const
{
    auto& d = processor.getDiagnostics();

    const double sr = d.sampleRate.load (std::memory_order_relaxed);
    const int minB = d.minBlockSize.load (std::memory_order_relaxed);
    const int maxB = d.maxBlockSize.load (std::memory_order_relaxed);

    juce::StringArray out;
    out.add (u8 ("Formato|") + processor.getFormatName());
    out.add (u8 ("Host|") + hostDescription);
    out.add (u8 ("Sistema|") + juce::SystemStats::getOperatingSystemName() + "  (" + binaryArchitecture() + ")");
    out.add (u8 ("Frecuencia de muestreo|") + (sr > 0.0 ? juce::String (sr, 0) + " Hz" : u8 ("—")));
    out.add (u8 ("Bloque (máx. anunciado)|") + juce::String (d.maxBlockFromPrepare.load (std::memory_order_relaxed)) + " muestras");
    out.add (u8 ("Bloques recibidos (mín / máx / último)|")
             + juce::String (minB) + " / " + juce::String (maxB) + " / "
             + juce::String (d.lastBlockSize.load (std::memory_order_relaxed)));
    out.add (u8 ("Canales|") + channelLayoutName (d.numInputChannels.load (std::memory_order_relaxed),
                                                  d.numOutputChannels.load (std::memory_order_relaxed)));
    out.add (u8 ("Latencia reportada|") + juce::String (processor.getLatencySamples()) + " muestras");
    out.add (u8 ("Eventos MIDI recibidos|") + juce::String (d.midiEventsReceived.load (std::memory_order_relaxed)));
    return out;
}

void PluginEditor::timerCallback()
{
    auto newLines = buildDiagnosticLines();
    if (newLines != lines)
    {
        lines = std::move (newLines);
        repaint();
    }
}

//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (kBaseWidth);
    g.fillAll (Palette::background);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto accent = lookAndFeel.getAccent();
    auto area = juce::Rectangle<float> (0.0f, 0.0f, kBaseWidth, kBaseHeight).reduced (24.0f);

    // Header -------------------------------------------------------------------
    auto header = area.removeFromTop (56.0f);
    g.setColour (accent);
    g.fillEllipse (header.getX(), header.getCentreY() - 9.0f, 18.0f, 18.0f);
    g.setColour (Palette::textPrimary);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("NovaPitch", header.withTrimmedLeft (30.0f), juce::Justification::centredLeft);

    g.setColour (Palette::textMuted);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (u8 ("v") + NOVAPITCH_VERSION_STRING + u8 ("  ·  Fase 1: passthrough"),
                header, juce::Justification::centredRight);

    // FL Studio badge -----------------------------------------------------------
    area.removeFromTop (8.0f);
    auto badge = area.removeFromTop (26.0f);
    const auto badgeColour = hostIsFlStudio ? Palette::ok : Palette::textMuted;
    const auto badgeText = hostIsFlStudio ? u8 ("FL Studio detectado") : u8 ("Host: ") + hostDescription;
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const float badgeWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), badgeText) + 24.0f;
    auto badgeRect = badge.withWidth (badgeWidth);
    g.setColour (badgeColour.withAlpha (0.15f));
    g.fillRoundedRectangle (badgeRect, 13.0f);
    g.setColour (badgeColour);
    g.drawRoundedRectangle (badgeRect, 13.0f, 1.0f);
    g.drawText (badgeText, badgeRect, juce::Justification::centred);

    // Diagnostics panel -------------------------------------------------------
    area.removeFromTop (14.0f);
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (Palette::panelOutline);
    g.drawRoundedRectangle (area, 10.0f, 1.0f);

    auto rows = area.reduced (18.0f, 12.0f);
    const float rowHeight = rows.getHeight() / static_cast<float> (juce::jmax (1, lines.size()));

    for (const auto& line : lines)
    {
        auto row = rows.removeFromTop (rowHeight);
        const auto label = line.upToFirstOccurrenceOf ("|", false, false);
        const auto value = line.fromFirstOccurrenceOf ("|", false, false);

        g.setFont (juce::FontOptions (14.0f));
        g.setColour (Palette::textMuted);
        g.drawText (label, row, juce::Justification::centredLeft);
        g.setColour (Palette::textPrimary);
        g.drawText (value, row, juce::Justification::centredRight);
    }
}

void PluginEditor::resized() {}

} // namespace novapitch
