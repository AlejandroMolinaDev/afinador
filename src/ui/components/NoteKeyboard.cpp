#include "ui/components/NoteKeyboard.h"

#include "dsp/NoteMapper.h"
#include "dsp/Scales.h"
#include "plugin/Parameters.h"
#include "ui/LookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace novapitch
{

namespace
{
juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

constexpr std::array<bool, 12> kBlack { false, true, false, true, false, false, true, false, true, false, true, false };
constexpr std::array<int, 7> kWhitePcs { 0, 2, 4, 5, 7, 9, 11 };
constexpr std::array<const char*, 12> kNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr std::array<const char*, 4> kStateNames { "Escala", "Activa", "Ignorar", "Eliminar" };
} // namespace

NoteKeyboard::NoteKeyboard (juce::AudioProcessorValueTreeState& s) : state (s)
{
    for (int pc = 0; pc < 12; ++pc)
        if (auto* p = state.getParameter (ParamIDs::noteState (pc)))
        {
            noteAttachments[static_cast<size_t> (pc)] = std::make_unique<juce::ParameterAttachment> (*p, [this, pc] (float v) {
                states[static_cast<size_t> (pc)] = juce::jlimit (0, dsp::kNumNoteStates - 1, juce::roundToInt (v));
                repaint();
            });
            noteAttachments[static_cast<size_t> (pc)]->sendInitialUpdate();
        }
    for (int pc = 0; pc < 12; ++pc)
        if (auto* p = state.getParameter (ParamIDs::noteDetune (pc)))
        {
            detuneAttachments[static_cast<size_t> (pc)] = std::make_unique<juce::ParameterAttachment> (*p, [this, pc] (float v) {
                detunes[static_cast<size_t> (pc)] = v;
                repaint();
            });
            detuneAttachments[static_cast<size_t> (pc)]->sendInitialUpdate();
        }
    if (auto* p = state.getParameter (ParamIDs::key))
    {
        keyAttachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { key = juce::roundToInt (v); repaint(); });
        keyAttachment->sendInitialUpdate();
    }
    if (auto* p = state.getParameter (ParamIDs::scale))
    {
        scaleAttachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { scale = juce::roundToInt (v); repaint(); });
        scaleAttachment->sendInitialUpdate();
    }

    resetButton.setButtonText (u8 ("Restablecer"));
    resetButton.setTooltip (u8 ("Todas las notas vuelven a «Escala»: siguen la tonalidad y la escala elegidas."));
    resetButton.onClick = [this] {
        for (int pc = 0; pc < 12; ++pc)
            setState (pc, static_cast<int> (dsp::NoteState::FollowScale));
    };
    allActiveButton.setButtonText (u8 ("Todas activas"));
    allActiveButton.setTooltip (u8 ("Las 12 notas son destino válido (como la escala cromática)."));
    allActiveButton.onClick = [this] {
        for (int pc = 0; pc < 12; ++pc)
            setState (pc, static_cast<int> (dsp::NoteState::Active));
    };
    resetButton.setComponentID ("notesReset");
    allActiveButton.setComponentID ("notesAllActive");
    for (auto* b : { &resetButton, &allActiveButton })
    {
        b->setColour (juce::TextButton::buttonColourId, Palette::panelRaised);
        b->setColour (juce::TextButton::textColourOffId, Palette::textPrimary.withAlpha (0.85f));
        addAndMakeVisible (*b);
    }
    setTooltip (u8 ("Clic: cambia el estado de la nota (Escala → Activa → Ignorar → Eliminar). Clic derecho: estado o "
                    "desafinación.\nRueda sobre una tecla: desafina esa nota ±1 ¢ (Mayús: ±10 ¢), para escalas microtonales.\n"
                    "Activa: destino de la corrección. Ignorar: pasa la afinación original. "
                    "Eliminar: se corrige a la nota válida más cercana."));
}

NoteKeyboard::~NoteKeyboard() = default;

