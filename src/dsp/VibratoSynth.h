#pragma once

#include <cstdint>

namespace novapitch::dsp
{

/** Artificial vibrato for sustained notes (runs at the analysis frame rate).

    After `delay` since the note started it fades in over 250 ms a periodic
    (sine, triangle, softened square or sawtooth) pitch modulation (rate, depth in cents), with optional random variation of
    rate/depth (smoothed random walks), amplitude modulation and formant
    modulation in sync with the pitch. Each new note restarts the delay and
    the phase (starting at a zero crossing: no jump). Real-time safe. */
class VibratoSynth
{
public:
    struct Settings
    {
        float rateHz = 5.5f;       // 0.1..10
        float depthCents = 0.0f;   // 0..100 (0 = off)
        float delayMs = 300.0f;    // 0..2000
        float variation = 0.2f;    // 0..1
        float amplitudeMod = 0.0f; // 0..1 (1 = ±25 % gain)
        float formantMod = 0.0f;   // 0..1 (1 = formants follow the vibrato fully)
        int shape = 0;             // 0 sine, 1 triangle, 2 square (softened), 3 sawtooth (rising). Stored: append only
    };

    /** Waveform value (-1..1) of a vibrato shape at phase 0..2pi (exposed for tests). */
    static float shapeValue (int shape, double phase) noexcept;

    struct Output
    {
        float pitchCents = 0.0f;
        float gain = 1.0f;
        float formantFactor = 1.0f;
    };

    static constexpr float kFadeInSeconds = 0.25f;
    static constexpr float kMaxAmplitudeMod = 0.25f;

    void prepare (double frameRateHz, uint32_t seed = 0x2545f491u) noexcept;
    void setFrameRate (double frameRateHz) noexcept { if (frameRateHz > 0.0) dt = 1.0 / frameRateHz; }
    void reset() noexcept;
    void setSettings (const Settings& s) noexcept { settings = s; }

    /** noteStarted: true on the first frame of a new note (or voiced onset). */
    Output process (bool voiced, bool noteStarted) noexcept;

private:
    float random() noexcept; // uniform -1..1

    Settings settings;
    double dt = 1.0 / 375.0;
    double phase = 0.0;
    double noteTime = 0.0;
    float rateWalk = 0.0f, depthWalk = 0.0f;
    uint32_t rng = 0x2545f491u, seed0 = 0x2545f491u;
    bool active = false;
};

} // namespace novapitch::dsp
