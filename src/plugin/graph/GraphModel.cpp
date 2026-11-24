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

    audioTree = pluginState.getChildWithName (GraphIds::audio);
    if (! audioTree.isValid())
    {
        audioTree = juce::ValueTree (GraphIds::audio);
        pluginState.appendChild (audioTree, nullptr);
    }
    ++audioGeneration;

    pending.clear();
    loadCaptured();
    loadTimeMap();
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

void GraphModel::loadTimeMap()
{
    timeMap.anchors.clear();
    if (const auto* mb = graph.getProperty (GraphIds::warp).getBinaryData())
    {
        const size_t n = mb->getSize() / (2 * sizeof (double));
        const auto* d = static_cast<const char*> (mb->getData());
        for (size_t i = 0; i < n; ++i)
        {
            double out, src;
            std::memcpy (&out, d + i * 16, sizeof (double));
            std::memcpy (&src, d + i * 16 + 8, sizeof (double));
            timeMap.anchors.emplace_back (out, src);
        }
    }
    if (! timeMap.isValid())
        timeMap.anchors.clear();
    ++timeMapGeneration;
}

void GraphModel::setTimeMap (const dsp::TimeMap& map)
{
    if (! graph.isValid() || ! map.isValid())
        return;
    juce::MemoryBlock mb;
    for (const auto& [out, src] : map.anchors)
    {
        mb.append (&out, sizeof (double));
        mb.append (&src, sizeof (double));
    }
    graph.setProperty (GraphIds::warp, mb, &undo); // listener reloads the map
}

void GraphModel::clearTimeEdits()
{
    if (graph.isValid() && graph.hasProperty (GraphIds::warp))
        graph.removeProperty (GraphIds::warp, &undo);
}

juce::String GraphModel::getAudioId (bool create)
{
    auto id = audioTree.getProperty (GraphIds::audioId).toString();
    if (id.isEmpty() && create && audioTree.isValid())
    {
        id = juce::Uuid().toDashedString();
        audioTree.setProperty (GraphIds::audioId, id, nullptr);
        ++audioGeneration;
    }
    return id;
}

std::vector<AudioPass> GraphModel::getAudioPasses() const
{
    std::vector<AudioPass> out;
    for (const auto& p : audioTree)
    {
        AudioPass a;
        a.fileName = p.getProperty (GraphIds::file).toString();
        a.start = static_cast<juce::int64> (p.getProperty (GraphIds::start, 0));
        a.length = static_cast<juce::int64> (p.getProperty (GraphIds::length, 0));
        a.sampleRate = p.getProperty (GraphIds::rate, 48000.0);
        a.channels = p.getProperty (GraphIds::channels, 1);
        if (a.fileName.isNotEmpty() && a.length > 0 && a.sampleRate > 0.0)
            out.push_back (a);
    }
    return out;
}

void GraphModel::addAudioPasses (const std::vector<AudioPass>& passes)
{
    if (! audioTree.isValid())
        return;
    for (const auto& a : passes)
    {
        juce::ValueTree p (GraphIds::pass);
        p.setProperty (GraphIds::file, a.fileName, nullptr);
        p.setProperty (GraphIds::start, static_cast<juce::int64> (a.start), nullptr);
        p.setProperty (GraphIds::length, static_cast<juce::int64> (a.length), nullptr);
        p.setProperty (GraphIds::rate, a.sampleRate, nullptr);
        p.setProperty (GraphIds::channels, a.channels, nullptr);
        audioTree.appendChild (p, nullptr); // later passes win where they overlap
    }
    ++audioGeneration;
    ++generation;
}

