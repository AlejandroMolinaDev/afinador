#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "dsp/NoteMapper.h"
#include "dsp/graph/CorrectionTrack.h"
#include "dsp/graph/TimeMap.h"
#include "plugin/graph/AudioCapture.h"

#include <functional>
#include <memory>
#include <vector>

namespace novapitch
{

/** ValueTree types and properties of the graphical-mode objects. */
namespace GraphIds
{
inline const juce::Identifier note ("Note"), line ("Line"), curve ("Curve");
inline const juce::Identifier id ("id"), start ("start"), end ("end"), pitch ("pitch"), endPitch ("endPitch"),
    points ("points"), captured ("captured"), retuneMs ("retuneMs"), vibratoKeep ("vibratoKeep"), formantSt ("formantSt");
// Phase 9: time edits (Graph@warp) and captured audio (plugin state child GraphAudio)
inline const juce::Identifier warp ("warp"), audio ("GraphAudio"), audioId ("audioId"), pass ("Pass"), file ("file"),
    length ("length"), rate ("rate"), channels ("channels");
} // namespace GraphIds

/** One captured analysis frame (host timeline). */
struct CaptureFrame
{
    double time = 0.0;      ///< seconds on the host timeline
    float midi = -1.0f;     ///< detected pitch (relative to A4 ref); < 0 = unvoiced
    float rmsDb = -120.0f;  ///< input level (waveform overview)
};
static_assert (sizeof (CaptureFrame) == 16, "CaptureFrame is stored as raw bytes in the project state");

/** Graphical-mode data model (message thread only).

    Lives in the plugin state as a "Graph" child, so it is saved with the FL
    Studio project:
      Graph [captured = MemoryBlock of CaptureFrame]
        Note  { id, start, end, pitch, retuneMs, vibratoKeep, formantSt }
        Line  { id, start, end, pitch, endPitch }
        Curve { id, start, end, points = MemoryBlock of (double t, float pitch) }
    Every edit goes through the UndoManager (unlimited undo/redo) and triggers
    onChanged, which recompiles the CorrectionTrack for the audio thread. */
class GraphModel : private juce::ValueTree::Listener
{
public:
    static constexpr const char* kTag = "Graph";
    /** Spacing of captured frames (the audio thread decimates to this). */
    static constexpr double kFrameSeconds = 0.01;

    explicit GraphModel (juce::UndoManager&);
    ~GraphModel() override;

    /** (Re)attach to the plugin state tree (after setStateInformation). */
    void bind (juce::ValueTree pluginState);

    // Capture ---------------------------------------------------------------------
    /** Frames of the capture pass in progress (merged on commitCapture). */
    void appendCaptured (const CaptureFrame* frames, int count);
    /** Ends a capture pass: the new pass replaces captured data in its time range. */
    void commitCapture();
    void clearCaptured();
    const std::vector<CaptureFrame>& getCaptured() const noexcept { return captured; }
    const std::vector<CaptureFrame>& getPendingCapture() const noexcept { return pending; }

    // Objects ---------------------------------------------------------------------
    juce::ValueTree getObjects() const { return graph; }
    juce::ValueTree findById (juce::int64 id) const;

    juce::ValueTree addNote (double start, double end, float pitch);
    juce::ValueTree addLine (double start, double end, float pitch, float endPitch);
    juce::ValueTree addCurve (const std::vector<std::pair<double, float>>& points);
    void moveObject (juce::ValueTree object, double deltaSeconds, float deltaPitch);
    void setBounds (juce::ValueTree object, double start, double end);
    void setObjectProperty (juce::ValueTree object, const juce::Identifier& id, const juce::var& value);
    bool split (juce::ValueTree note, double at);
    bool merge (juce::ValueTree first, juce::ValueTree second);
    void remove (juce::ValueTree object);
    void clearObjects();

    /** Builds Note objects from the captured curve with the given mapper (key,
        scale, note states). Replaces the existing objects (one undo step).
        Returns the number of notes created. */
    int importFromAuto (dsp::NoteMapper mapper, double minNoteSeconds = 0.08, double smoothSeconds = 0.04);

    /** Builds Curve objects that follow the captured pitch exactly (one per
        sung phrase, a point every 20 ms): nothing changes until they are
        edited. Replaces the existing objects (one undo step). Returns the
        number of curves created. */
    int importAsCurves();

    /** Running-median centre used to scale a curve's vibrato (exposed for tests). */
    static std::vector<std::pair<double, float>> scaleCurveVibrato (const std::vector<std::pair<double, float>>& points, float keep);

    // Time edits (phase 9) -------------------------------------------------------
    /** Output time (host timeline) <-> source time (captured audio). Objects and the
        captured curve are stored in source time; compile() maps them to output time. */
    const dsp::TimeMap& getTimeMap() const noexcept { return timeMap; }
    /** Undoable (one step per gesture with beginEdit). Invalid maps are ignored. */
    void setTimeMap (const dsp::TimeMap&);
    void clearTimeEdits();
    int getTimeMapGeneration() const noexcept { return timeMapGeneration; }

    // Captured audio (phase 9) ----------------------------------------------------
    /** Identifier of this instance's capture folder (created when `create`). */
    juce::String getAudioId (bool create);
    std::vector<AudioPass> getAudioPasses() const;
    void addAudioPasses (const std::vector<AudioPass>&);
    /** Forgets every pass and returns them (the caller deletes the files). */
    std::vector<AudioPass> removeAudioPasses();
    int getAudioGeneration() const noexcept { return audioGeneration; }

    /** Compiled form for the audio thread. */
    std::unique_ptr<dsp::CorrectionTrack> compile() const;

    /** Starts a new undo step (call once per user gesture). */
    void beginEdit (const juce::String& name = {}) { undo.beginNewTransaction (name); }

    /** Objects changed (any edit, undo/redo, load): recompile the track. */
    std::function<void()> onChanged;
    /** Captured curve changed (not undoable). */
    std::function<void()> onCaptureChanged;

    juce::UndoManager& getUndoManager() noexcept { return undo; }

    /** Incremented on every change of objects or captured data (lets the UI poll). */
    int getGeneration() const noexcept { return generation; }

    /** Points of a Curve object (time, pitch). */
    static std::vector<std::pair<double, float>> readCurvePoints (const juce::ValueTree& curve);

private:
    void storeCaptured();
    void loadCaptured();
    void loadTimeMap();
    juce::ValueTree makeObject (const juce::Identifier& type, double start, double end);
    void changed();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override
    {
        if (property == GraphIds::warp)
            loadTimeMap(); // also after undo / redo
        changed();
    }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { changed(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { changed(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { changed(); }

    juce::UndoManager& undo;
    juce::ValueTree graph, audioTree;
    dsp::TimeMap timeMap;
    int timeMapGeneration = 0, audioGeneration = 0;
    std::vector<CaptureFrame> captured, pending;
    juce::int64 nextId = 1;
    bool loading = false;
    int generation = 0;
};

} // namespace novapitch
