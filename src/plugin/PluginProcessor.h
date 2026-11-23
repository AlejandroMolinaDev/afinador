#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/CorrectionCurve.h"
#include "dsp/HarmonyEngine.h"
#include "dsp/HqShifter.h"
#include "dsp/KeyDetector.h"
#include "dsp/MidiNoteState.h"
#include "dsp/NoteMapper.h"
#include "dsp/PitchDetector.h"
#include "dsp/PsolaShifter.h"
#include "dsp/VibratoAnalyzer.h"
#include "dsp/VibratoSynth.h"
#include "plugin/InstanceRegistry.h"
#include "plugin/MidiLearn.h"
#include "plugin/Parameters.h"
#include "plugin/PitchFrameFifo.h"
#include "plugin/PresetManager.h"
#include "plugin/graph/GraphModel.h"
#include "plugin/graph/GraphRuntime.h"
#include "plugin/graph/TimeRenderer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <tuple>

#if HAS_CLAP_JUCE_EXTENSIONS
 #include <clap-juce-extensions/clap-juce-extensions.h>
#endif

namespace novapitch
{

/** Lock-free runtime diagnostics written by the audio thread and read by the
    editor: how FL Studio (or any host) drives the plugin. */
struct HostDiagnostics
{
    std::atomic<double>   sampleRate { 0.0 };
    std::atomic<int>      maxBlockFromPrepare { 0 };
    std::atomic<int>      lastBlockSize { 0 };
    std::atomic<int>      minBlockSize { 0 };   // smallest non-zero block seen
    std::atomic<int>      maxBlockSize { 0 };
    std::atomic<int>      numInputChannels { 0 };
    std::atomic<int>      numOutputChannels { 0 };
    std::atomic<uint32_t> midiEventsReceived { 0 };
    std::atomic<uint32_t> processCalls { 0 };

