#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace novapitch
{

/** One-octave keyboard to set the state of each of the 12 notes (parameters
    noteState0..11): Escala (follows key + scale), Activa, Ignorar (the original
    pitch passes) or Eliminar (redirected to the nearest valid note).
    Keys are coloured by the *effective* state after key, scale and overrides;
    the detected and target notes are highlighted live.
    Click = next state, right click = choose. */
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

    /** Key rectangle of a pitch class (local coordinates; for tests and hit testing). */
    juce::Rectangle<float> keyBounds (int pitchClass) const;
    int pitchClassAt (juce::Point<float>) const;

    void setState (int pitchClass, int state);
    int getState (int pitchClass) const;
    /** Effective state (1 Activa, 2 Ignorar, 3 Eliminar) after key/scale. */
    int getEffectiveState (int pitchClass) const;

private:
    juce::AudioProcessorValueTreeState& state;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 12> noteAttachments;
    std::unique_ptr<juce::ParameterAttachment> keyAttachment, scaleAttachment;
    std::array<int, 12> states {};
    int key = 0, scale = 0;
    int detected = -1, target = -1;
    juce::Colour accent { 0xffff6b4a };
    juce::Rectangle<float> keysArea;
    juce::TextButton resetButton, allActiveButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteKeyboard)
};

} // namespace novapitch