void NoteKeyboard::setLiveNotes (int detectedPc, int targetPc)
{
    if (detectedPc != detected || targetPc != target)
    {
        detected = detectedPc;
        target = targetPc;
        repaint (keysArea.toNearestInt().expanded (2));
    }
}

void NoteKeyboard::setState (int pitchClass, int newState)
{
    if (pitchClass < 0 || pitchClass >= 12)
        return;
    if (auto& a = noteAttachments[static_cast<size_t> (pitchClass)])
        a->setValueAsCompleteGesture (static_cast<float> (juce::jlimit (0, dsp::kNumNoteStates - 1, newState)));
}

void NoteKeyboard::setDetune (int pitchClass, float cents)
{
    if (pitchClass < 0 || pitchClass >= 12)
        return;
    if (auto& a = detuneAttachments[static_cast<size_t> (pitchClass)])
        a->setValueAsCompleteGesture (juce::jlimit (-50.0f, 50.0f, std::round (cents * 10.0f) / 10.0f));
}

float NoteKeyboard::getDetune (int pitchClass) const
{
    return detunes[static_cast<size_t> (juce::jlimit (0, 11, pitchClass))];
}

void NoteKeyboard::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int pc = pitchClassAt (e.position);
    const float dy = w.isReversed ? -w.deltaY : w.deltaY;
    if (pc < 0 || std::abs (dy) < 1.0e-4f)
        return;
    setDetune (pc, getDetune (pc) + (dy > 0.0f ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 10.0f : 1.0f));
}

