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
const juce::Rectangle<int> kTraceBounds   { 20, 312, 720, 112 };
const juce::Rectangle<int> kFooterBounds  { 20, 432, 720, 18 };
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

    // Input profile ------------------------------------------------------------
    profileLabel.setText (u8 ("Perfil de entrada"), juce::dontSendNotification);
    profileLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    profileLabel.setColour (juce::Label::textColourId, Palette::textMuted);
    content.addAndMakeVisible (profileLabel);

    for (int i = 0; i < dsp::kNumInputProfiles; ++i)
        profileBox.addItem (u8 (dsp::kInputProfileNames[static_cast<size_t> (i)]), i + 1);
    profileBox.setTooltip (u8 ("Rango de frecuencias en el que se busca la nota. Elige el más "
                               "cercano a la voz o instrumento: un rango ajustado reduce errores de octava."));
    content.addAndMakeVisible (profileBox);
    profileAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.getValueTreeState(), ParamIDs::inputProfile, profileBox);
    profileBox.onChange = [this] { updateProfileRangeLabel(); };

    profileRangeLabel.setFont (juce::FontOptions (12.0f));
    profileRangeLabel.setColour (juce::Label::textColourId, Palette::textMuted);
    content.addAndMakeVisible (profileRangeLabel);
    updateProfileRangeLabel();

    // Sensitivity --------------------------------------------------------------
    sensitivityLabel.setText (u8 ("Sensibilidad"), juce::dontSendNotification);
    sensitivityLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    sensitivityLabel.setColour (juce::Label::textColourId, Palette::textMuted);
    content.addAndMakeVisible (sensitivityLabel);

    sensitivitySlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    sensitivitySlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    sensitivitySlider.setColour (juce::Slider::textBoxTextColourId, Palette::textPrimary);
    sensitivitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    sensitivitySlider.setTooltip (u8 ("Cuánto ruido o aire tolera la detección (0–100 %). Súbela para voces "
                                      "susurradas o grabaciones ruidosas; bájala si la detección salta en "
                                      "respiraciones o sibilantes."));
    content.addAndMakeVisible (sensitivitySlider);
    sensitivityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.getValueTreeState(), ParamIDs::sensitivity, sensitivitySlider);
    sensitivitySlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
    sensitivitySlider.updateText();

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
void PluginEditor::updateProfileRangeLabel()
{
    const auto profile = dsp::inputProfileFromIndex (profileBox.getSelectedId() - 1);
    const auto r = dsp::getF0Range (profile);
    profileRangeLabel.setText (u8 ("Rango de búsqueda: ") + juce::String (juce::roundToInt (r.minHz)) + u8 ("–")
                                   + juce::String (juce::roundToInt (r.maxHz)) + " Hz",
                               juce::dontSendNotification);
}

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
            trace.pushFrame (f.timeSeconds, f.voiced ? dsp::hzToMidi (f.frequencyHz) : -1.0f);
            lastFrame = f;
        }
        gotFrames = gotFrames || n > 0;
        if (n < static_cast<int> (frameScratch.size()))
            break;
    }

    // Phase 2: reference A4 = 440 Hz (the "detune" reference arrives in phase 3).
    meter.setPitch (lastFrame.voiced ? lastFrame.frequencyHz : 0.0f, lastFrame.confidence, dsp::kDefaultA4Hz);
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

    auto side = kSidePanel.reduced (16, 12);
    owner.profileLabel.setBounds (side.removeFromTop (18));
    side.removeFromTop (4);
    owner.profileBox.setBounds (side.removeFromTop (30));
    owner.profileRangeLabel.setBounds (side.removeFromTop (22));
    side.removeFromTop (12);
    owner.sensitivityLabel.setBounds (side.removeFromTop (18));
    owner.sensitivitySlider.setBounds (side.removeFromTop (96));
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
    g.drawText (u8 ("v") + NOVAPITCH_VERSION_STRING + u8 ("  ·  Fase 2: detección"),
                header.withRight (badge.getX() - 14.0f), juce::Justification::centredRight);

    // Side panel background ---------------------------------------------------
    const auto side = kSidePanel.toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (side, 10.0f);
    g.setColour (Palette::panelOutline);
    g.drawRoundedRectangle (side.reduced (0.5f), 10.0f, 1.0f);

    // Footer: host diagnostics ------------------------------------------------
    g.setColour (Palette::textMuted.withAlpha (0.8f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (owner.footerText, kFooterBounds.toFloat(), juce::Justification::centredLeft);
}

} // namespace novapitch
