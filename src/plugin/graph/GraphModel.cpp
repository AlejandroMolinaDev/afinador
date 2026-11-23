#include "plugin/graph/GraphModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace novapitch
{

namespace
{
const auto& kNote = GraphIds::note;
const auto& kLine = GraphIds::line;
const auto& kCurve = GraphIds::curve;
const auto& kId = GraphIds::id;
const auto& kStart = GraphIds::start;
const auto& kEnd = GraphIds::end;
const auto& kPitch = GraphIds::pitch;
const auto& kEndPitch = GraphIds::endPitch;
const auto& kPoints = GraphIds::points;
const auto& kCaptured = GraphIds::captured;
const auto& kRetune = GraphIds::retuneMs;
const auto& kVibrato = GraphIds::vibratoKeep;
const auto& kFormant = GraphIds::formantSt;

double getD (const juce::ValueTree& t, const juce::Identifier& id, double def = 0.0) { return t.getProperty (id, def); }
float getF (const juce::ValueTree& t, const juce::Identifier& id, float def = 0.0f)
{
    return static_cast<float> (static_cast<double> (t.getProperty (id, def)));
}

std::vector<std::pair<double, float>> readPoints (const juce::ValueTree& t)
{
    std::vector<std::pair<double, float>> pts;
    if (const auto* mb = t.getProperty (kPoints).getBinaryData())
    {
        const size_t n = mb->getSize() / (sizeof (double) + sizeof (float));
        const auto* data = static_cast<const char*> (mb->getData());
        for (size_t i = 0; i < n; ++i)
        {
            double time;
            float pitch;
            std::memcpy (&time, data + i * 12, sizeof (double));
            std::memcpy (&pitch, data + i * 12 + 8, sizeof (float));
            pts.emplace_back (time, pitch);
        }
    }
    return pts;
}

juce::MemoryBlock writePoints (const std::vector<std::pair<double, float>>& pts)
{
    juce::MemoryBlock mb;
    for (const auto& [time, pitch] : pts)
    {
        mb.append (&time, sizeof (double));
        mb.append (&pitch, sizeof (float));
    }
    return mb;
}
} // namespace

GraphModel::GraphModel (juce::UndoManager& u) : undo (u) {}

std::vector<std::pair<double, float>> GraphModel::readCurvePoints (const juce::ValueTree& curve)
{
    return readPoints (curve);
}

GraphModel::~GraphModel()
{
    if (graph.isValid())
        graph.removeListener (this);
}

void GraphModel::bind (juce::ValueTree pluginState)
{
    if (graph.isValid())
        graph.removeListener (this);

    graph = pluginState.getChildWithName (kTag);
    if (! graph.isValid())
    {
        graph = juce::ValueTree (kTag);
        pluginState.appendChild (graph, nullptr);
    }

    nextId = 1;
    for (const auto& child : graph)
        nextId = std::max (nextId, static_cast<juce::int64> (child.getProperty (kId, 0)) + 1);

    pending.clear();
    loadCaptured();
    graph.addListener (this);
    undo.clearUndoHistory();
    changed();
}

void GraphModel::changed()
{
    ++generation;
    if (! loading && onChanged)
        onChanged();
}

//==============================================================================
void GraphModel::appendCaptured (const CaptureFrame* frames, int count)
{
    pending.insert (pending.end(), frames, frames + count);
    ++generation;
}

void GraphModel::commitCapture()
{
    if (pending.empty())
        return;

    std::sort (pending.begin(), pending.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });
    // The new pass owns its time range (plus half a frame on each side).
    const double lo = pending.front().time - 0.5 * kFrameSeconds, hi = pending.back().time + 0.5 * kFrameSeconds;
    captured.erase (std::remove_if (captured.begin(), captured.end(),
                                    [lo, hi] (const CaptureFrame& f) { return f.time >= lo && f.time <= hi; }),
                    captured.end());
    captured.insert (captured.end(), pending.begin(), pending.end());
    std::sort (captured.begin(), captured.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });
    pending.clear();
    storeCaptured();
}

void GraphModel::clearCaptured()
{
    captured.clear();
    pending.clear();
    storeCaptured();
}

void GraphModel::storeCaptured()
{
    if (! graph.isValid())
        return;
    juce::MemoryBlock mb (captured.data(), captured.size() * sizeof (CaptureFrame));
    loading = true; // the curve is data, not an undoable edit
    graph.setProperty (kCaptured, mb, nullptr);
    loading = false;
    ++generation;
    if (onCaptureChanged)
        onCaptureChanged();
}