void NoteKeyboard::askExactDetune (int pc)
{
    detuneDialog = std::make_unique<juce::AlertWindow> (u8 ("Desafinación de ") + u8 (kNames[static_cast<size_t> (pc)]),
                                                        u8 ("Cents respecto al temperamento igual (−50 a +50)."),
                                                        juce::MessageBoxIconType::NoIcon, this);
    detuneDialog->addTextEditor ("cents", juce::String (getDetune (pc), 1));
    detuneDialog->addButton (u8 ("Aceptar"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    detuneDialog->addButton (u8 ("Cancelar"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    // May be called after the keyboard is deleted (editor closed with the dialog open).
    detuneDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<NoteKeyboard> (this), pc] (int result) {
            if (safe == nullptr)
                return;
            if (result == 1 && safe->detuneDialog != nullptr)
            {
                const auto text = safe->detuneDialog->getTextEditorContents ("cents").replaceCharacter (',', '.')
                                      .replace (u8 ("−"), "-").retainCharacters ("0123456789.+-");
                safe->setDetune (pc, text.getFloatValue());
            }
            safe->detuneDialog.reset();
        }));
}

int NoteKeyboard::getState (int pitchClass) const
{
    return states[static_cast<size_t> (juce::jlimit (0, 11, pitchClass))];
}

int NoteKeyboard::getEffectiveState (int pitchClass) const
{
    dsp::NoteMapper mapper;
    mapper.setKeyAndScale (key, dsp::scaleTypeFromIndex (scale));
    for (int pc = 0; pc < 12; ++pc)
        mapper.setNoteState (pc, dsp::noteStateFromIndex (states[static_cast<size_t> (pc)]));
    return static_cast<int> (mapper.getEffectiveState (pitchClass));
}

void NoteKeyboard::resized()
{
    auto r = getLocalBounds();
    auto side = r.removeFromRight (190).reduced (12, 8);
    side.removeFromTop (70); // legend (painted)
    allActiveButton.setBounds (side.removeFromBottom (26));
    side.removeFromBottom (6);
    resetButton.setBounds (side.removeFromBottom (26));
    keysArea = r.toFloat().reduced (10.0f, 8.0f);
}

juce::Rectangle<float> NoteKeyboard::keyBounds (int pc) const
{
    const float whiteW = keysArea.getWidth() / 7.0f;
    if (! kBlack[static_cast<size_t> (pc)])
    {
        const auto index = static_cast<float> (std::find (kWhitePcs.begin(), kWhitePcs.end(), pc) - kWhitePcs.begin());
        return { keysArea.getX() + index * whiteW, keysArea.getY(), whiteW, keysArea.getHeight() };
    }
    // Black key centred on the boundary after the white key below it.
    const auto below = static_cast<float> (std::find (kWhitePcs.begin(), kWhitePcs.end(), pc - 1) - kWhitePcs.begin());
    const float w = whiteW * 0.62f;
    return { keysArea.getX() + (below + 1.0f) * whiteW - 0.5f * w, keysArea.getY(), w, keysArea.getHeight() * 0.58f };
}

int NoteKeyboard::pitchClassAt (juce::Point<float> p) const
{
    for (int pc = 0; pc < 12; ++pc) // black keys are on top
        if (kBlack[static_cast<size_t> (pc)] && keyBounds (pc).contains (p))
            return pc;
    for (int pc : kWhitePcs)
        if (keyBounds (pc).contains (p))
            return pc;
    return -1;
}

namespace
{
constexpr float kQuickDetunes[] = { -50.0f, -31.0f, -25.0f, -14.0f, 0.0f, 2.0f, 14.0f, 25.0f, 50.0f };
} // namespace

juce::PopupMenu NoteKeyboard::createKeyMenu (int pc) const
{
    juce::PopupMenu detune;
    for (size_t i = 0; i < std::size (kQuickDetunes); ++i)
    {
        const float c = kQuickDetunes[i];
        const auto label = std::abs (c) < 0.01f ? u8 ("Sin desafinación (0 ¢)") : (c > 0.0f ? "+" : "") + juce::String (juce::roundToInt (c)) + u8 (" ¢");
        detune.addItem (100 + static_cast<int> (i), label, true, std::abs (getDetune (pc) - c) < 0.05f);
    }
    detune.addSeparator();
    detune.addItem (200, u8 ("Otro valor…"));

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel()); // JUCE menus don't inherit the target's look
    menu.addSectionHeader (u8 (kNames[static_cast<size_t> (pc)]));
    for (int s = 0; s < dsp::kNumNoteStates; ++s)
        menu.addItem (s + 1, u8 (kStateNames[static_cast<size_t> (s)]), true, getState (pc) == s);
    menu.addSeparator();
    menu.addSubMenu (u8 ("Desafinación (") + (getDetune (pc) > 0.05f ? "+" : "") + juce::String (getDetune (pc), 1) + u8 (" ¢)"), detune);
    menu.addItem (300, u8 ("Quitar todas las desafinaciones"));
    return menu;
}

void NoteKeyboard::keyMenuItemChosen (int pc, int result)
{
    if (pc < 0 || pc >= 12)
        return;
    if (result >= 1 && result <= dsp::kNumNoteStates)
        setState (pc, result - 1);
    else if (result >= 100 && result < 100 + static_cast<int> (std::size (kQuickDetunes)))
        setDetune (pc, kQuickDetunes[static_cast<size_t> (result - 100)]);
    else if (result == 200)
        askExactDetune (pc);
    else if (result == 300)
        for (int k = 0; k < 12; ++k)
            setDetune (k, 0.0f);
}

void NoteKeyboard::mouseDown (const juce::MouseEvent& e)
{
    const int pc = pitchClassAt (e.position);
    if (pc < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        createKeyMenu (pc).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                          [safe = juce::Component::SafePointer<NoteKeyboard> (this), pc] (int result) {
                                              if (safe != nullptr)
                                                  safe->keyMenuItemChosen (pc, result);
                                          });
        return;
    }
    setState (pc, (getState (pc) + 1) % dsp::kNumNoteStates);
}