std::vector<AudioPass> GraphModel::removeAudioPasses()
{
    auto passes = getAudioPasses();
    if (audioTree.isValid())
        audioTree.removeAllChildren (nullptr);
    ++audioGeneration;
    ++generation;
    return passes;
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

int GraphModel::importFromAuto (dsp::NoteMapper mapper, double minNoteSeconds, double smoothSeconds)
{
    undo.beginNewTransaction (juce::String::fromUTF8 ("Importar desde automático"));
    clearObjects();

    // Centred moving average (+-smoothSeconds of voiced frames) so vibrato and
    // jitter don't split notes; the mapper's hysteresis does the rest.
    const double kSmooth = std::max (0.0, smoothSeconds);
    constexpr double kMaxGap = 0.05;
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
    float runPitch = 0.0f; // the note's target incl. its per-note detune
    double runStart = 0.0, runEnd = 0.0, lastTime = -1.0e9;

    auto flush = [&] {
        runStart = std::max (0.0, runStart); // the timeline starts at 0 (older captures had frames before it)
        if (runNote >= 0 && runEnd - runStart >= minNoteSeconds)
        {
            addNote (runStart, runEnd, runPitch);
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
            runPitch = target.targetMidi;
            runStart = f.time;
        }
        runEnd = f.time + kFrameSeconds;
    }
    flush();
    return created;
}

int GraphModel::importAsCurves()
{
    undo.beginNewTransaction (juce::String::fromUTF8 ("Importar como curvas"));
    clearObjects();

    constexpr double kMaxGap = 0.05, kSpacing = 0.02, kMinLength = 0.06;
    int created = 0;
    std::vector<std::pair<double, float>> run;
    auto flush = [&] {
        run.erase (std::remove_if (run.begin(), run.end(), [] (const auto& pt) { return pt.first < 0.0; }), run.end());
        if (run.size() >= 2 && run.back().first - run.front().first >= kMinLength)
        {
            addCurve (run);
            ++created;
        }
        run.clear();
    };
    double last = -1.0e9;
    for (const auto& f : captured)
    {
        if (f.midi < 0.0f || f.time - last > kMaxGap)
            flush();
        if (f.midi >= 0.0f && (run.empty() || f.time - run.back().first >= kSpacing - 1.0e-6))
            run.emplace_back (f.time, f.midi);
        if (f.midi >= 0.0f)
            last = f.time;
    }
    flush();
    return created;
}

std::vector<std::pair<double, float>> GraphModel::scaleCurveVibrato (const std::vector<std::pair<double, float>>& pts, float keep)
{
    // centre = running median over +-90 ms (removes a vibrato cycle, keeps glides)
    std::vector<std::pair<double, float>> out (pts);
    std::vector<float> window;
    for (size_t i = 0, lo = 0, hi = 0; i < pts.size(); ++i)
    {
        while (pts[lo].first < pts[i].first - 0.09)
            ++lo;
        hi = std::max (hi, i);
        while (hi + 1 < pts.size() && pts[hi + 1].first <= pts[i].first + 0.09)
            ++hi;
        window.clear();
        for (size_t k = lo; k <= hi; ++k)
            window.push_back (pts[k].second);
        const auto mid = window.begin() + static_cast<long> (window.size() / 2);
        std::nth_element (window.begin(), mid, window.end());
        out[i].second = *mid + keep * (pts[i].second - *mid);
    }
    return out;
}

//==============================================================================
std::unique_ptr<dsp::CorrectionTrack> GraphModel::compile() const
{
    auto track = std::make_unique<dsp::CorrectionTrack>();
    if (! graph.isValid())
        return track;

    const auto& map = timeMap; // objects live in source time; the track is in output time
    for (const auto& t : graph)
    {
        dsp::CorrectionTrack::Segment s;
        s.start = map.toOutput (getD (t, kStart));
        s.end = map.toOutput (getD (t, kEnd));
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
            auto pts = readPoints (t);
            if (pts.size() < 2)
                continue;
            if (const float keep = getF (t, kVibrato, -1.0f); keep >= 0.0f && std::abs (keep - 1.0f) > 1.0e-4f)
                pts = scaleCurveVibrato (pts, keep); // 0 = flat, 1 = as drawn, 2 = double
            for (auto& p : pts)
                p.first = map.toOutput (p.first);
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