void GraphModel::loadCaptured()
{
    captured.clear();
    if (const auto* mb = graph.getProperty (kCaptured).getBinaryData())
    {
        captured.resize (mb->getSize() / sizeof (CaptureFrame));
        std::memcpy (captured.data(), mb->getData(), captured.size() * sizeof (CaptureFrame));
    }
}

//==============================================================================
juce::ValueTree GraphModel::findById (juce::int64 id) const
{
    for (const auto& child : graph)
        if (static_cast<juce::int64> (child.getProperty (kId, -1)) == id)
            return child;
    return {};
}

juce::ValueTree GraphModel::makeObject (const juce::Identifier& type, double start, double end)
{
    juce::ValueTree t (type);
    t.setProperty (kId, nextId++, nullptr);
    t.setProperty (kStart, std::min (start, end), nullptr);
    t.setProperty (kEnd, std::max (start, end), nullptr);
    return t;
}

juce::ValueTree GraphModel::addNote (double start, double end, float pitch)
{
    auto t = makeObject (kNote, start, end);
    t.setProperty (kPitch, pitch, nullptr);
    t.setProperty (kRetune, -1.0f, nullptr);
    t.setProperty (kVibrato, -1.0f, nullptr);
    t.setProperty (kFormant, 0.0f, nullptr);
    graph.appendChild (t, &undo);
    return t;
}

juce::ValueTree GraphModel::addLine (double start, double end, float pitch, float endPitch)
{
    if (end < start)
        std::swap (pitch, endPitch);
    auto t = makeObject (kLine, start, end);
    t.setProperty (kPitch, pitch, nullptr);
    t.setProperty (kEndPitch, endPitch, nullptr);
    graph.appendChild (t, &undo);
    return t;
}

