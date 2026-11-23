#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/PitchFrameFifo.h"
#include "ui/LookAndFeel.h"
#include "ui/components/GraphEditor.h"
#include "ui/components/LabeledControls.h"
#include "ui/components/PitchTrace.h"
#include "ui/components/TuningMeter.h"
#include "ui/components/VibratoDisplay.h"

#include <array>
#include <memory>
#include <vector>

namespace novapitch
{

class NovaPitchProcessor;

/** Main editor. All content lives in a fixed-size (kBaseWidth x kBaseHeight)
    component that is scaled with an AffineTransform, so the UI stays crisp on
    HiDPI / Retina and resizes 75 %–200 % with a fixed aspect ratio. */
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit PluginEditor (NovaPitchProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseWidth = 760;
    static constexpr int kBaseHeight = 640;

private:
    class Content final : public juce::Component
    {
    public:
        explicit Content (PluginEditor& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        PluginEditor& owner;
    };

    enum class Page { Correction = 0, Formants = 1, Vibrato = 2, Harmony = 3, HarmonyBus = 4, Midi = 5 };
    static constexpr int kNumPages = 6;

    void timerCallback() override;
    juce::String buildFooterText() const;
    void showPage (Page);
    /** Shows the automatic layout or the graphical editor, following the graphMode parameter. */
    void updateView();
    void setGraphMode (bool);

    // Presets / settings
    void refreshPresetBox();
    void showSaveDialog();
    void showOptionsMenu();
    void applyAccent (juce::Colour);

    // MIDI Learn
    void showKnobMenu (LabeledKnob&);
    void refreshMidiLabels();
    std::vector<LabeledKnob*> allKnobs() const;

    NovaPitchProcessor& processor;
    NovaLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    Content content { *this };
    TuningMeter meter;
    PitchTrace trace;

    std::vector<std::unique_ptr<LabeledCombo>> combos; // key, scale, mode, profile
    std::vector<std::unique_ptr<LabeledKnob>> knobs;   // speed, tolerance, hold, transpose, mix, sensitivity, A4
    std::vector<std::unique_ptr<LabeledToggle>> formantToggles; // preserve, link
    std::vector<std::unique_ptr<LabeledKnob>> formantKnobs;     // shift, character
    std::vector<std::unique_ptr<LabeledKnob>> vibratoKnobs;     // keep, rate, depth, delay, variation, amp, formant
    VibratoDisplay vibratoDisplay;

    // Tabs: each page is a transparent container placed over the page panel.
    std::array<juce::TextButton, kNumPages> tabs;
    std::array<juce::Component, kNumPages> pages;

    // Harmony page: voice selector + one panel per voice (only the selected is visible)
    struct VoicePanel
    {
        juce::Component panel;
        std::unique_ptr<LabeledCombo> mode;
        std::unique_ptr<LabeledKnob> degrees, semitones, formant, pan, level, humanize;
        std::unique_ptr<LabeledToggle> mute, solo;
    };
    std::array<VoicePanel, 4> voicePanels;
    std::array<juce::TextButton, 4> voiceButtons;
    int selectedVoice = 0;
    void selectVoice (int);
    void updateVoiceVisibility();
    std::vector<std::unique_ptr<LabeledKnob>> busKnobs;
    std::vector<std::unique_ptr<LabeledToggle>> midiToggles;
    juce::String heldNotesText;
    Page currentPage = Page::Correction;

    // Preset bar (header)
    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, savePreset, optionsButton;
    std::unique_ptr<juce::AlertWindow> saveDialog;
    juce::String shownPresetName;
    int shownMidiGeneration = -1;
    int shownLearning = -2;

    // Auto / Graphical mode (phase 8)
    juce::TextButton autoButton, graphButton;
    std::unique_ptr<GraphEditor> graphEditor;
    bool graphView = false;

    juce::String hostDescription;
    bool hostIsFlStudio = false;
    juce::String footerText;

    std::array<PitchFrame, 512> frameScratch {};
    PitchFrame lastFrame;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace novapitch