void NoteKeyboard::paint (juce::Graphics& g)
{
    auto drawKey = [&] (int pc) {
        const auto r = keyBounds (pc).reduced (1.5f, 0.0f);
        const bool black = kBlack[static_cast<size_t> (pc)];
        const int st = getState (pc);
        const int eff = getEffectiveState (pc);

        g.setColour (black ? juce::Colour (0xff12161b) : juce::Colour (0xff2b333d));
        g.fillRoundedRectangle (r, 5.0f);

        // State colour band at the bottom of the key
        juce::Colour band = Palette::textMuted.withAlpha (0.25f);
        if (eff == static_cast<int> (dsp::NoteState::Active))
            band = accent.withAlpha (st == static_cast<int> (dsp::NoteState::FollowScale) ? 0.55f : 0.95f);
        else if (eff == static_cast<int> (dsp::NoteState::Ignore))
            band = Palette::warn;
        auto bandArea = r.withTrimmedTop (r.getHeight() - (black ? 10.0f : 14.0f)).reduced (4.0f, 3.0f);
        g.setColour (band);
        g.fillRoundedRectangle (bandArea, 3.0f);

        // Live notes: target = accent outline, detected = dot
        if (pc == target)
        {
            g.setColour (accent);
            g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 2.5f);
        }
        else
        {
            g.setColour (Palette::panelOutline);
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
        }
        if (pc == detected)
        {
            g.setColour (Palette::textPrimary);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getCentreX(), r.getY() + 12.0f }));
        }

        // Labels
        const bool removed = eff == static_cast<int> (dsp::NoteState::Remove);
        g.setColour (removed ? Palette::textMuted.withAlpha (0.6f) : Palette::textPrimary);
        g.setFont (juce::FontOptions (black ? 11.0f : 14.0f, juce::Font::bold));
        auto text = r.withTrimmedBottom (black ? 12.0f : 18.0f);
        g.drawText (u8 (kNames[static_cast<size_t> (pc)]), text.removeFromBottom (black ? 14.0f : 18.0f), juce::Justification::centred);
        g.setFont (juce::FontOptions (black ? 8.5f : 10.0f));
        g.setColour (st == static_cast<int> (dsp::NoteState::FollowScale) ? Palette::textMuted : Palette::textPrimary.withAlpha (0.9f));
        g.drawFittedText (u8 (kStateNames[static_cast<size_t> (st)]), text.removeFromBottom (13.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        if (const float d = getDetune (pc); std::abs (d) >= 0.05f)
        {
            g.setColour (Palette::warn);
            g.setFont (juce::FontOptions (black ? 8.5f : 10.0f, juce::Font::bold));
            g.drawFittedText ((d > 0.0f ? "+" : "") + juce::String (d, std::abs (d - std::round (d)) < 0.05f ? 0 : 1) + u8 (" ¢"),
                              text.removeFromBottom (13.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
        if (removed)
        {
            g.setColour (Palette::textMuted.withAlpha (0.35f));
            g.drawLine (r.getX() + 6.0f, r.getBottom() - 22.0f, r.getRight() - 6.0f, r.getY() + 22.0f, 1.0f);
        }
    };

    for (int pc : kWhitePcs)
        drawKey (pc);
    for (int pc = 0; pc < 12; ++pc)
        if (kBlack[static_cast<size_t> (pc)])
            drawKey (pc);

    // Legend
    auto legend = getLocalBounds().removeFromRight (190).reduced (12, 8).toFloat();
    g.setFont (juce::FontOptions (11.0f));
    const std::pair<juce::Colour, const char*> items[] = { { accent, "Activa (destino)" },
                                                           { Palette::warn, "Ignorar (sin corregir)" },
                                                           { Palette::textMuted.withAlpha (0.4f), "Eliminar / fuera de escala" },
                                                           { juce::Colours::transparentBlack, "Rueda: desafinar (±1 ¢)" } };
    for (const auto& [colour, label] : items)
    {
        auto row = legend.removeFromTop (17.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (row.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, 7.0f), 2.0f);
        g.setColour (Palette::textMuted);
        g.drawText (u8 (label), row.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
    }
}

} // namespace novapitch
