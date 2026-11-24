#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <set>
#include <vector>

namespace novapitch
{

class NovaPitchProcessor;
class GraphModel;
struct UiTestAccess;

/** Graphical mode editor (phase 8, no ARA): piano roll with the captured pitch
    curve and the correction objects (notes, lines, freehand curves), a toolbar
    and a per-note inspector. Fixed base size 720 x 538 (scaled by the editor).

    Workflow in FL Studio: "Capturar" + play the song -> the pitch curve appears
    -> "Importar desde automático" (notes in the current key/scale) -> edit ->
    play: the output follows the objects; outside them the original voice passes. */
class GraphEditor final : public juce::Component
{
public:
    enum class Tool { Select = 0, Note, Line, Curve, Erase, Split, Time };
    static constexpr int kNumTools = 7;

    explicit GraphEditor (NovaPitchProcessor&);
    ~GraphEditor() override;

    void setAccent (juce::Colour);
    /** Called by the editor's 30 Hz timer: playhead, capture progress, model changes. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void setTool (Tool);
    Tool getTool() const noexcept { return tool; }

    class Roll;

private:
    void updateButtons();
    void updateInspector();
    /** Predicted output pitch per captured frame (-1 = not corrected / unvoiced). */
    void updatePrediction();
    void applyInspector (const juce::Identifier& property, double value);
    void mergeSelection();
    void showClearMenu();
    // Menus built and handled separately, so the UI tests run the code a click runs.
    juce::PopupMenu createImportMenu() const;
    void importMenuItemChosen (int result);
    juce::PopupMenu createClearMenu() const;
    void clearMenuItemChosen (int result);
    void deleteSelection();

    // Piano-roll introspection for the UI tests (Roll is private to the .cpp).
    juce::Component& getRollComponent() noexcept;
    /** Point in roll coordinates of a source time and pitch (MIDI). */
    juce::Point<float> rollPoint (double sourceTime, float pitch) const;
    void setRollView (double start, double length, float top, float range);
    double getRollViewStart() const;
    double getRollViewLength() const;
    std::vector<juce::int64> getSelectedIds() const;
    bool isFollowing() const;
    juce::String buildInfoText() const;

    NovaPitchProcessor& processor;
    GraphModel& model;
    juce::Colour accent;
    Tool tool = Tool::Select;

    std::unique_ptr<Roll> roll;
    std::array<juce::TextButton, kNumTools> toolButtons;
    juce::TextButton mergeButton, undoButton, redoButton;
    juce::TextButton captureButton, importButton, clearButton, followButton, zoomOut, zoomIn, fitButton;
    juce::Slider retuneSlider, vibratoSlider, formantSlider;
    bool updatingInspector = false;

    int shownGeneration = -1;
    std::vector<float> predicted;
    float predictedRetune = -1.0f, predictedKeep = -1.0f;
    juce::String infoText;

    friend class Roll;
    friend struct UiTestAccess;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphEditor)
};

} // namespace novapitch
