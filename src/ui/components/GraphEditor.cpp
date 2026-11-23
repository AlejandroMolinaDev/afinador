#include "ui/components/GraphEditor.h"

#include "dsp/PitchUtils.h"
#include "plugin/PluginProcessor.h"
#include "plugin/graph/GraphModel.h"
#include "ui/LookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace novapitch
{

namespace
{
juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

constexpr float kKeysWidth = 40.0f;
constexpr double kMinNoteSeconds = 0.02;
constexpr double kMinViewSeconds = 0.25, kMaxViewSeconds = 600.0;
const juce::Colour kCaptureRed { 0xffe5484d };

bool isBlackKey (int note)
{
    const int pc = ((note % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

double getD (const juce::ValueTree& t, const juce::Identifier& id) { return static_cast<double> (t.getProperty (id, 0.0)); }
float getF (const juce::ValueTree& t, const juce::Identifier& id, float def = 0.0f)
{
    return static_cast<float> (static_cast<double> (t.getProperty (id, def)));
}
juce::int64 getId (const juce::ValueTree& t) { return static_cast<juce::int64> (t.getProperty (GraphIds::id, -1)); }

juce::String formatTime (double seconds, double step)
{
    const bool negative = seconds < 0.0;
    seconds = std::abs (seconds);
    const int minutes = static_cast<int> (seconds / 60.0);
    const double rest = seconds - 60.0 * minutes;
    const int decimals = step < 0.1 ? 2 : (step < 1.0 ? 1 : 0);
    char secs[32] {};
    std::snprintf (secs, sizeof (secs), "%0*.*f", decimals > 0 ? 3 + decimals : 2, decimals, rest);
    return (negative ? "-" : "") + juce::String (minutes) + ":" + juce::String (secs);
}

void styleButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, Palette::panelRaised);
    b.setColour (juce::TextButton::textColourOffId, Palette::textPrimary.withAlpha (0.85f));
    b.setColour (juce::TextButton::textColourOnId, Palette::background);
}
} // namespace

//==============================================================================
class GraphEditor::Roll final : public juce::Component
{
public:
    explicit Roll (GraphEditor& o) : owner (o), model (o.model) {}

    // View ------------------------------------------------------------------------
    double viewStart = 0.0, viewLength = 8.0;
    float pitchTop = 72.0f, pitchRange = 24.0f;
    bool follow = true;
    bool userMovedPitch = false;
    std::set<juce::int64> selection;
    double shownPlayhead = -1.0;

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedLeft (kKeysWidth); }
    float xOf (double t) const
    {
        const auto p = plot();
        return p.getX() + static_cast<float> ((t - viewStart) / viewLength) * p.getWidth();
    }
    /** x of a source-time position (objects and the captured curve live in source time). */
    float xs (double sourceTime) const { return xOf (model.getTimeMap().toOutput (sourceTime)); }
    /** Source time under x. */
    double sOf (float x) const { return model.getTimeMap().toSource (tOf (x)); }
    /** End of the visible range in source time. */
    double sourceViewEnd() const { return model.getTimeMap().toSource (viewStart + viewLength + 0.05); }

    double tOf (float x) const
    {
        const auto p = plot();
        return viewStart + static_cast<double> ((x - p.getX()) / p.getWidth()) * viewLength;
    }
    float yOf (float pitch) const
    {
        const auto p = plot();
        return p.getY() + (pitchTop - pitch) / pitchRange * p.getHeight();
    }
    float pOf (float y) const
    {
        const auto p = plot();
        return pitchTop - (y - p.getY()) / p.getHeight() * pitchRange;
    }

    std::vector<juce::ValueTree> selectedObjects() const
    {
        std::vector<juce::ValueTree> out;
        for (const auto& child : model.getObjects())
            if (selection.count (getId (child)) > 0)
                out.push_back (child);
        return out;
    }

    void validateSelection()
    {
        for (auto it = selection.begin(); it != selection.end();)
            it = model.findById (*it).isValid() ? std::next (it) : selection.erase (it);
    }

    void selectAll()
    {
        selection.clear();
        for (const auto& child : model.getObjects())
            selection.insert (getId (child));
        repaint();
        owner.updateInspector();
    }

    void setViewStart (double t)
    {
        viewStart = std::max (-0.25, t);
        repaint();
    }

    void zoomTime (double factor, double anchor)
    {
        const double newLength = std::clamp (viewLength * factor, kMinViewSeconds, kMaxViewSeconds);
        const double u = (anchor - viewStart) / viewLength;
        viewLength = newLength;
        setViewStart (anchor - u * newLength);
    }

    void zoomPitch (float factor, float anchor)
    {
        const float newRange = std::clamp (pitchRange * factor, 6.0f, 72.0f);
        const float u = (pitchTop - anchor) / pitchRange;
        pitchRange = newRange;
        pitchTop = std::clamp (anchor + u * newRange, 12.0f + newRange, 120.0f);
        userMovedPitch = true;
        repaint();
    }

    /** Time / pitch extent of everything in the graph; false when empty. */
    bool extent (double& t0, double& t1, float& p0, float& p1) const
    {
        bool any = false;
        t0 = 1.0e9;
        t1 = -1.0e9;
        p0 = 1000.0f;
        p1 = -1000.0f;
        auto add = [&] (double t, float p) {
            t0 = std::min (t0, t);
            t1 = std::max (t1, t);
            if (p >= 0.0f)
            {
                p0 = std::min (p0, p);
                p1 = std::max (p1, p);
            }
            any = true;
        };
        for (const auto* frames : { &model.getCaptured(), &model.getPendingCapture() })
            for (const auto& f : *frames)
                add (model.getTimeMap().toOutput (f.time), f.midi);
        for (const auto& o : model.getObjects())
        {
            const auto& map = model.getTimeMap();
            add (map.toOutput (getD (o, GraphIds::start)), getF (o, GraphIds::pitch, -1.0f));
            add (map.toOutput (getD (o, GraphIds::end)),
                 o.hasType (GraphIds::line) ? getF (o, GraphIds::endPitch) : getF (o, GraphIds::pitch, -1.0f));
        }
        return any;
    }

    void fitPitch()
    {
        double t0, t1;
        float p0, p1;
        if (! extent (t0, t1, p0, p1) || p1 < p0)
            return;
        pitchRange = std::clamp (p1 - p0 + 6.0f, 12.0f, 72.0f);
        pitchTop = std::clamp (0.5f * (p0 + p1) + 0.5f * pitchRange, 12.0f + pitchRange, 120.0f);
        repaint();
    }

    /** Shows everything; `minLength` keeps a sensible zoom for the automatic fit. */
    void fitAll (double minLength = kMinViewSeconds)
    {
        double t0, t1;
        float p0, p1;
        if (! extent (t0, t1, p0, p1))
        {
            viewStart = 0.0;
            viewLength = 8.0;
            repaint();
            return;
        }
        viewLength = std::clamp ((t1 - t0) * 1.08 + 0.2, minLength, kMaxViewSeconds);
        viewStart = std::max (-0.25, t0 - 0.04 * viewLength);
        userMovedPitch = false;
        fitPitch();
        repaint();
    }

    /** Pages the view when the playhead leaves it (follow mode). Returns true if the view moved. */
    bool ensureVisible (double t)
    {
        if (t >= viewStart && t <= viewStart + 0.92 * viewLength)
            return false;
        setViewStart (t - 0.05 * viewLength);
        return true;
    }

    void updatePlayhead (double t, bool playing)
    {
        const double shown = playing ? t : -1.0;
        if (std::abs (shown - shownPlayhead) < 1.0e-4)
            return;
        auto strip = [this] (double time) {
            if (time >= 0.0)
                repaint (juce::Rectangle<float> (xOf (time) - 4.0f, 0.0f, 8.0f, static_cast<float> (getHeight())).toNearestInt());
        };
        strip (shownPlayhead);
        shownPlayhead = shown;
        strip (shownPlayhead);
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const auto accent = owner.accent;
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (area, 8.0f);

        const auto pl = plot();
        const int lowest = static_cast<int> (std::floor (pOf (pl.getBottom())));
        const int highest = static_cast<int> (std::ceil (pOf (pl.getY())));
        const float rowHeight = pl.getHeight() / pitchRange;

        // Keyboard strip + pitch lanes
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (area.toNearestInt());
            g.setFont (juce::FontOptions (std::clamp (rowHeight * 0.75f, 8.0f, 11.0f)));
            for (int n = lowest; n <= highest; ++n)
            {
                const float y0 = yOf (static_cast<float> (n) + 0.5f), y1 = yOf (static_cast<float> (n) - 0.5f);
                const auto lane = juce::Rectangle<float> (pl.getX(), y0, pl.getWidth(), y1 - y0);
                if (isBlackKey (n))
                {
                    g.setColour (Palette::background.withAlpha (0.45f));
                    g.fillRect (lane);
                }
                const bool isC = ((n % 12) + 12) % 12 == 0;
                g.setColour (isC ? Palette::panelOutline : Palette::grid.withAlpha (0.6f));
                g.drawHorizontalLine (juce::roundToInt (y1), pl.getX(), pl.getRight());

                const auto key = juce::Rectangle<float> (area.getX() + 2.0f, y0, kKeysWidth - 4.0f, y1 - y0).reduced (0.0f, 0.5f);
                g.setColour (isBlackKey (n) ? Palette::background : Palette::panelRaised.brighter (0.15f));
                g.fillRect (key);
                if (isC || rowHeight >= 13.0f)
                {
                    g.setColour (isBlackKey (n) ? Palette::textMuted : Palette::textPrimary.withAlpha (isC ? 0.95f : 0.6f));
                    g.drawText (juce::String (dsp::midiNoteName (n)), key.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
                }
            }
        }

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (pl.toNearestInt());

        // Time grid
        static constexpr double steps[] = { 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0 };
        double step = steps[std::size (steps) - 1];
        for (double s : steps)
            if (s / viewLength * pl.getWidth() >= 70.0)
            {
                step = s;
                break;
            }
        g.setFont (juce::FontOptions (10.0f));
        for (double t = std::floor (viewStart / step) * step; t <= viewStart + viewLength; t += step)
        {
            const float x = xOf (t);
            g.setColour (Palette::grid);
            g.drawVerticalLine (juce::roundToInt (x), pl.getY(), pl.getBottom());
            g.setColour (Palette::textMuted.withAlpha (0.7f));
            g.drawText (formatTime (t, step), juce::Rectangle<float> (x + 3.0f, pl.getY() + 2.0f, 60.0f, 12.0f), juce::Justification::centredLeft);
        }

        const auto& captured = model.getCaptured();
        const auto& pending = model.getPendingCapture();

        // Level overview (bottom band)
        auto drawLevel = [&] (const std::vector<CaptureFrame>& frames, juce::Colour c) {
            if (frames.empty())
                return;
            const float base = pl.getBottom(), height = 24.0f;
            juce::Path p;
            bool started = false;
            const double srcEnd = sourceViewEnd();
            for (auto it = visibleBegin (frames); it != frames.end() && it->time <= srcEnd; ++it)
            {
                const float level = std::clamp ((it->rmsDb + 60.0f) / 60.0f, 0.0f, 1.0f);
                const float x = xs (it->time), y = base - level * height;
                if (! started)
                {
                    p.startNewSubPath (x, base);
                    started = true;
                }
                p.lineTo (x, y);
            }
            if (! started)
                return;
            p.lineTo (p.getCurrentPosition().x, base);
            p.closeSubPath();
            g.setColour (c);
            g.fillPath (p);
        };
        drawLevel (captured, Palette::textMuted.withAlpha (0.12f));
        drawLevel (pending, kCaptureRed.withAlpha (0.12f));

        // Captured pitch curve
        auto drawCurve = [&] (const std::vector<CaptureFrame>& frames, juce::Colour c) {
            juce::Path p;
            bool open = false;
            double last = -1.0e9;
            const auto begin = visibleBegin (frames);
            const auto count = std::distance (begin, frames.end());
            const size_t stride = static_cast<size_t> (std::max<long> (1, static_cast<long> (count) / static_cast<long> (pl.getWidth() * 3.0f)));
            const double srcEnd = sourceViewEnd();
            for (auto it = begin; it < frames.end() && it->time <= srcEnd;
                 it += static_cast<long> (std::min<size_t> (stride, static_cast<size_t> (frames.end() - it))))
            {
                if (it->midi < 0.0f || it->time - last > 0.03 * static_cast<double> (stride))
                    open = false;
                if (it->midi >= 0.0f)
                {
                    const float x = xs (it->time), y = yOf (it->midi);
                    if (open)
                        p.lineTo (x, y);
                    else
                        p.startNewSubPath (x, y);
                    open = true;
                }
                last = it->time;
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        drawCurve (captured, Palette::textPrimary.withAlpha (0.55f));

        // Predicted corrected curve (where objects act)
        if (owner.predicted.size() == captured.size())
        {
            juce::Path p;
            bool open = false;
            const auto begin = visibleBegin (captured);
            const double srcEnd = sourceViewEnd();
            for (auto it = begin; it != captured.end() && it->time <= srcEnd; ++it)
            {
                const float out = owner.predicted[static_cast<size_t> (it - captured.begin())];
                if (out < 0.0f)
                {
                    open = false;
                    continue;
                }
                const float x = xs (it->time), y = yOf (out);
                if (open)
                    p.lineTo (x, y);
                else
                    p.startNewSubPath (x, y);
                open = true;
            }
            g.setColour (accent.brighter (0.35f));
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        drawCurve (pending, kCaptureRed.withAlpha (0.85f));

        // Objects
        for (const auto& o : model.getObjects())
        {
            const bool selected = selection.count (getId (o)) > 0;
            const double s = model.getTimeMap().toOutput (getD (o, GraphIds::start));
            const double e = model.getTimeMap().toOutput (getD (o, GraphIds::end));
            if (e < viewStart || s > viewStart + viewLength)
                continue;

            if (o.hasType (GraphIds::note))
            {
                const auto r = noteRect (o);
                g.setColour (accent.withAlpha (selected ? 0.62f : 0.36f));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (selected ? Palette::textPrimary : accent);
                g.drawRoundedRectangle (r, 3.0f, selected ? 1.6f : 1.0f);
                if (r.getWidth() > 30.0f && r.getHeight() >= 9.0f)
                {
                    g.setColour (Palette::textPrimary);
                    g.setFont (juce::FontOptions (std::min (11.0f, r.getHeight() - 1.0f), juce::Font::bold));
                    g.drawText (juce::String (dsp::midiNoteName (static_cast<int> (std::lround (getF (o, GraphIds::pitch))))),
                                r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
                }
                const bool customised = getF (o, GraphIds::retuneMs, -1.0f) >= 0.0f || getF (o, GraphIds::vibratoKeep, -1.0f) >= 0.0f
                                        || std::abs (getF (o, GraphIds::formantSt)) > 0.05f;
                if (customised && r.getWidth() > 12.0f)
                {
                    g.setColour (Palette::warn);
                    g.fillEllipse (r.getRight() - 7.0f, r.getY() + 2.0f, 4.0f, 4.0f);
                }
            }
            else if (o.hasType (GraphIds::line))
            {
                const juce::Point<float> a (xOf (s), yOf (getF (o, GraphIds::pitch))), b (xOf (e), yOf (getF (o, GraphIds::endPitch)));
                if (selected)
                {
                    g.setColour (Palette::textPrimary);
                    g.drawLine ({ a, b }, 5.0f);
                }
                g.setColour (accent);
                g.drawLine ({ a, b }, 3.0f);
                for (auto pt : { a, b })
                    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (pt));
            }
            else if (o.hasType (GraphIds::curve))
            {
                const auto pts = GraphModel::readCurvePoints (o);
                juce::Path p;
                for (size_t i = 0; i < pts.size(); ++i)
                {
                    const float x = xs (pts[i].first), y = yOf (pts[i].second);
                    if (i == 0)
                        p.startNewSubPath (x, y);
                    else
                        p.lineTo (x, y);
                }
                if (selected)
                {
                    g.setColour (Palette::textPrimary);
                    g.strokePath (p, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                g.setColour (accent);
                g.strokePath (p, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // Gestures in progress
        if (drag == Drag::LineDraw)
        {
            g.setColour (accent.withAlpha (0.8f));
            g.drawLine ({ xs (lineT0), yOf (lineP0), xs (lineT1), yOf (lineP1) }, 3.0f);
        }
        else if (drag == Drag::CurveDraw && drawn.size() > 1)
        {
            juce::Path p;
            p.startNewSubPath (xs (drawn.front().first), yOf (drawn.front().second));
            for (size_t i = 1; i < drawn.size(); ++i)
                p.lineTo (xs (drawn[i].first), yOf (drawn[i].second));
            g.setColour (accent.withAlpha (0.8f));
            g.strokePath (p, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (drag == Drag::Rubber)
        {
            g.setColour (accent.withAlpha (0.12f));
            g.fillRect (rubber);
            g.setColour (accent.withAlpha (0.7f));
            g.drawRect (rubber, 1.0f);
        }

        // Time-edit anchors (ruler markers)
        for (const auto& [out, src] : model.getTimeMap().anchors)
        {
            const float x = xOf (out);
            if (x < pl.getX() - 6.0f || x > pl.getRight() + 6.0f)
                continue;
            const bool moved = std::abs (out - src) > 1.0e-6;
            g.setColour (moved ? Palette::warn : Palette::textMuted.withAlpha (0.6f));
            juce::Path marker;
            marker.addTriangle (x - 4.0f, pl.getY() + 14.0f, x + 4.0f, pl.getY() + 14.0f, x, pl.getY() + 20.0f);
            g.fillPath (marker);
            g.setColour (Palette::warn.withAlpha (moved ? 0.25f : 0.0f));
            g.drawVerticalLine (juce::roundToInt (x), pl.getY() + 20.0f, pl.getBottom());
        }

        // Playhead
        if (shownPlayhead >= 0.0)
        {
            const float x = xOf (shownPlayhead);
            g.setColour (accent);
            g.drawLine (x, pl.getY(), x, pl.getBottom(), 1.5f);
            juce::Path tri;
            tri.addTriangle (x - 5.0f, pl.getY(), x + 5.0f, pl.getY(), x, pl.getY() + 7.0f);
            g.fillPath (tri);
        }

        // Empty state
        if (captured.empty() && pending.empty() && model.getObjects().getNumChildren() == 0)
        {
            g.setColour (Palette::textMuted);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            auto box = pl.withSizeKeepingCentre (520.0f, 90.0f);
            g.drawText (u8 ("Sin captura todavía"), box.removeFromTop (24.0f), juce::Justification::centred);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (u8 ("1. Pulsa «Capturar» y reproduce la pista en FL Studio.\n"
                                  "2. «Importar desde automático» crea las notas en la tonalidad actual.\n"
                                  "3. Edita: arrastra notas, dibuja líneas o curvas. Fuera de los objetos pasa la voz original."),
                              box.toNearestInt(), juce::Justification::centredTop, 4, 1.0f);
        }
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.grabKeyboardFocus();
        const auto pos = e.position;
        const double t = sOf (pos.x); // source time: objects follow their audio
        const float p = pOf (pos.y);
        dragStartTime = t;
        dragStartPitch = p;
        appliedDt = 0.0;
        appliedDp = 0.0f;
        drag = Drag::None;
        dragObject = {};

        const auto hit = hitObject (pos);

        // Right click deletes (like FL Studio's piano roll), whatever the tool.
        if (e.mods.isPopupMenu())
        {
            if (hit.object.isValid())
            {
                model.beginEdit (u8 ("Borrar"));
                selection.erase (getId (hit.object));
                model.remove (hit.object);
            }
            return;
        }

        model.beginEdit();
        const auto tool = owner.tool;

        if (tool == Tool::Time)
        {
            if (hit.object.isValid() && hit.object.hasType (GraphIds::note))
                beginTimeEdit (hit.object, hit.mode, e.mods.isAltDown(), pos.x);
            repaint();
            return;
        }

        if (tool == Tool::Select || ((tool == Tool::Note || tool == Tool::Line) && hit.object.isValid()))
        {
            if (hit.object.isValid())
            {
                const auto id = getId (hit.object);
                if (e.mods.isShiftDown())
                {
                    if (selection.count (id) > 0)
                        selection.erase (id);
                    else
                        selection.insert (id);
                }
                else if (selection.count (id) == 0)
                    selection = { id };
                drag = hit.mode;
                dragObject = hit.object;
            }
            else
            {
                if (! e.mods.isShiftDown())
                    selection.clear();
                rubberBase = selection;
                drag = Drag::Rubber;
                rubber = { pos, pos };
            }
        }
        else if (tool == Tool::Note)
        {
            const float pitch = std::round (p);
            createdAnchor = std::max (0.0, t);
            dragObject = model.addNote (createdAnchor, createdAnchor + kMinNoteSeconds, pitch);
            selection = { getId (dragObject) };
            drag = Drag::Create;
        }
        else if (tool == Tool::Line)
        {
            lineT0 = lineT1 = std::max (0.0, t);
            lineP0 = lineP1 = snap (p, e.mods);
            drag = Drag::LineDraw;
        }
        else if (tool == Tool::Curve)
        {
            drawn = { { std::max (0.0, t), p } };
            drag = Drag::CurveDraw;
        }
        else if (tool == Tool::Erase)
        {
            drag = Drag::Erase;
            eraseAt (pos);
        }
        else if (tool == Tool::Split)
        {
            if (hit.object.isValid() && hit.object.hasType (GraphIds::note))
                model.split (hit.object, t);
        }
        owner.updateInspector();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto pos = e.position;
        const double t = sOf (pos.x);
        const float p = pOf (pos.y);

        if (drag == Drag::TimeEdit)
        {
            dragTimeEdit (tOf (pos.x) - timeDragStartOut);
            return;
        }

        switch (drag)
        {
            case Drag::Move:
            {
                auto objects = selectedObjects();
                bool hasNote = false;
                double minStart = 1.0e9;
                for (const auto& o : objects)
                {
                    hasNote = hasNote || o.hasType (GraphIds::note);
                    minStart = std::min (minStart, getD (o, GraphIds::start));
                }
                const double totalDt = t - dragStartTime;
                const float rawDp = p - dragStartPitch;
                const float totalDp = (hasNote && ! e.mods.isAltDown()) ? std::round (rawDp) : rawDp;
                double dt = totalDt - appliedDt;
                if (minStart + dt < 0.0)
                    dt = -minStart;
                const float dp = totalDp - appliedDp;
                if (std::abs (dt) > 1.0e-9 || std::abs (dp) > 1.0e-6f)
                {
                    for (auto& o : objects)
                        model.moveObject (o, dt, dp);
                    appliedDt += dt;
                    appliedDp += dp;
                }
                break;
            }
            case Drag::ResizeLeft:
            {
                const double end = getD (dragObject, GraphIds::end);
                model.setBounds (dragObject, std::clamp (t, 0.0, end - kMinNoteSeconds), end);
                break;
            }
            case Drag::ResizeRight:
            {
                const double start = getD (dragObject, GraphIds::start);
                model.setBounds (dragObject, start, std::max (t, start + kMinNoteSeconds));
                break;
            }
            case Drag::LineStart:
                model.setObjectProperty (dragObject, GraphIds::start, std::clamp (t, 0.0, getD (dragObject, GraphIds::end) - 0.01));
                model.setObjectProperty (dragObject, GraphIds::pitch, snap (p, e.mods));
                break;
            case Drag::LineEnd:
                model.setObjectProperty (dragObject, GraphIds::end, std::max (t, getD (dragObject, GraphIds::start) + 0.01));
                model.setObjectProperty (dragObject, GraphIds::endPitch, snap (p, e.mods));
                break;
            case Drag::Create:
            {
                const double a = createdAnchor, b = std::max (0.0, t);
                model.setBounds (dragObject, std::min (a, b), std::max ({ a, b, std::min (a, b) + kMinNoteSeconds }));
                break;
            }
            case Drag::LineDraw:
                lineT1 = std::max (0.0, t);
                lineP1 = snap (p, e.mods);
                repaint();
                break;
            case Drag::CurveDraw:
                if (t > drawn.back().first + 0.004)
                    drawn.emplace_back (t, p);
                repaint();
                break;
            case Drag::Rubber:
            {
                rubber = juce::Rectangle<float> (e.mouseDownPosition, pos);
                selection = rubberBase;
                const double t0 = sOf (rubber.getX()), t1 = sOf (rubber.getRight());
                const float pHi = pOf (rubber.getY()), pLo = pOf (rubber.getBottom());
                for (const auto& o : model.getObjects())
                {
                    const double s = getD (o, GraphIds::start), en = getD (o, GraphIds::end);
                    float lo = getF (o, GraphIds::pitch), hi = lo;
                    if (o.hasType (GraphIds::line))
                    {
                        const float endPitch = getF (o, GraphIds::endPitch);
                        lo = std::min (lo, endPitch);
                        hi = std::max (hi, endPitch);
                    }
                    else if (o.hasType (GraphIds::curve))
                        for (const auto& pt : GraphModel::readCurvePoints (o))
                        {
                            lo = std::min (lo, pt.second);
                            hi = std::max (hi, pt.second);
                        }
                    if (en >= t0 && s <= t1 && hi + 0.5f >= pLo && lo - 0.5f <= pHi)
                        selection.insert (getId (o));
                }
                repaint();
                owner.updateInspector();
                break;
            }
            case Drag::Erase:
                eraseAt (pos);
                break;
            case Drag::TimeEdit: // handled above
            case Drag::None:
                break;
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag == Drag::Create && dragObject.isValid()
            && getD (dragObject, GraphIds::end) - getD (dragObject, GraphIds::start) < 0.05)
            model.setBounds (dragObject, createdAnchor, createdAnchor + 0.25); // a click makes a 250 ms note
        else if (drag == Drag::LineDraw && std::abs (lineT1 - lineT0) > 0.01)
        {
            auto line = model.addLine (lineT0, lineT1, lineP0, lineP1);
            selection = { getId (line) };
        }
        else if (drag == Drag::CurveDraw && drawn.size() >= 2 && drawn.back().first - drawn.front().first > 0.02)
        {
            auto curve = model.addCurve (drawn);
            if (curve.isValid())
                selection = { getId (curve) };
        }
        drag = Drag::None;
        drawn.clear();
        owner.updateInspector();
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto tool = owner.tool;
        auto newCursor = juce::MouseCursor::NormalCursor;
        if (tool == Tool::Time)
        {
            const auto hit = hitObject (e.position);
            if (hit.object.isValid() && hit.object.hasType (GraphIds::note))
                newCursor = (hit.mode == Drag::ResizeLeft || hit.mode == Drag::ResizeRight) ? juce::MouseCursor::LeftRightResizeCursor
                                                                                           : juce::MouseCursor::DraggingHandCursor;
        }
        else if (tool == Tool::Select || tool == Tool::Note || tool == Tool::Line)
        {
            const auto hit = hitObject (e.position);
            if (hit.mode == Drag::ResizeLeft || hit.mode == Drag::ResizeRight)
                newCursor = juce::MouseCursor::LeftRightResizeCursor;
            else if (hit.mode == Drag::LineStart || hit.mode == Drag::LineEnd)
                newCursor = juce::MouseCursor::CrosshairCursor;
            else if (hit.object.isValid())
                newCursor = juce::MouseCursor::DraggingHandCursor;
            else if (tool != Tool::Select)
                newCursor = juce::MouseCursor::CrosshairCursor;
        }
        else
            newCursor = juce::MouseCursor::CrosshairCursor;
        setMouseCursor (newCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float dy = w.isReversed ? -w.deltaY : w.deltaY;
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
            zoomTime (dy > 0.0f ? 0.8 : 1.25, tOf (e.position.x));
        else if (e.mods.isAltDown())
            zoomPitch (dy > 0.0f ? 0.85f : 1.18f, pOf (e.position.y));
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : dy;
            follow = false;
            owner.updateButtons();
            setViewStart (viewStart - static_cast<double> (d) * 0.3 * viewLength);
        }
        else
        {
            pitchTop = std::clamp (pitchTop + dy * 6.0f, 12.0f + pitchRange, 120.0f);
            userMovedPitch = true;
            repaint();
        }
    }

    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override
    {
        if (scaleFactor > 0.0f)
            zoomTime (1.0 / scaleFactor, tOf (e.position.x));
    }

private:
    enum class Drag { None, Move, ResizeLeft, ResizeRight, LineStart, LineEnd, Create, LineDraw, CurveDraw, Rubber, Erase, TimeEdit };

    //==============================================================================
    // Time tool: a note's start/end become anchors of the time map; the previous
    // note's start and the next note's end stay fixed, so the neighbours absorb
    // the change. Body = move the syllable, edge = stretch / compress it.
    static void ensureAnchor (dsp::TimeMap& map, double src)
    {
        for (const auto& a : map.anchors)
            if (std::abs (a.second - src) < 1.0e-6)
                return;
        const double out = map.toOutput (src);
        auto it = std::lower_bound (map.anchors.begin(), map.anchors.end(), src,
                                    [] (const std::pair<double, double>& a, double s) { return a.second < s; });
        map.anchors.insert (it, { out, src });
    }

    /** Removes identity anchors that only separate identity segments. */
    static void simplify (dsp::TimeMap& map)
    {
        auto identity = [] (const std::pair<double, double>& a) { return std::abs (a.first - a.second) < 1.0e-9; };
        for (size_t i = 0; i < map.anchors.size();)
        {
            const bool prevId = i == 0 || identity (map.anchors[i - 1]);
            const bool nextId = i + 1 >= map.anchors.size() || identity (map.anchors[i + 1]);
            if (identity (map.anchors[i]) && prevId && nextId)
                map.anchors.erase (map.anchors.begin() + static_cast<long> (i));
            else
                ++i;
        }
        if (map.isIdentity())
            map.anchors.clear();
    }

    void beginTimeEdit (const juce::ValueTree& note, Drag mode, bool reset, float mouseX)
    {
        const double start = getD (note, GraphIds::start), end = getD (note, GraphIds::end);
        double fixedLeft = std::max (0.0, start - 0.5), fixedRight = end + 0.5;
        for (const auto& o : model.getObjects())
        {
            if (! o.hasType (GraphIds::note) || o == note)
                continue;
            const double s0 = getD (o, GraphIds::start), s1 = getD (o, GraphIds::end);
            if (s0 < start && s0 > fixedLeft - 1.0e-9)
                fixedLeft = std::max (fixedLeft, s0);
            if (s1 > end && s1 < fixedRight + 1.0e-9)
                fixedRight = std::min (fixedRight, s1);
        }
        if (fixedLeft >= start - dsp::TimeMap::kMinSpacing)
            fixedLeft = std::max (0.0, start - 0.25);
        if (fixedRight <= end + dsp::TimeMap::kMinSpacing)
            fixedRight = end + 0.25;

        auto map = model.getTimeMap();
        // Edits inside this region are replaced by the new one (Alt: just removed).
        map.anchors.erase (std::remove_if (map.anchors.begin(), map.anchors.end(),
                                           [&] (const auto& a) { return a.second > fixedLeft + 1.0e-6 && a.second < fixedRight - 1.0e-6; }),
                           map.anchors.end());
        if (reset)
        {
            simplify (map);
            model.beginEdit (u8 ("Deshacer edición de tiempo"));
            model.setTimeMap (map);
            return;
        }
        ensureAnchor (map, fixedLeft);
        ensureAnchor (map, fixedRight);
        movingSources.clear();
        if (mode != Drag::ResizeRight)
            movingSources.push_back (start);
        if (mode != Drag::ResizeLeft)
            movingSources.push_back (end);
        for (double src : movingSources)
            ensureAnchor (map, src);
        timeBaseMap = map;
        timeDragStartOut = tOf (mouseX);
        drag = Drag::TimeEdit;
        model.beginEdit (u8 ("Edición de tiempo")); // the map is written on the first drag
    }

    void dragTimeEdit (double dt)
    {
        auto map = timeBaseMap;
        auto isMoving = [this] (double src) {
            return std::any_of (movingSources.begin(), movingSources.end(), [src] (double m) { return std::abs (m - src) < 1.0e-6; });
        };
        // Clamp dt so every moved anchor keeps its distance to the fixed ones.
        double lo = -1.0e9, hi = 1.0e9;
        for (size_t i = 0; i < map.anchors.size(); ++i)
        {
            if (! isMoving (map.anchors[i].second))
                continue;
            if (i > 0 && ! isMoving (map.anchors[i - 1].second))
                lo = std::max (lo, map.anchors[i - 1].first + dsp::TimeMap::kMinSpacing - map.anchors[i].first);
            if (i + 1 < map.anchors.size() && ! isMoving (map.anchors[i + 1].second))
                hi = std::min (hi, map.anchors[i + 1].first - dsp::TimeMap::kMinSpacing - map.anchors[i].first);
        }
        dt = std::clamp (dt, lo, hi);
        for (auto& a : map.anchors)
            if (isMoving (a.second))
                a.first += dt;
        simplify (map);
        model.setTimeMap (map);
    }

    dsp::TimeMap timeBaseMap;
    std::vector<double> movingSources;
    double timeDragStartOut = 0.0;

    struct Hit
    {
        juce::ValueTree object;
        Drag mode = Drag::None;
    };

    template <typename Frames>
    static auto visibleBeginImpl (const Frames& frames, double t)
    {
        return std::lower_bound (frames.begin(), frames.end(), t, [] (const CaptureFrame& f, double time) { return f.time < time; });
    }
    std::vector<CaptureFrame>::const_iterator visibleBegin (const std::vector<CaptureFrame>& frames) const
    {
        auto it = visibleBeginImpl (frames, model.getTimeMap().toSource (viewStart - 0.05));
        return it == frames.begin() ? it : it - 1;
    }

    juce::Rectangle<float> noteRect (const juce::ValueTree& o) const
    {
        const float pitch = getF (o, GraphIds::pitch);
        const float x0 = xs (getD (o, GraphIds::start)), x1 = xs (getD (o, GraphIds::end));
        return juce::Rectangle<float>::leftTopRightBottom (x0, yOf (pitch + 0.5f), std::max (x1, x0 + 2.0f), yOf (pitch - 0.5f)).reduced (0.0f, 1.0f);
    }

    static float snap (float pitch, const juce::ModifierKeys& mods) { return mods.isAltDown() ? pitch : std::round (pitch); }

    Hit hitObject (juce::Point<float> pos) const
    {
        const auto objects = model.getObjects();
        for (int i = objects.getNumChildren(); --i >= 0;) // topmost (last added) first
        {
            const auto o = objects.getChild (i);
            if (o.hasType (GraphIds::note))
            {
                const auto r = noteRect (o);
                if (! r.expanded (0.0f, 1.0f).contains (pos))
                    continue;
                if (r.getWidth() > 18.0f && pos.x < r.getX() + 6.0f)
                    return { o, Drag::ResizeLeft };
                if (r.getWidth() > 18.0f && pos.x > r.getRight() - 6.0f)
                    return { o, Drag::ResizeRight };
                return { o, Drag::Move };
            }
            if (o.hasType (GraphIds::line))
            {
                const juce::Point<float> a (xs (getD (o, GraphIds::start)), yOf (getF (o, GraphIds::pitch)));
                const juce::Point<float> b (xs (getD (o, GraphIds::end)), yOf (getF (o, GraphIds::endPitch)));
                if (pos.getDistanceFrom (a) < 7.0f)
                    return { o, Drag::LineStart };
                if (pos.getDistanceFrom (b) < 7.0f)
                    return { o, Drag::LineEnd };
                juce::Point<float> nearest;
                if (juce::Line<float> (a, b).getDistanceFromPoint (pos, nearest) < 6.0f)
                    return { o, Drag::Move };
                continue;
            }
            if (o.hasType (GraphIds::curve))
            {
                const auto pts = GraphModel::readCurvePoints (o);
                for (size_t k = 1; k < pts.size(); ++k)
                {
                    const juce::Point<float> a (xs (pts[k - 1].first), yOf (pts[k - 1].second));
                    const juce::Point<float> b (xs (pts[k].first), yOf (pts[k].second));
                    juce::Point<float> nearest;
                    if (juce::Line<float> (a, b).getDistanceFromPoint (pos, nearest) < 6.0f)
                        return { o, Drag::Move };
                }
            }
        }
        return {};
    }

    void eraseAt (juce::Point<float> pos)
    {
        if (const auto hit = hitObject (pos); hit.object.isValid())
        {
            selection.erase (getId (hit.object));
            model.remove (hit.object);
        }
    }

    GraphEditor& owner;
    GraphModel& model;

    Drag drag = Drag::None;
    juce::ValueTree dragObject;
    double dragStartTime = 0.0, appliedDt = 0.0, createdAnchor = 0.0;
    float dragStartPitch = 0.0f, appliedDp = 0.0f;
    double lineT0 = 0.0, lineT1 = 0.0;
    float lineP0 = 0.0f, lineP1 = 0.0f;
    std::vector<std::pair<double, float>> drawn;
    juce::Rectangle<float> rubber;
    std::set<juce::int64> rubberBase;
};

//==============================================================================
GraphEditor::GraphEditor (NovaPitchProcessor& p)
    : processor (p), model (p.getGraphModel()), accent (Palette::defaultAccent)
{
    setWantsKeyboardFocus (true);
    roll = std::make_unique<Roll> (*this);
    addAndMakeVisible (*roll);

    const char* toolNames[] = { "Seleccionar", "Nota", "Línea", "Curva", "Borrar", "Dividir", "Tiempo" };
    const char* toolTips[] = {
        "Seleccionar y mover (Mayús: añadir a la selección). Arrastra los bordes de una nota para cambiar su duración. "
        "Las notas se mueven por semitonos; con Alt, libremente. Clic derecho: borrar.",
        "Dibuja notas: arrastra en el tiempo; un clic crea una nota de 250 ms.",
        "Dibuja una línea recta de afinación (deslizamientos entre notas). Alt: sin ajustar a semitonos.",
        "Dibuja a mano alzada la curva de afinación exacta.",
        "Borra los objetos que toques o arrastres.",
        "Divide una nota en el punto donde hagas clic.",
        "Corrección de tiempo (no destructiva): arrastra una nota para mover la sílaba o sus bordes para estirarla; "
        "las notas vecinas se ajustan. Alt+clic en una nota deshace su edición. Requiere la captura de audio." };
    for (int i = 0; i < kNumTools; ++i)
    {
        auto& b = toolButtons[static_cast<size_t> (i)];
        b.setButtonText (u8 (toolNames[i]));
        b.setTooltip (u8 (toolTips[i]));
        b.setRadioGroupId (3001);
        b.setClickingTogglesState (true);
        styleButton (b);
        b.onClick = [this, i] {
            if (toolButtons[static_cast<size_t> (i)].getToggleState())
                tool = static_cast<Tool> (i);
        };
        addAndMakeVisible (b);
    }
    toolButtons[0].setToggleState (true, juce::dontSendNotification);

    mergeButton.setButtonText (u8 ("Unir"));
    mergeButton.setTooltip (u8 ("Une las notas seleccionadas en una sola."));
    mergeButton.onClick = [this] { mergeSelection(); };
    undoButton.setButtonText (u8 ("Deshacer"));
    undoButton.setTooltip (u8 ("Deshacer (Ctrl/Cmd+Z)"));
    undoButton.onClick = [this] { model.getUndoManager().undo(); };
    redoButton.setButtonText (u8 ("Rehacer"));
    redoButton.setTooltip (u8 ("Rehacer (Ctrl/Cmd+Mayús+Z)"));
    redoButton.onClick = [this] { model.getUndoManager().redo(); };

    captureButton.setButtonText (u8 ("● Capturar"));
    captureButton.setTooltip (u8 ("Mientras está activo, se registran la afinación y el audio de la voz cuando FL Studio "
                                  "reproduce (el audio se guarda en tu carpeta de usuario, no en el proyecto). Volver a "
                                  "capturar un tramo reemplaza solo ese tramo."));
    captureButton.setClickingTogglesState (true);
    captureButton.onClick = [this] { processor.setGraphCapture (captureButton.getToggleState()); };
    importButton.setButtonText (u8 ("Importar desde automático"));
    importButton.setTooltip (u8 ("Crea notas a partir de la curva capturada con la tonalidad, escala y notas actuales. "
                                 "Reemplaza los objetos existentes (se puede deshacer)."));
    importButton.onClick = [this] {
        if (model.getCaptured().empty())
        {
            infoText = u8 ("Primero captura: pulsa «Capturar» y reproduce.");
            repaint();
            return;
        }
        juce::PopupMenu menu;
        menu.addSectionHeader (u8 ("Crear objetos desde la captura (se puede deshacer)"));
        menu.addItem (2, u8 ("Notas — detalle normal"));
        menu.addItem (3, u8 ("Notas — más detalle (pasajes rápidos, adornos)"));
        menu.addItem (1, u8 ("Notas — menos detalle (notas largas)"));
        menu.addSeparator();
        menu.addItem (4, u8 ("Curvas — siguen la afinación capturada (para editar la forma)"));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (importButton), [this] (int result) {
            if (result >= 1 && result <= 3)
                processor.importGraphFromAuto (result - 1);
            else if (result == 4)
                processor.importGraphAsCurves();
            else
                return;
            roll->selection.clear();
            roll->fitAll();
        });
    };
    clearButton.setButtonText (u8 ("Limpiar…"));
    clearButton.setTooltip (u8 ("Borrar objetos y/o la curva capturada."));
    clearButton.onClick = [this] { showClearMenu(); };
    followButton.setButtonText (u8 ("Seguir"));
    followButton.setTooltip (u8 ("La vista sigue a la posición de reproducción de FL Studio."));
    followButton.setClickingTogglesState (true);
    followButton.setToggleState (true, juce::dontSendNotification);
    followButton.onClick = [this] { roll->follow = followButton.getToggleState(); };
    zoomOut.setButtonText (u8 ("−"));
    zoomOut.setTooltip (u8 ("Alejar (Ctrl/Cmd + rueda)"));
    zoomOut.onClick = [this] { roll->zoomTime (2.0, roll->viewStart + 0.5 * roll->viewLength); };
    zoomIn.setButtonText ("+");
    zoomIn.setTooltip (u8 ("Acercar (Ctrl/Cmd + rueda)"));
    zoomIn.onClick = [this] { roll->zoomTime (0.5, roll->viewStart + 0.5 * roll->viewLength); };
    fitButton.setButtonText (u8 ("Ajustar"));
    fitButton.setTooltip (u8 ("Muestra toda la captura y los objetos. Rueda: tono · Mayús+rueda: tiempo · Alt+rueda: zoom de tono."));
    fitButton.onClick = [this] { roll->fitAll(); };

    for (auto* b : { &mergeButton, &undoButton, &redoButton, &captureButton, &importButton, &clearButton, &followButton,
                     &zoomOut, &zoomIn, &fitButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    captureButton.setColour (juce::TextButton::buttonOnColourId, kCaptureRed);
    captureButton.setColour (juce::TextButton::textColourOnId, Palette::textPrimary);

    // Per-note inspector
    auto setupSlider = [this] (juce::Slider& s, double lo, double hi, double step, const juce::Identifier& property,
                               std::function<juce::String (double)> text, const char* tip) {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        s.setRange (lo, hi, step);
        s.textFromValueFunction = std::move (text);
        s.updateText();
        s.setTooltip (u8 (tip));
        s.setColour (juce::Slider::trackColourId, Palette::panelOutline);
        s.setColour (juce::Slider::backgroundColourId, Palette::background);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.onDragStart = [this] { model.beginEdit (u8 ("Ajuste de nota")); };
        s.onValueChange = [this, &s, property] {
            if (! updatingInspector)
                applyInspector (property, s.getValue());
        };
        addAndMakeVisible (s);
    };
    setupSlider (retuneSlider, -1.0, 400.0, 1.0, GraphIds::retuneMs,
                 [] (double v) { return v < 0.0 ? u8 ("Global") : juce::String (juce::roundToInt (v)) + " ms"; },
                 "Velocidad de corrección de las notas seleccionadas. «Global» usa la Velocidad de la pestaña Corrección.");
    setupSlider (vibratoSlider, -1.0, 200.0, 1.0, GraphIds::vibratoKeep,
                 [] (double v) { return v < 0.0 ? u8 ("Global") : juce::String (juce::roundToInt (v)) + " %"; },
                 "Vibrato conservado en las notas o curvas seleccionadas (0 % lo elimina, 200 % lo exagera). «Global» usa Conservar vibrato (en las curvas, la forma dibujada).");
    setupSlider (formantSlider, -12.0, 12.0, 0.1, GraphIds::formantSt,
                 [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " st"; },
                 "Desplazamiento de formantes adicional en las notas seleccionadas.");

    updateButtons();
    updateInspector();
}

GraphEditor::~GraphEditor() = default;

void GraphEditor::setAccent (juce::Colour c)
{
    accent = c;
    repaint();
}

void GraphEditor::setTool (Tool t)
{
    tool = t;
    toolButtons[static_cast<size_t> (t)].setToggleState (true, juce::dontSendNotification);
}

void GraphEditor::updateButtons()
{
    undoButton.setEnabled (model.getUndoManager().canUndo());
    redoButton.setEnabled (model.getUndoManager().canRedo());
    followButton.setToggleState (roll->follow, juce::dontSendNotification);
    captureButton.setToggleState (processor.isGraphCapturing(), juce::dontSendNotification);
    int notes = 0;
    for (const auto& o : roll->selectedObjects())
        notes += o.hasType (GraphIds::note) ? 1 : 0;
    mergeButton.setEnabled (notes >= 2);
}

void GraphEditor::updateInspector()
{
    const juce::ScopedValueSetter<bool> guard (updatingInspector, true);
    juce::ValueTree first, firstCurve;
    for (const auto& o : roll->selectedObjects())
    {
        if (o.hasType (GraphIds::note) && ! first.isValid())
            first = o;
        if (o.hasType (GraphIds::curve) && ! firstCurve.isValid())
            firstCurve = o;
    }
    const bool any = first.isValid();
    retuneSlider.setEnabled (any);
    formantSlider.setEnabled (any);
    vibratoSlider.setEnabled (any || firstCurve.isValid()); // curves: scales their own vibrato
    retuneSlider.setValue (any ? getF (first, GraphIds::retuneMs, -1.0f) : -1.0, juce::dontSendNotification);
    const auto& vibSource = any ? first : firstCurve;
    const float vib = vibSource.isValid() ? getF (vibSource, GraphIds::vibratoKeep, -1.0f) : -1.0f;
    vibratoSlider.setValue (vib < 0.0f ? -1.0 : vib * 100.0, juce::dontSendNotification);
    formantSlider.setValue (any ? getF (first, GraphIds::formantSt) : 0.0, juce::dontSendNotification);
    updateButtons();
}

void GraphEditor::applyInspector (const juce::Identifier& property, double value)
{
    float stored = static_cast<float> (value);
    if (property == GraphIds::vibratoKeep)
        stored = value < 0.0 ? -1.0f : static_cast<float> (value / 100.0);
    for (auto& o : roll->selectedObjects())
        if (o.hasType (GraphIds::note) || (o.hasType (GraphIds::curve) && property == GraphIds::vibratoKeep))
            model.setObjectProperty (o, property, stored);
}

void GraphEditor::mergeSelection()
{
    std::vector<juce::ValueTree> notes;
    for (const auto& o : roll->selectedObjects())
        if (o.hasType (GraphIds::note))
            notes.push_back (o);
    if (notes.size() < 2)
        return;
    std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return getD (a, GraphIds::start) < getD (b, GraphIds::start); });
    model.beginEdit (u8 ("Unir"));
    for (size_t i = 1; i < notes.size(); ++i)
        model.merge (notes.front(), notes[i]);
    roll->selection = { getId (notes.front()) };
}

void GraphEditor::deleteSelection()
{
    auto objects = roll->selectedObjects();
    if (objects.empty())
        return;
    model.beginEdit (u8 ("Borrar"));
    for (auto& o : objects)
        model.remove (o);
    roll->selection.clear();
}

void GraphEditor::showClearMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, u8 ("Borrar todos los objetos"), model.getObjects().getNumChildren() > 0);
    menu.addItem (2, u8 ("Borrar la curva capturada"), ! model.getCaptured().empty());
    menu.addItem (3, u8 ("Borrar todo"));
    menu.addSeparator();
    menu.addItem (4, u8 ("Quitar ediciones de tiempo"), ! model.getTimeMap().isIdentity());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (clearButton), [this] (int result) {
        if (result == 1 || result == 3)
        {
            model.beginEdit (u8 ("Borrar objetos"));
            model.clearObjects();
            roll->selection.clear();
        }
        if (result == 2 || result == 3)
            processor.clearGraphCapture(); // pitch curve + captured audio files
        if (result == 3 || result == 4)
        {
            model.beginEdit (u8 ("Quitar ediciones de tiempo"));
            model.clearTimeEdits();
        }
    });
}

juce::String GraphEditor::buildInfoText() const
{
    double captured = 0.0;
    const auto& frames = model.getCaptured();
    for (size_t i = 1; i < frames.size(); ++i)
        if (frames[i].time - frames[i - 1].time < 0.1)
            captured += frames[i].time - frames[i - 1].time;
    juce::String text = u8 ("Captura ") + formatTime (captured, 1.0) + u8 (" · ")
                        + juce::String (model.getObjects().getNumChildren()) + u8 (" objetos");
    if (processor.isGraphCapturing())
        text = (processor.isHostPlaying() ? u8 ("Capturando… · ") : u8 ("Esperando reproducción · ")) + text;
    else if (processor.isTimeAudioMissing())
        text = u8 ("Falta el audio capturado: vuelve a capturar para oír las ediciones de tiempo · ") + text;
    else if (! model.getTimeMap().isIdentity() && processor.isRenderingTime())
        text = u8 ("Renderizando tiempo… · ") + text;
    return text;
}

void GraphEditor::updatePrediction()
{
    auto& state = processor.getValueTreeState();
    const bool vintage = juce::roundToInt (state.getRawParameterValue ("qualityMode")->load()) == 3;
    predictedRetune = vintage ? 0.0f : state.getRawParameterValue ("retuneSpeed")->load();
    predictedKeep = vintage ? 0.0f : state.getRawParameterValue ("vibratoPreserve")->load() / 100.0f;

    // Same idea as the audio path: correction acts on a vibrato-free centre
    // (here a centred +-90 ms median: removes a vibrato cycle, keeps note steps
    // sharp), smoothed by the retune speed; the oscillation around the centre
    // is put back scaled by "keep".
    const auto& frames = model.getCaptured();
    const size_t n = frames.size();
    predicted.assign (n, -1.0f);
    const auto track = model.compile();
    std::vector<float> centre (n, -1.0f), window;
    for (size_t i = 0, lo = 0, hi = 0; i < n; ++i)
    {
        if (frames[i].midi < 0.0f)
            continue;
        while (frames[lo].time < frames[i].time - 0.09)
            ++lo;
        hi = std::max (hi, i);
        while (hi + 1 < n && frames[hi + 1].time <= frames[i].time + 0.09)
            ++hi;
        window.clear();
        for (size_t k = lo; k <= hi; ++k)
            if (frames[k].midi >= 0.0f)
                window.push_back (frames[k].midi);
        const auto mid = window.begin() + static_cast<long> (window.size() / 2);
        std::nth_element (window.begin(), mid, window.end());
        centre[i] = *mid;
    }

    float correction = 0.0f;
    double lastTime = -1.0e9;
    bool active = false;
    for (size_t i = 0; i < n; ++i)
    {
        const auto& f = frames[i];
        dsp::CorrectionTrack::Result r;
        if (f.midi < 0.0f || ! track->lookup (model.getTimeMap().toOutput (f.time), r)) // the track is in output time
        {
            active = false;
            continue;
        }
        const bool exact = r.kind != dsp::CorrectionTrack::Kind::Note;
        const float tau = exact ? 0.0f : (r.retuneMs >= 0.0f ? r.retuneMs : predictedRetune);
        const float keep = exact ? 0.0f : (r.vibratoKeep >= 0.0f ? r.vibratoKeep : predictedKeep);
        const float error = r.pitch - centre[i];
        const double dt = active ? std::max (0.0, f.time - lastTime) : 0.0;
        if (! active || tau < 0.5f)
            correction = error;
        else
            correction += static_cast<float> (1.0 - std::exp (-dt * 1000.0 / tau)) * (error - correction);
        // The +-0.8 st clamp keeps note transitions (where the centre lags) from drawing spikes.
        predicted[i] = centre[i] + correction + keep * std::clamp (f.midi - centre[i], -0.8f, 0.8f);
        active = true;
        lastTime = f.time;
    }
}

void GraphEditor::refresh()
{
    const int generation = model.getGeneration();
    bool changed = generation != shownGeneration;
    {
        auto& state = processor.getValueTreeState();
        const bool vintage = juce::roundToInt (state.getRawParameterValue ("qualityMode")->load()) == 3;
        const float retune = vintage ? 0.0f : state.getRawParameterValue ("retuneSpeed")->load();
        const float keep = vintage ? 0.0f : state.getRawParameterValue ("vibratoPreserve")->load() / 100.0f;
        if (changed || std::abs (retune - predictedRetune) > 1.0e-3f || std::abs (keep - predictedKeep) > 1.0e-4f)
        {
            updatePrediction();
            changed = true;
        }
    }
    if (changed)
    {
        const bool hadData = shownGeneration >= 0;
        shownGeneration = generation;
        roll->validateSelection();
        updateInspector();
        if (! roll->userMovedPitch && (! model.getCaptured().empty() || ! model.getPendingCapture().empty()))
        {
            if (! hadData)
                roll->fitAll (8.0);
            else
                roll->fitPitch();
        }
    }
    updateButtons();

    const bool playing = processor.isHostPlaying();
    const double t = processor.getHostTimeSeconds();
    if (playing && roll->follow && roll->ensureVisible (t))
        roll->shownPlayhead = -1.0;
    if (changed)
        roll->repaint();
    roll->updatePlayhead (t, playing);

    auto newInfo = buildInfoText();
    if (newInfo != infoText && ! (infoText.startsWith (u8 ("Primero")) && ! changed))
    {
        infoText = std::move (newInfo);
        repaint (getLocalBounds().withHeight (64));
    }
}

void GraphEditor::resized()
{
    auto row1 = juce::Rectangle<int> (0, 0, getWidth(), 28);
    for (size_t i = 0; i < toolButtons.size(); ++i)
        toolButtons[i].setBounds (row1.removeFromLeft (i == 0 ? 86 : 70).reduced (2, 0));
    row1.removeFromLeft (8);
    mergeButton.setBounds (row1.removeFromLeft (58).reduced (2, 0));
    redoButton.setBounds (row1.removeFromRight (66).reduced (2, 0));
    undoButton.setBounds (row1.removeFromRight (70).reduced (2, 0));

    auto row2 = juce::Rectangle<int> (0, 32, getWidth(), 28);
    captureButton.setBounds (row2.removeFromLeft (96).reduced (2, 0));
    importButton.setBounds (row2.removeFromLeft (176).reduced (2, 0));
    clearButton.setBounds (row2.removeFromLeft (76).reduced (2, 0));
    row2.removeFromLeft (10);
    followButton.setBounds (row2.removeFromLeft (64).reduced (2, 0));
    zoomOut.setBounds (row2.removeFromLeft (30).reduced (2, 0));
    zoomIn.setBounds (row2.removeFromLeft (30).reduced (2, 0));
    fitButton.setBounds (row2.removeFromLeft (64).reduced (2, 0));

    roll->setBounds (0, 66, getWidth(), getHeight() - 66 - 50);

    auto inspector = juce::Rectangle<int> (0, getHeight() - 44, getWidth(), 44).reduced (12, 10);
    inspector.removeFromLeft (118); // caption
    const int w = inspector.getWidth() / 3;
    for (auto* s : { &retuneSlider, &vibratoSlider, &formantSlider })
        s->setBounds (inspector.removeFromLeft (w).withTrimmedLeft (66).reduced (2, 0));
}

void GraphEditor::paint (juce::Graphics& g)
{
    // Info text (right of the second toolbar row)
    auto info = juce::Rectangle<float> (static_cast<float> (fitButton.getRight() + 10), 32.0f,
                                        static_cast<float> (getWidth() - fitButton.getRight() - 12), 28.0f);
    g.setColour (processor.isGraphCapturing() ? kCaptureRed.brighter (0.3f) : Palette::textMuted);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (infoText, info.toNearestInt(), juce::Justification::centredRight, 2, 0.9f);

    // Inspector panel
    const auto panel = juce::Rectangle<float> (0.0f, static_cast<float> (getHeight() - 44), static_cast<float> (getWidth()), 44.0f);
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (Palette::panelOutline);
    g.drawRoundedRectangle (panel.reduced (0.5f), 8.0f, 1.0f);

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (Palette::textMuted);
    g.drawFittedText (u8 ("NOTAS\nSELECCIONADAS"), panel.withWidth (118.0f).reduced (12.0f, 6.0f).toNearestInt(),
                      juce::Justification::centredLeft, 2, 1.0f);
    g.setFont (juce::FontOptions (11.0f));
    const char* labels[] = { "Velocidad", "Vibrato", "Formante" };
    juce::Slider* sliders[] = { &retuneSlider, &vibratoSlider, &formantSlider };
    for (int i = 0; i < 3; ++i)
    {
        const auto b = sliders[i]->getBounds();
        g.setColour (sliders[i]->isEnabled() ? Palette::textPrimary.withAlpha (0.85f) : Palette::textMuted.withAlpha (0.6f));
        g.drawText (u8 (labels[i]), juce::Rectangle<int> (b.getX() - 64, b.getY(), 60, b.getHeight()), juce::Justification::centredRight);
    }
}

bool GraphEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }
    if (mods.isCommandDown() && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown())
            model.getUndoManager().redo();
        else
            model.getUndoManager().undo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'Y' || code == 'y'))
    {
        model.getUndoManager().redo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'A' || code == 'a'))
    {
        roll->selectAll();
        return true;
    }
    // Arrows nudge the selection: up/down a semitone (Shift: 10 cents),
    // left/right 10 ms (Shift: 100 ms).
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey
        || code == juce::KeyPress::rightKey)
    {
        auto objects = roll->selectedObjects();
        if (objects.empty())
            return false;
        const float dp = code == juce::KeyPress::upKey ? 1.0f : (code == juce::KeyPress::downKey ? -1.0f : 0.0f);
        const double dt = code == juce::KeyPress::rightKey ? 0.01 : (code == juce::KeyPress::leftKey ? -0.01 : 0.0);
        const float pitchStep = mods.isShiftDown() ? 0.1f : 1.0f;
        const double timeStep = mods.isShiftDown() ? 10.0 : 1.0;
        model.beginEdit (u8 ("Mover"));
        for (auto& o : objects)
            model.moveObject (o, std::max (-getD (o, GraphIds::start), dt * timeStep), dp * pitchStep);
        return true;
    }
    if (code == juce::KeyPress::escapeKey)
    {
        roll->selection.clear();
        updateInspector();
        roll->repaint();
        return true;
    }
    if (! mods.isAnyModifierKeyDown() && code >= '1' && code < '1' + kNumTools)
    {
        setTool (static_cast<Tool> (code - '1'));
        return true;
    }
    return false;
}

} // namespace novapitch
