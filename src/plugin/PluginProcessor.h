#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/CorrectionCurve.h"
#include "dsp/HqShifter.h"
#include "dsp/NoteMapper.h"
#include "dsp/PitchDetector.h"
#include "dsp/PsolaShifter.h"
#include "dsp/VibratoAnalyzer.h"
#include "dsp/VibratoSynth.h"
#include "plugin/MidiLearn.h"
#include "plugin/Parameters.h"
#include "plugin/PitchFrameFifo.h"
#include "plugin/PresetManager.h"

#include <array>
#include <atomic>
#include <cstdint>

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
                                 private juce::Timer
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
    bool producesMidi() const override { return false; }
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

    /** Incremented (message thread) whenever MIDI mappings change: lets the editor refresh labels. */
    int getMidiMappingGeneration() const noexcept { return midiMappingGeneration.load(); }

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NovaPitchProcessor)
};

} // namespace novapitch
