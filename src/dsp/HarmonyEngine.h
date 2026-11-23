#pragma once

#include "dsp/Biquad.h"
#include "dsp/PsolaShifter.h"

#include <array>
#include <cstdint>
#include <vector>

namespace novapitch::dsp
{

/** Harmony voice modes. Stored in state: append only. */
enum class HarmonyMode : int
{
    Off = 0,
    Diatonic = 1,  ///< N scale degrees from the lead's target note (current scale)
    Semitones = 2, ///< fixed interval
    Midi = 3       ///< i-th lowest held MIDI note (chords from FL's piano roll)
};

inline constexpr int kNumHarmonyModes = 4;

struct HarmonyVoiceSettings
{
    HarmonyMode mode = HarmonyMode::Off;
    int degrees = 2;         ///< diatonic: +2 = third above, -2 = third below, +7 = octave
    int semitones = 7;       ///< semitone mode
    float formantSt = 0.0f;  ///< ±12
    float pan = 0.0f;        ///< -1 (L) .. +1 (R)
    float levelDb = -6.0f;
    float humanize = 0.2f;   ///< 0..1 → up to ±12 c slow detune and 0–30 ms delay
    bool mute = false;
    bool solo = false;
};

struct HarmonyBusSettings
{
    float hpfHz = 120.0f;
    float lpfHz = 12000.0f;
    float eqFreqHz = 2500.0f;
    float eqGainDb = 0.0f;
    float attackMs = 30.0f;
    float releaseMs = 200.0f;
    float gateDb = -60.0f;
    float width = 1.0f; ///< 0 = mono, 1 = as panned, 2 = wide
};

/** Per analysis frame information about the lead voice. Pitches are MIDI
    note numbers relative to the A4 reference. */
struct HarmonyFrame
{
    bool voiced = false;
    float periodSamples = 0.0f;
    float detectedMidi = 0.0f;   ///< input pitch
    float leadOutputMidi = 0.0f; ///< lead pitch after correction, vibrato and transposition
    int leadNote = -1;           ///< lead target note incl. transposition (-1 = none)
    uint16_t scaleMask = 0x0fff; ///< valid pitch classes for diatonic steps
    float inputRmsDb = -120.0f;
    int numHeld = 0;
    std::array<int, 16> held {}; ///< held MIDI notes, ascending
};

/** Four harmony voices generated from the lead vocal. Each voice has its own
    PSOLA shifter (formant preserving) running with the lead's latency, so the
    harmonies stay aligned with the corrected lead in every mode. The lead's
    analysis (f0, voicing, target) is reused: voices only add their interval.
    Voices inherit the lead's expression (vibrato, deviation from the note).
    Bus: HPF, LPF, peaking EQ, attack/release envelope, level gate, width.
    Real-time safe after prepare(). */
class HarmonyEngine
{
public:
    static constexpr int kVoices = 4;

    void prepare (double sampleRate, int maxLatencySamples, int maxBlockSize, double frameRate);
    void reset() noexcept;
    void setLatency (int samples) noexcept;

    void setVoice (int index, const HarmonyVoiceSettings&) noexcept;
    void setBus (const HarmonyBusSettings&) noexcept;
    bool anyVoiceEnabled() const noexcept;

    /** Once per analysis frame (audio thread). */
    void updateControl (const HarmonyFrame&) noexcept;

    /** Adds the stereo harmony bus to outL/outR (outR may equal outL for mono). */
    void process (const float* monoIn, float* outL, float* outR, int numSamples) noexcept;

    /** Diatonic step: `degrees` scale steps from `note` within `mask` (12-bit pitch classes). */
    static int stepDegrees (int note, int degrees, uint16_t mask) noexcept;

    /** Target pitch of a voice for a frame (exposed for tests). Returns false if the voice is silent. */
    bool voiceTargetMidi (int index, const HarmonyFrame&, float& midiOut) const noexcept;

private:
    struct Voice
    {
        HarmonyVoiceSettings settings;
        PsolaShifter shifter;
        std::vector<float> buffer;
        std::vector<float> delayLine;
        int delayWrite = 0;
        int delaySamples = 0;
        float env = 0.0f, envTarget = 0.0f;
        float panL = 0.7071f, panR = 0.7071f;
        float levelLin = 0.5f;
        float detuneWalk = 0.0f;
        uint32_t rng = 1u;
    };

    float random (Voice&) noexcept;
    void updateFilters() noexcept;

    double sampleRate = 48000.0;
    double frameRate = 375.0;
    int maxBlock = 0;
    std::array<Voice, kVoices> voices;
    HarmonyBusSettings bus, appliedBus;
    bool filtersDirty = true;
    std::array<Biquad, 2> hpf, lpf, peak;
    std::vector<float> busL, busR;
    float attackCoeff = 0.001f, releaseCoeff = 0.0001f;
};

} // namespace novapitch::dsp