juce::ValueTree GraphModel::addCurve (const std::vector<std::pair<double, float>>& rawPoints)
{
    auto pts = rawPoints;
    std::sort (pts.begin(), pts.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    if (pts.size() < 2)
        return {};
    auto t = makeObject (kCurve, pts.front().first, pts.back().first);
    t.setProperty (kPoints, writePoints (pts), nullptr);
    graph.appendChild (t, &undo);
    return t;
}

void GraphModel::moveObject (juce::ValueTree t, double dt, float dp)
{
    if (! t.isValid())
        return;
    t.setProperty (kStart, getD (t, kStart) + dt, &undo);
    t.setProperty (kEnd, getD (t, kEnd) + dt, &undo);
    if (t.hasType (kCurve))
    {
        auto pts = readPoints (t);
        for (auto& p : pts)
        {
            p.first += dt;
            p.second += dp;
        }
        t.setProperty (kPoints, writePoints (pts), &undo);
    }
    else
    {
        t.setProperty (kPitch, getF (t, kPitch) + dp, &undo);
        if (t.hasType (kLine))
            t.setProperty (kEndPitch, getF (t, kEndPitch) + dp, &undo);
    }
}

void GraphModel::setBounds (juce::ValueTree t, double start, double end)
{
    if (! t.isValid() || t.hasType (kCurve))
        return;
    t.setProperty (kStart, std::min (start, end), &undo);
    t.setProperty (kEnd, std::max (start, end), &undo);
}

void GraphModel::setObjectProperty (juce::ValueTree t, const juce::Identifier& id, const juce::var& value)
{
    if (t.isValid())
        t.setProperty (id, value, &undo);
}

bool GraphModel::split (juce::ValueTree t, double at)
{
    if (! t.isValid() || ! t.hasType (kNote))
        return false;
    const double start = getD (t, kStart), end = getD (t, kEnd);
    if (at <= start + 0.01 || at >= end - 0.01)
        return false;

    auto second = makeObject (kNote, at, end);
    for (const auto* id : { &kPitch, &kRetune, &kVibrato, &kFormant })
        second.setProperty (*id, t.getProperty (*id), nullptr);
    t.setProperty (kEnd, at, &undo);
    graph.appendChild (second, &undo);
    return true;
}

bool GraphModel::merge (juce::ValueTree a, juce::ValueTree b)
{
    if (! a.isValid() || ! b.isValid() || a == b || ! a.hasType (kNote) || ! b.hasType (kNote))
        return false;
    if (getD (b, kStart) < getD (a, kStart))
        std::swap (a, b);
    a.setProperty (kEnd, std::max (getD (a, kEnd), getD (b, kEnd)), &undo);
    graph.removeChild (b, &undo);
    return true;
}

void GraphModel::remove (juce::ValueTree t)
{
    if (t.isValid())
        graph.removeChild (t, &undo);
}

void GraphModel::clearObjects()
{
    for (int i = graph.getNumChildren(); --i >= 0;)
        graph.removeChild (i, &undo);
}

int GraphModel::importFromAuto (dsp::NoteMapper mapper, double minNoteSeconds)
{
    undo.beginNewTransaction (juce::String::fromUTF8 ("Importar desde automático"));
    clearObjects();

    // Centred moving average (+-40 ms of voiced frames) so vibrato and jitter
    // don't split notes; the mapper's hysteresis does the rest.
    constexpr double kSmooth = 0.04, kMaxGap = 0.05;
    const size_t n = captured.size();
    std::vector<float> smooth (n, -1.0f);
    for (size_t i = 0, lo = 0, hi = 0; i < n; ++i)
    {
        if (captured[i].midi < 0.0f)
            continue;
        while (captured[lo].time < captured[i].time - kSmooth)
            ++lo;
        hi = std::max (hi, i);
        while (hi + 1 < n && captured[hi + 1].time <= captured[i].time + kSmooth)
            ++hi;
        double sum = 0.0;
        int count = 0;
        for (size_t k = lo; k <= hi; ++k)
            if (captured[k].midi >= 0.0f)
            {
                sum += captured[k].midi;
                ++count;
            }
        smooth[i] = static_cast<float> (sum / std::max (1, count));
    }

    mapper.reset();
    int created = 0;
    int runNote = -1;
    double runStart = 0.0, runEnd = 0.0, lastTime = -1.0e9;

    auto flush = [&] {
        if (runNote >= 0 && runEnd - runStart >= minNoteSeconds)
        {
            addNote (runStart, runEnd, static_cast<float> (runNote));
            ++created;
        }
        runNote = -1;
    };

    for (size_t i = 0; i < n; ++i)
    {
        const auto& f = captured[i];
        if (smooth[i] < 0.0f || f.time - lastTime > kMaxGap)
        {
            flush();
            mapper.reset();
        }
        lastTime = f.time;
        if (smooth[i] < 0.0f)
            continue;

        const auto target = mapper.map (smooth[i]);
        const int note = target.hasTarget && target.correct ? target.note : -1; // "Ignore" notes stay free
        if (note != runNote)
        {
            flush();
            runNote = note;
            runStart = f.time;
        }
        runEnd = f.time + kFrameSeconds;
    }
    flush();
    return created;
}

//==============================================================================
std::unique_ptr<dsp::CorrectionTrack> GraphModel::compile() const
{
    auto track = std::make_unique<dsp::CorrectionTrack>();
    if (! graph.isValid())
        return track;

    for (const auto& t : graph)
    {
        dsp::CorrectionTrack::Segment s;
        s.start = getD (t, kStart);
        s.end = getD (t, kEnd);
        if (t.hasType (kNote))
        {
            s.kind = dsp::CorrectionTrack::Kind::Note;
            s.startPitch = s.endPitch = getF (t, kPitch);
            s.retuneMs = getF (t, kRetune, -1.0f);
            s.vibratoKeep = getF (t, kVibrato, -1.0f);
            s.formantSt = getF (t, kFormant);
        }
        else if (t.hasType (kLine))
        {
            s.kind = dsp::CorrectionTrack::Kind::Line;
            s.startPitch = getF (t, kPitch);
            s.endPitch = getF (t, kEndPitch);
            s.retuneMs = 0.0f;    // drawn lines are followed exactly:
            s.vibratoKeep = 0.0f; // the drawn pitch is the output pitch
        }
        else if (t.hasType (kCurve))
        {
            const auto pts = readPoints (t);
            if (pts.size() < 2)
                continue;
            s.kind = dsp::CorrectionTrack::Kind::Curve;
            s.curveOffset = static_cast<int> (track->curvePoints.size());
            s.curveCount = static_cast<int> (pts.size());
            s.retuneMs = 0.0f;
            s.vibratoKeep = 0.0f;
            track->curvePoints.insert (track->curvePoints.end(), pts.begin(), pts.end());
        }
        else
            continue;
        if (s.end > s.start)
            track->segments.push_back (s);
    }

    std::sort (track->segments.begin(), track->segments.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
    for (size_t i = 0; i + 1 < track->segments.size(); ++i) // later objects win where they overlap
        track->segments[i].end = std::min (track->segments[i].end, track->segments[i + 1].start);
    return track;
}

} // namespace novapitch
