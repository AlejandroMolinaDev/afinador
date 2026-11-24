#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/PitchFrameFifo.h"
#include "ui/LookAndFeel.h"
#include "ui/components/GraphEditor.h"
#include "ui/components/LabeledControls.h"
#include "ui/components/NoteKeyboard.h"
#include "ui/components/PitchTrace.h"
#include "ui/components/TuningMeter.h"
#include "ui/components/VibratoDisplay.h"

#include <array>
#include <memory>
#include <vector>

namespace novapitch
{

class NovaPitchProcessor;
struct UiTestAccess; // tests/ui: reaches the editor's controls and menus

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

    enum class Page { Correction = 0, Notes = 1, Formants = 2, Vibrato = 3, Harmony = 4, HarmonyBus = 5, Midi = 6 };
    static constexpr int kNumPages = 7;

    void timerCallback() override;
    juce::String buildFooterText() const;
    void showPage (Page);
    /** Shows the automatic layout or the graphical editor, following the graphMode parameter. */
    void updateView();
    void setGraphMode (bool);

    // Pop-up menus: each one is built by create*Menu() and its result handled by
    // *MenuItemChosen(), so the UI tests run exactly what a menu click runs.

    // Key detection / instance sync (phase 9)
    void showListenMenu();
    juce::PopupMenu createListenMenu() const;
    void listenMenuItemChosen (int result);
    void showStatus (const juce::String&);
    void updateKeyControls();
    void paintFooter (juce::Graphics&) const;

    // Presets / settings
    void refreshPresetBox();
    void showSaveDialog();
    void showOptionsMenu();
    juce::PopupMenu createOptionsMenu();
    void optionsMenuItemChosen (int result);
    void applyAccent (juce::Colour);

    // MIDI Learn
    void showKnobMenu (LabeledKnob&);
    juce::PopupMenu createKnobMenu (const LabeledKnob&) const;
    void knobMenuItemChosen (int parameterIndex, int result);
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
    std::unique_ptr<NoteKeyboard> noteKeyboard; // 12-note states (spec 5.2)

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
    std::vector<std::unique_ptr<LabeledToggle>> midiToggles; // target, learn scale, exact octave, MIDI out
    juce::ComboBox vibShapeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> vibShapeAttachment;
    juce::String heldNotesText;
    Page currentPage = Page::Correction;

    // Preset bar (header)
    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, savePreset, optionsButton;
    std::array<juce::TextButton, 2> abButtons; // A/B comparison
    bool shownModified = false;
    std::unique_ptr<juce::AlertWindow> saveDialog;
    juce::String optionsMenuPreset; // preset the options menu was opened for
    juce::String shownPresetName;
    int shownMidiGeneration = -1;
    int shownLearning = -2;

    // Auto / Graphical mode (phase 8)
    juce::TextButton autoButton, graphButton;
    std::unique_ptr<GraphEditor> graphEditor;
    bool graphView = false;

    juce::TextButton listenButton, syncButton;
    juce::String statusText;
    juce::uint32 statusUntil = 0;
    int shownKeyGeneration = 0;

    juce::String hostDescription;
    bool hostIsFlStudio = false;
    juce::String footerText;

    std::array<PitchFrame, 512> frameScratch {};
    PitchFrame lastFrame;

    friend struct UiTestAccess;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace novapitch
