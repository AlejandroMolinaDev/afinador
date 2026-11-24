#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace novapitch
{

struct UiTestAccess;

/** One-octave keyboard to set the state of each of the 12 notes (parameters
    noteState0..11): Escala (follows key + scale), Activa, Ignorar (the original
    pitch passes) or Eliminar (redirected to the nearest valid note).
    Keys are coloured by the *effective* state after key, scale and overrides;
    the detected and target notes are highlighted live.
    Click = next state, right click = choose state / detune, mouse wheel over a
    key = detune ±1 cent (Shift: ±10) for microtonal scales. */
class NoteKeyboard final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    explicit NoteKeyboard (juce::AudioProcessorValueTreeState&);
    ~NoteKeyboard() override;

    void setAccent (juce::Colour c) { accent = c; repaint(); }
    /** Pitch classes (0..11) of the detected and target notes, -1 = none. */
    void setLiveNotes (int detectedPc, int targetPc);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Key rectangle of a pitch class (local coordinates; for tests and hit testing). */
    juce::Rectangle<float> keyBounds (int pitchClass) const;
    int pitchClassAt (juce::Point<float>) const;

    void setState (int pitchClass, int state);
    int getState (int pitchClass) const;
    /** Effective state (1 Activa, 2 Ignorar, 3 Eliminar) after key/scale. */
    int getEffectiveState (int pitchClass) const;

    /** Per-note detune in cents (parameters noteDetune0..11). */
    void setDetune (int pitchClass, float cents);
    float getDetune (int pitchClass) const;

private:
    juce::AudioProcessorValueTreeState& state;
    void askExactDetune (int pitchClass);
    /** Right-click menu of a key and its handler (the UI tests run the same code). */
    juce::PopupMenu createKeyMenu (int pitchClass) const;
    void keyMenuItemChosen (int pitchClass, int result);

    std::array<std::unique_ptr<juce::ParameterAttachment>, 12> noteAttachments, detuneAttachments;
    std::array<float, 12> detunes {};
    std::unique_ptr<juce::AlertWindow> detuneDialog;
    std::unique_ptr<juce::ParameterAttachment> keyAttachment, scaleAttachment;
    std::array<int, 12> states {};
    int key = 0, scale = 0;
    int detected = -1, target = -1;
    juce::Colour accent { 0xffff6b4a };
    juce::Rectangle<float> keysArea;
    juce::TextButton resetButton, allActiveButton;

    friend struct UiTestAccess;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteKeyboard)
};

} // namespace novapitch
