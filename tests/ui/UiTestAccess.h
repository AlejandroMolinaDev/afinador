#pragma once

// Test-only access to the editor's private controls and menu handlers
// (PluginEditor, GraphEditor and NoteKeyboard declare `friend struct UiTestAccess`).
// Menus: create*Menu() builds exactly the menu the user sees and
// *MenuItemChosen() runs exactly what clicking an item runs.

#include "ui/PluginEditor.h"
#include "ui/components/GraphEditor.h"
#include "ui/components/NoteKeyboard.h"

namespace novapitch
{

struct UiTestAccess
{
    // PluginEditor ---------------------------------------------------------------
    static PluginEditor& editor (juce::AudioProcessorEditor& e) { return dynamic_cast<PluginEditor&> (e); }
    static juce::Component& content (PluginEditor& e) { return e.content; }
    static GraphEditor& graph (PluginEditor& e) { return *e.graphEditor; }
    static NoteKeyboard& keyboard (PluginEditor& e) { return *e.noteKeyboard; }
    static int currentPage (const PluginEditor& e) { return static_cast<int> (e.currentPage); }
    static int numPages() { return PluginEditor::kNumPages; }
    static bool graphView (const PluginEditor& e) { return e.graphView; }
    static juce::Component& page (PluginEditor& e, int index) { return e.pages[static_cast<size_t> (index)]; }
    static juce::Component& voicePanel (PluginEditor& e, int v) { return e.voicePanels[static_cast<size_t> (v)].panel; }
    static juce::ComboBox& presetBox (PluginEditor& e) { return e.presetBox; }
    static juce::AlertWindow* saveDialog (PluginEditor& e) { return e.saveDialog.get(); }
    static juce::String statusText (const PluginEditor& e) { return e.statusText; }
    static juce::String footerText (const PluginEditor& e) { return e.footerText; }
    static juce::String heldNotesText (const PluginEditor& e) { return e.heldNotesText; }
    static juce::Colour accent (const PluginEditor& e) { return e.lookAndFeel.getAccent(); }
    static const PitchFrame& lastFrame (const PluginEditor& e) { return e.lastFrame; }
    static std::vector<LabeledKnob*> knobs (const PluginEditor& e) { return e.allKnobs(); }
    static int menusOpened (const PluginEditor& e) { return e.lookAndFeel.getNumMenusOpened(); }

    static juce::PopupMenu optionsMenu (PluginEditor& e) { return e.createOptionsMenu(); }
    static void chooseOption (PluginEditor& e, int result) { e.optionsMenuItemChosen (result); }
    static juce::PopupMenu knobMenu (PluginEditor& e, const LabeledKnob& k) { return e.createKnobMenu (k); }
    static void chooseKnobItem (PluginEditor& e, const LabeledKnob& k, int result) { e.knobMenuItemChosen (k.getParameterIndex(), result); }
    static juce::PopupMenu listenMenu (PluginEditor& e) { return e.createListenMenu(); }
    static void chooseListen (PluginEditor& e, int result) { e.listenMenuItemChosen (result); }

    // NoteKeyboard -------------------------------------------------------------------
    static juce::PopupMenu keyMenu (NoteKeyboard& k, int pc) { return k.createKeyMenu (pc); }
    static void chooseKeyItem (NoteKeyboard& k, int pc, int result) { k.keyMenuItemChosen (pc, result); }
    static juce::AlertWindow* detuneDialog (NoteKeyboard& k) { return k.detuneDialog.get(); }
    static int detected (const NoteKeyboard& k) { return k.detected; }
    static int target (const NoteKeyboard& k) { return k.target; }

    // GraphEditor --------------------------------------------------------------------
    static juce::Component& roll (GraphEditor& g) { return g.getRollComponent(); }
    static juce::Point<float> rollPoint (const GraphEditor& g, double sourceTime, float pitch) { return g.rollPoint (sourceTime, pitch); }
    static void setView (GraphEditor& g, double start, double length, float top, float range) { g.setRollView (start, length, top, range); }
    static double viewStart (const GraphEditor& g) { return g.getRollViewStart(); }
    static double viewLength (const GraphEditor& g) { return g.getRollViewLength(); }
    static std::vector<juce::int64> selection (const GraphEditor& g) { return g.getSelectedIds(); }
    static bool following (const GraphEditor& g) { return g.isFollowing(); }
    static juce::String infoText (const GraphEditor& g) { return g.infoText; }
    static juce::PopupMenu importMenu (GraphEditor& g) { return g.createImportMenu(); }
    static void chooseImport (GraphEditor& g, int result) { g.importMenuItemChosen (result); }
    static juce::PopupMenu clearMenu (GraphEditor& g) { return g.createClearMenu(); }
    static void chooseClear (GraphEditor& g, int result) { g.clearMenuItemChosen (result); }
    static juce::Slider& retuneSlider (GraphEditor& g) { return g.retuneSlider; }
    static juce::Slider& vibratoSlider (GraphEditor& g) { return g.vibratoSlider; }
    static juce::Slider& formantSlider (GraphEditor& g) { return g.formantSlider; }
};

} // namespace novapitch
