#pragma once

// Automatic layout checks on a live component tree (whatever page / view /
// scale is showing):
//   1. every visible control lies inside the window and is not clipped by any
//      of its parents;
//   2. no two controls (or a control and a display) overlap;
//   3. the centre of each control is hit by that control (not covered by
//      another component, which would swallow the clicks);
//   4. every text fits: buttons, switches, labels (incl. knob values at their
//      min / default / max) and every item of every combo box, measured with
//      the real LookAndFeel fonts and the same text areas the LookAndFeel uses;
//   5. clickable targets keep a usable size.
// Painted captions (drawn in paint()) are not components: they are reviewed in
// the screenshots.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/components/NoteKeyboard.h"

#include <functional>
#include <typeinfo>
#include <vector>

namespace novapitch::uitest
{

struct LayoutReport
{
    std::vector<juce::String> issues;
    int controls = 0, texts = 0;
    void add (const juce::String& s) { issues.push_back (s); }
    juce::String summary() const { return juce::StringArray (issues.data(), static_cast<int> (issues.size())).joinIntoString ("\n"); }
};

/** Squashing a text below this horizontal scale counts as "does not fit". */
constexpr float kMinTextScale = 1.0f; // no squashing at all

inline juce::String describe (const juce::Component& c)
{
    juce::String name = typeid (c).name();
    if (auto* b = dynamic_cast<const juce::Button*> (&c))
        name << " \"" << b->getButtonText() << "\"";
    else if (auto* l = dynamic_cast<const juce::Label*> (&c))
        name << " \"" << l->getText() << "\"";
    else if (auto* cb = dynamic_cast<const juce::ComboBox*> (&c))
        name << " \"" << cb->getText() << "\"";
    juce::StringArray path;
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (p->getComponentID().isNotEmpty())
            path.insert (0, p->getComponentID());
    if (! path.isEmpty())
        name << " [" << path.joinIntoString ("/") << "]";
    return name;
}

inline bool isTooltip (const juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (dynamic_cast<const juce::TooltipWindow*> (p) != nullptr)
            return true;
    return false;
}

/** Parts of a compound control (a slider's value box, a combo's label...). */
inline bool isInsideCompound (const juce::Component& c, const juce::Component& root)
{
    for (auto* p = c.getParentComponent(); p != nullptr && p != &root; p = p->getParentComponent())
        if (dynamic_cast<const juce::Slider*> (p) != nullptr || dynamic_cast<const juce::ComboBox*> (p) != nullptr
            || dynamic_cast<const juce::TextEditor*> (p) != nullptr)
            return true;
    return false;
}

inline bool isControl (const juce::Component& c)
{
    return dynamic_cast<const juce::Button*> (&c) != nullptr || dynamic_cast<const juce::Slider*> (&c) != nullptr
           || dynamic_cast<const juce::ComboBox*> (&c) != nullptr || dynamic_cast<const juce::TextEditor*> (&c) != nullptr
           || dynamic_cast<const NoteKeyboard*> (&c) != nullptr || c.getComponentID() == "roll";
}

inline bool isDisplay (const juce::Component& c)
{
    const auto id = c.getComponentID();
    return id == "meter" || id == "trace" || id == "vibratoDisplay";
}

inline void collect (juce::Component& c, juce::Component& root, std::vector<juce::Component*>& leaves, std::vector<juce::Component*>& texts)
{
    for (auto* child : c.getChildren())
    {
        if (! child->isVisible() || isTooltip (*child))
            continue;
        const bool compoundPart = isInsideCompound (*child, root);
        if (! compoundPart && (isControl (*child) || isDisplay (*child)))
            leaves.push_back (child);
        if (dynamic_cast<juce::Label*> (child) != nullptr || dynamic_cast<juce::Button*> (child) != nullptr
            || dynamic_cast<juce::ComboBox*> (child) != nullptr)
            texts.push_back (child);
        collect (*child, root, leaves, texts);
    }
}

inline float textWidth (const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (font, text);
}

/** Checks that `text` fits in `available` px with font `font` (allowing a light squash). */
inline void checkText (LayoutReport& report, const juce::Component& c, const juce::String& text, const juce::Font& font,
                       float available, float minScale, const juce::String& what)
{
    if (text.trim().isEmpty())
        return;
    ++report.texts;
    const float natural = textWidth (font, text);
    if (available <= 0.0f || natural * minScale > available + 0.5f)
        report.add ("text does not fit (" + what + "): \"" + text + "\" needs " + juce::String (natural, 1) + " px, has "
                    + juce::String (available, 1) + " px in " + describe (c));
}

inline void checkTexts (LayoutReport& report, juce::Component& c)
{
    auto& lf = c.getLookAndFeel();
    if (auto* tb = dynamic_cast<juce::TextButton*> (&c))
    {
        // Same text area as LookAndFeel_V2::drawButtonText.
        const auto font = lf.getTextButtonFont (*tb, tb->getHeight());
        const int cornerSize = juce::jmin (tb->getHeight(), tb->getWidth()) / 2;
        const int fontHeight = juce::roundToInt (font.getHeight() * 0.6f);
        const int left = juce::jmin (fontHeight, 2 + cornerSize / (tb->isConnectedOnLeft() ? 4 : 2));
        const int right = juce::jmin (fontHeight, 2 + cornerSize / (tb->isConnectedOnRight() ? 4 : 2));
        checkText (report, c, tb->getButtonText(), font, static_cast<float> (tb->getWidth() - left - right), kMinTextScale, "button");
    }
    else if (auto* toggle = dynamic_cast<juce::ToggleButton*> (&c))
    {
        // NovaLookAndFeel::drawToggleButton: 34 px switch at x = 1..35, caption from x = 45, no squashing.
        checkText (report, c, toggle->getButtonText(), juce::Font (juce::FontOptions (13.0f)),
                   static_cast<float> (toggle->getWidth() - 45), 1.0f, "switch caption");
    }
    else if (auto* label = dynamic_cast<juce::Label*> (&c))
    {
        if (label->isBeingEdited())
            return;
        const auto font = lf.getLabelFont (*label);
        const auto area = lf.getLabelBorderSize (*label).subtractedFrom (label->getLocalBounds());
        const int lines = juce::jmax (1, static_cast<int> (static_cast<float> (area.getHeight()) / font.getHeight()));
        if (area.getHeight() + 1.0f < font.getHeight())
            report.add ("label too short for its font (" + juce::String (area.getHeight()) + " px < "
                        + juce::String (font.getHeight(), 1) + " px): " + describe (c));
        const float minScale = juce::jmax (kMinTextScale, label->getMinimumHorizontalScale());
        checkText (report, c, label->getText(), font, static_cast<float> (area.getWidth() * lines), minScale, "label");

        // A knob's value box: check the widest values, not only the current one.
        if (auto* slider = dynamic_cast<juce::Slider*> (label->getParentComponent()))
        {
            const auto range = slider->getRange();
            for (const double v : { range.getStart(), range.getEnd(), slider->getDoubleClickReturnValue(),
                                    range.getStart() + 0.5 * range.getLength(), range.getStart() + 0.123 * range.getLength() })
                checkText (report, c, slider->getTextFromValue (v), font, static_cast<float> (area.getWidth()), minScale,
                           "knob value");
        }
    }
    else if (auto* combo = dynamic_cast<juce::ComboBox*> (&c))
    {
        // LookAndFeel_V4::positionComboBoxText: label = (w - 30) x (h - 2), default label border 5 + 5.
        const auto font = lf.getComboBoxFont (*combo);
        const float available = static_cast<float> (combo->getWidth() - 30 - 10);
        const bool presetBox = combo->getComponentID() == "presetBox";
        for (int i = 0; i < combo->getNumItems(); ++i)
            checkText (report, c, combo->getItemText (i) + (presetBox ? " *" : ""), font, available, kMinTextScale, "combo item");
    }
}

/** Runs every check on `root` (normally the editor) as it is showing now. */
inline LayoutReport checkLayout (juce::Component& root, float minTarget = 14.0f)
{
    LayoutReport report;
    // A tooltip is transient (the next mouse move hides it): not part of the layout.
    std::function<void (juce::Component&)> hideTips = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (auto* tip = dynamic_cast<juce::TooltipWindow*> (child); tip != nullptr && tip->isVisible())
                tip->hideTip();
            hideTips (*child);
        }
    };
    hideTips (root);
    std::vector<juce::Component*> leaves, texts;
    collect (root, root, leaves, texts);
    report.controls = static_cast<int> (leaves.size());

    const auto window = root.getLocalBounds().toFloat().expanded (0.5f);
    const float scale = static_cast<float> (root.getWidth()) / 760.0f; // PluginEditor::kBaseWidth
    std::vector<juce::Rectangle<float>> areas;
    for (auto* c : leaves)
    {
        const auto area = root.getLocalArea (c, c->getLocalBounds().toFloat());
        areas.push_back (area);
        if (! window.contains (area))
            report.add ("outside the window: " + describe (*c) + " at " + area.toString());

        for (auto* p = c->getParentComponent(); p != nullptr && p != &root; p = p->getParentComponent())
        {
            const auto inParent = p->getLocalArea (c, c->getLocalBounds().toFloat());
            if (! p->getLocalBounds().toFloat().expanded (0.5f).contains (inParent))
            {
                report.add ("clipped by its parent " + describe (*p) + ": " + describe (*c));
                break;
            }
        }

        const auto centre = area.getCentre();
        auto* under = root.getComponentAt (centre.toInt());
        if (under != c && ! c->isParentOf (under))
            report.add ("centre covered by " + (under != nullptr ? describe (*under) : juce::String ("nothing")) + ": " + describe (*c));

        if (isControl (*c) && (area.getWidth() < minTarget * scale || area.getHeight() < minTarget * scale))
            report.add ("too small to click (" + area.toString() + "): " + describe (*c));
    }

    for (size_t i = 0; i < leaves.size(); ++i)
        for (size_t j = i + 1; j < leaves.size(); ++j)
        {
            if (leaves[i]->isParentOf (leaves[j]) || leaves[j]->isParentOf (leaves[i]))
                continue;
            const auto overlap = areas[i].getIntersection (areas[j]);
            if (overlap.getWidth() > 1.0f && overlap.getHeight() > 1.0f)
                report.add ("overlap " + overlap.toString() + ": " + describe (*leaves[i]) + " / " + describe (*leaves[j]));
        }

    for (auto* t : texts)
        if (t->isShowing())
            checkTexts (report, *t);
    return report;
}

} // namespace novapitch::uitest