    void resetBlockStats() noexcept
    {
        lastBlockSize.store (0, std::memory_order_relaxed);
        minBlockSize.store (0, std::memory_order_relaxed);
        maxBlockSize.store (0, std::memory_order_relaxed);
    }
};

/** Signal flow (per sub-block of <= kControlBlock samples, so control runs every
    ~2.7 ms whatever block size the host uses):
        input -> mono sum -> PitchDetector --(frame)--> NoteMapper -> CorrectionCurve
              -> ratio -> PsolaShifter (all channels, same grains) -> dry/wet -> output */
class NovaPitchProcessor final : public juce::AudioProcessor,
                                 private dsp::PitchDetector::Listener,
                                 private juce::Timer,
                                 private InstanceRegistry::Member
                              #if HAS_CLAP_JUCE_EXTENSIONS
                               , public clap_juce_extensions::clap_properties
                              #endif
{
public:
    static constexpr int kControlBlock = 32;
    static constexpr double kLiveLatencySeconds = 0.003;
    static constexpr double kTrackingLatencySeconds = 0.010;
    static constexpr float kVintageGrainScale = 0.7f;

    NovaPitchProcessor();
    ~NovaPitchProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    const juce::String getName() const override { return "NovaPitch"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; } // "MIDI: enviar notas" (corrected notes out)
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    /** Human readable plugin format (VST3 / AU / CLAP / Standalone ...). */
    juce::String getFormatName() const;

    /** Latency of a processing mode at a sample rate, in samples.
        (HQ configures a temporary engine: not for the audio thread.) */
    static int latencyForMode (QualityMode mode, double sampleRate);

    HostDiagnostics& getDiagnostics() noexcept { return diagnostics; }
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    /** Pitch frames for the UI (consumer: message thread). */
    PitchFrameFifo& getPitchFrameFifo() noexcept { return pitchFifo; }

    PresetManager& getPresetManager() noexcept { return *presetManager; }
    MidiLearn& getMidiLearn() noexcept { return midiLearn; }
    const dsp::MidiNoteState& getMidiNotes() const noexcept { return midiNotes; }

    /** Writes the pitch classes played while "learn scale" is on into the scale
        and note-state parameters (message thread; called by the timer). */
    void applyLearnedScale();

    /** Incremented (message thread) whenever MIDI mappings change: lets the editor refresh labels. */
    int getMidiMappingGeneration() const noexcept { return midiMappingGeneration.load(); }

    //==========================================================================
    // Graphical mode (phase 8). FL Studio has no ARA: the pitch curve is captured
    // while the host plays ("capture by playback") and indexed by host time.
    GraphModel& getGraphModel() noexcept { return graphModel; }
    /** Starts/stops capturing the detected pitch while the host plays (message thread). */
    void setGraphCapture (bool shouldCapture);
    bool isGraphCapturing() const noexcept { return captureRequested.load(); }
    /** Host timeline position of the last block and whether the host is playing. */
    double getHostTimeSeconds() const noexcept { return hostTimeSeconds.load (std::memory_order_relaxed); }
    bool isHostPlaying() const noexcept { return hostPlaying.load (std::memory_order_relaxed); }
    /** Replaces the graph objects with notes from the captured curve, using the
        current key / scale / note states (one undo step). Returns notes created. */
    int importGraphFromAuto (int detail = 1);
    /** Replaces the graph objects with curves that follow the captured pitch (editable). */
    int importGraphAsCurves();
    //==========================================================================
    // Key detection (phase 9): f0 chromagram + Krumhansl–Schmuckler profiles.
    /** Starts listening to the input for `voicedSeconds` of sung material (message thread). */
    void startKeyListening (double voicedSeconds);
    /** Stops listening; with `apply`, the detected key/scale is written to the parameters. */
    dsp::KeyDetector::Result finishKeyListening (bool apply);
    bool isKeyListening() const noexcept { return keyListening.load(); }
    /** Voiced seconds heard so far and the target. */
    double getKeyListenSeconds() const noexcept { return keyVoicedSeconds.load(); }
    double getKeyListenTarget() const noexcept { return keyListenTarget; }
    /** Key of the graphical mode's captured curve (message thread). */
    dsp::KeyDetector::Result detectKeyFromCapture (bool apply);
    /** Last detection result and a counter that changes with every new result (UI). */
    dsp::KeyDetector::Result getLastKeyResult() const noexcept { return lastKeyResult; }
    int getKeyResultGeneration() const noexcept { return keyResultGeneration; }
    /** Writes a key (tonic + Major/Natural minor) into the key and scale parameters. */
    void applyKey (const dsp::KeyDetector::Result&);

    // Instance sync (phase 9) ---------------------------------------------------
    /** Sends this instance's key, scale, note states and A4 to the other instances
        in this process. Returns how many received them. */
    int syncToOtherInstances();
    static int getNumInstancesInProcess() { return InstanceRegistry::getInstance().size(); }
    /** Applies settings received from another instance (message thread; called by the timer). */
    void applyPendingSync();

    // Captured audio + time edits (phase 9) --------------------------------------
    /** Default folder of the captured audio (per user, outside the project). */
    static juce::File defaultCaptureRoot();
    void setCaptureRoot (const juce::File& root) { captureRoot = root; }
    juce::File getCaptureRoot() const { return captureRoot; }
    /** Forgets the captured pitch curve and deletes this instance's captured audio. */
    void clearGraphCapture();
    /** Writes pending captured audio and collects finished passes now (tests, shutdown). */
    void flushGraphCapture();
    /** Renders the time edits synchronously and publishes them (tests / offline tools). */
    void renderTimeEditsNow();
    bool isRenderingTime() const noexcept { return timeRenderer.isBusy() || timeDirty; }
    /** True when some time edit could not be rendered because its captured audio is gone. */
    bool isTimeAudioMissing() const noexcept { return timeAudioMissing; }

    /** Message-thread housekeeping of the graphical mode: drains the capture
        FIFO, commits capture passes, frees retired tracks. Called by the timer
        (public so tests can run it without a message loop). */
    void processGraphMessages();

private:
    struct ParamPointers
    {
        std::atomic<float>* inputProfile = nullptr;
        std::atomic<float>* sensitivity = nullptr;
        std::atomic<float>* qualityMode = nullptr;
        std::atomic<float>* key = nullptr;
        std::atomic<float>* scale = nullptr;
        std::atomic<float>* referenceA4 = nullptr;
        std::atomic<float>* retuneSpeed = nullptr;
        std::atomic<float>* tolerance = nullptr;
        std::atomic<float>* naturalHold = nullptr;
        std::atomic<float>* transpose = nullptr;
        std::atomic<float>* mix = nullptr;
        std::array<std::atomic<float>*, 12> noteStates {};
        std::atomic<float>* midiTarget = nullptr;
        std::atomic<float>* midiScaleLearn = nullptr;
        struct Voice { std::atomic<float>* mode, * degrees, * semitones, * formant, * pan, * level, * humanize, * mute, * solo; };
        std::array<Voice, dsp::HarmonyEngine::kVoices> voices {};
        std::atomic<float>* busHpf = nullptr, * busLpf = nullptr, * busEqFreq = nullptr, * busEqGain = nullptr,
                          * busAttack = nullptr, * busRelease = nullptr, * busGate = nullptr, * busWidth = nullptr;
        std::atomic<float>* formantPreserve = nullptr;
        std::atomic<float>* formantShift = nullptr;
        std::atomic<float>* formantLink = nullptr;
        std::atomic<float>* character = nullptr;
        std::atomic<float>* vibratoPreserve = nullptr;
        std::atomic<float>* vibRate = nullptr;
        std::atomic<float>* vibDepth = nullptr;
        std::atomic<float>* vibDelay = nullptr;
        std::atomic<float>* vibVariation = nullptr;
        std::atomic<float>* vibAmpMod = nullptr;
        std::atomic<float>* vibFormantMod = nullptr;
        std::atomic<float>* graphMode = nullptr;
        std::atomic<float>* midiOut = nullptr;
        std::atomic<float>* midiTargetExact = nullptr;
        std::atomic<float>* vibShape = nullptr;
    };

    /** Reads all parameters and pushes them into the DSP objects (audio thread). */
    void updateDspFromParameters() noexcept;
    QualityMode currentMode() const noexcept;
    void runChain (juce::AudioBuffer<float>&, bool bypassed) noexcept;

    // PitchDetector::Listener (audio thread)
    void pitchFrameReady (const dsp::PitchEstimate&) noexcept override;

    // Timer (message thread): forwards latency changes to the host (FL PDC).
    void timerCallback() override;

    juce::AudioProcessorValueTreeState apvts;
    ParamPointers params;

    HostDiagnostics diagnostics;

    dsp::PitchDetector pitchDetector;
    dsp::NoteMapper noteMapper;
    dsp::CorrectionCurve correctionCurve;
    dsp::PsolaShifter shifter;   // Live, Tracking, Vintage
    dsp::HqShifter hqShifter;    // HQ
    dsp::VibratoAnalyzer vibratoAnalyzer;
    dsp::VibratoSynth vibratoSynth;
    int synthNote = -1;
    bool wasVoiced = false;
    float vibratoKeep = 1.0f;          // 0..2 ("Conservar vibrato")
    float outputGain = 1.0f, outputGainTarget = 1.0f; // vibrato amplitude modulation
    bool hqActive = false;
    dsp::HarmonyEngine harmony;
    dsp::MidiNoteState midiNotes;
    bool midiTargetOn = false;
    bool midiTargetExactOn = false;

    // MIDI note output (audio thread): notes collected by the frame callback,
    // written to the host's buffer at the end of the block.
    struct MidiOutEvent
    {
        int offset;
        int note;
        int velocity; // 0 = note off
    };
    static constexpr int kMaxMidiOutEvents = 64;
    void queueMidiOut (int offset, int note, int velocity) noexcept;
    void flushMidiOut (juce::MidiBuffer&) noexcept;
    std::array<MidiOutEvent, kMaxMidiOutEvents> midiOutEvents {};
    int numMidiOutEvents = 0;
    int midiOutNote = -1;
    int midiOutCandidate = -1, midiOutCandidateFrames = 0;
    bool midiOutOn = false;
    int chunkOffsetInBlock = 0;
    bool wasLearningScale = false; // message thread
    uint16_t appliedLearnedMask = 0; // message thread
    std::array<int, kNumQualityModes> modeLatency {};

    std::vector<float> monoScratch;
    PitchFrameFifo pitchFifo;
    double inputSampleRate = 48000.0;

    // Values cached once per block for the frame callback
    float a4Hz = 440.0f;
    float transposeSemitones = 0.0f;
    QualityMode blockMode = kDefaultQualityMode;
    bool formantPreserve = true;
    bool formantLink = false;
    float formantStaticFactor = 1.0f; // shift x character (x link part)

    std::atomic<int> desiredLatency { 0 };

    MidiLearn midiLearn { *this };
    std::unique_ptr<PresetManager> presetManager;
    std::atomic<int> midiMappingGeneration { 0 };

    // Key detection / instance sync ---------------------------------------------
    void receiveSongSettings (const SongSettings&) override;
    void setParameterValue (const juce::String& id, float value);

    std::array<std::atomic<float>, 12> keyChroma {};   // written by the audio thread while listening
    std::atomic<double> keyVoicedSeconds { 0.0 };
    std::atomic<bool> keyListening { false };
    double keyListenTarget = 10.0;
    dsp::KeyDetector::Result lastKeyResult;
    int keyResultGeneration = 0;
    juce::SpinLock syncLock;
    SongSettings pendingSync;
    std::atomic<bool> hasPendingSync { false };

    // Captured audio + time edits ------------------------------------------------
    TimeRenderer::Job makeTimeJob();
    void updateTimeRender();
    AudioCapture audioCapture;
    TimeRenderer timeRenderer;
    Publisher<RenderedTime> timePublisher;
    juce::File captureRoot = defaultCaptureRoot();
    const RenderedTime* blockTime = nullptr; // audio thread
    int64_t blockHostSample = 0;             // audio thread
    std::tuple<int, int, double, int> timeKey { -1, -1, 0.0, 0 }; // message thread
    juce::uint32 timeChangedAt = 0;
    bool timeDirty = false;
    int timeJobCounter = 0;
    bool timeAudioMissing = false;

    // Graphical mode ------------------------------------------------------------
    void readPlayHead() noexcept;
    void publishGraphTrack();

    juce::UndoManager graphUndo;
    GraphModel graphModel { graphUndo };   // message thread
    TrackPublisher trackPublisher;
    CaptureFifo captureFifo;
    std::atomic<bool> captureRequested { false };
    std::atomic<double> hostTimeSeconds { 0.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<uint64_t> audioBlocksDone { 0 };
    // audio thread
    const dsp::CorrectionTrack* blockTrack = nullptr;
    bool graphModeOn = false;
    bool blockPlaying = false;
    double blockHostTime = 0.0;
    double chunkHostTime = 0.0;
    int64_t chunkStartIndex = 0;
    double lastCaptureTime = -1.0e9;
    bool lastCaptureVoiced = false;
    dsp::CorrectionCurve::Settings autoCurveSettings;
    // message thread
    bool wasCapturing = false, wasHostPlaying = false;
    double lastDrainedTime = -1.0e9;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NovaPitchProcessor)
};

} // namespace novapitch
